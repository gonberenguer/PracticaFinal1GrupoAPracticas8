/**
Monitor.c

    Funcionalidad:
        Proceso de tipo demonio que, a partir de los valores del fichero de configuración,
        espera una señal de FileProcessor a través de un named pipe y, cuando recibe la señal
        se encarga de detectar los patrones de fraude definidos.

        Se comunica con el proceso FileProcessor utilizando named pipe, y se sincroniza con dicho proceso
        utilizando un semáforo común.

        Escribe datos de la operación en los ficheros de log.

    Compilación:
        gcc Monitor.c -o Monitor

    Ejecución:
        ./Monitor

    Parámetros:
        No Utiliza parámetros de ejecución
*/ 

// Nombre del fichero de configuración
#define FICHERO_CONFIGURACION "Monitor.conf"

// Número de patrones de fraude implementados
#define NUM_PATRONES_FRAUDE 5

// Segundos que debe dormir el hilo cuando no está activo
#define SEGUNDOS_HILO_DORMIDO 5

// ------------------------------------------------------------------
// Librerías necesarias y explicación
// ------------------------------------------------------------------
#pragma region Librerias
#include <stdio.h>          // Funciones estándar de entrada y salida
#include <stdlib.h>         // Funciones útiles para varias operaciones: atoi, exit, malloc, rand...
#include <string.h>         // Tratamiento de cadenas de caracteres
#include <pthread.h>        // Tratamiento de hilos y mutex
#include <time.h>           // Tratamiento de datos temporales
#include <stdarg.h>         // Tratamiento de parámetros opcionales va_init...
#include <semaphore.h>      // Tratamiento de semáforos
#include <unistd.h>         // Gestión de procesos, acceso a archivos, pipe, control de señales
#include <sys/types.h>      // Definiciones de typos de datos: pid_t, size_t...
#include <sys/stat.h>       // Definiciones y estructuras para trabajar con estados de archivos Linux
#include <dirent.h>         // Definiciones y estructuras necesarias para trabajar con directorios en Linux
#include <linux/limits.h>   // Define varias constantes que representan los límites del sistema en sistemas operativos Linux
#include <fcntl.h>          // Proporciona funciones y constantes para controlar archivos y descriptores de archivo en Linux 
#include <signal.h>         // Manejo de la señal CTRL-C
#include <glib.h>           // Manejo de diccionarios GLib utilizado para la detección de patrones de fraude

#include "Monitor.h"        // Declaración de funciones de este módulo
#pragma endregion Librerias


// ------------------------------------------------------------------
// FUNCIONES DE TRATAMIENTO DEL FICHERO DE CONFIGURACIÓN
// ------------------------------------------------------------------
#pragma region FicheroConfiguracion

// Parámetros máximos del fichero de configuración
#define MAX_LONGITUD_LINEA 100
#define MAX_LONGITUD_CLAVE 50
#define MAX_LONGITUD_VALOR 50
#define MAX_ENTRADAS_CONFIG 100
#define MAX_LINE_LENGTH 1024

// Formato de la estructura de una linea del fichero de configuración. p.ej. FicheroConsolidado (clave) = consolidado.csv(valor)
struct EntradaConfiguracion {
    char clave[MAX_LONGITUD_CLAVE];
    char valor[MAX_LONGITUD_VALOR];
};

// Estructura para guardar el fichero de configuración en memoria y no tener que acceder a el muchas veces en el programa ppal.
struct EntradaConfiguracion configuracion[MAX_ENTRADAS_CONFIG];
int num_entradas = 0;
// Variable que indica que el fichero de configuracion ya está leído
int ficheroConfiguracionLeido = 0;

//Función que leera el archivo de configuración y sabrá guardar los parámetros de fp.conf
void leer_archivo_configuracion(const char *nombre_archivo) {
    
    // Como este fichero únicamente se lee al inicio del programa antes de crear los threads
    // no es necesario hacerlo thread safe

    FILE *archivo = fopen(nombre_archivo, "r");
    if (archivo == NULL) {
        //perror muestra un mensaje de error en caso de error al abrir el fichero
        perror("Error al abrir el archivo de configuración");
        exit(1);
    }

    char linea[MAX_LONGITUD_LINEA];
    //Vamos metiendo cada línea del fichero
    while (fgets(linea, sizeof(linea), archivo)) {
        // No leer las líneas vacías y comentarios (controlamos # para linux y ; para los archivos .ini de Windows)
        if (linea[0] == '\n' || linea[0] == '#' || linea[0] == ';')
            continue;

        char clave[MAX_LONGITUD_CLAVE];
        char valor[MAX_LONGITUD_VALOR];

        // Encontrar la posición del primer '=' en la línea; el valor está justo en la primera posición detrás del igual de la clave
        char *posicion_igual = strchr(linea, '=');
        if (posicion_igual == NULL) {
            continue;
        }

        // Copiar la clave
        //Con la operación de posicion_igual (posicion del símbolo =) - linea (0 siempre)
        strncpy(clave, linea, posicion_igual - linea);
        clave[posicion_igual - linea] = '\0'; // Terminar la clave

        // Leer el valor
        char *posicion_valor = posicion_igual + 1;
        //Controlamos que se pueda poner un espacio al meter el valor después del igual
        if (*posicion_valor == ' ') {
            posicion_valor++;
        }
        //Leo el valor
        sscanf(posicion_valor, "%[^\n]", valor);
        //Añadp clave y valor en la estructura
        if (num_entradas < MAX_ENTRADAS_CONFIG) {
            strcpy(configuracion[num_entradas].clave, clave);
            strcpy(configuracion[num_entradas].valor, valor);
            num_entradas++;
        } else {
            printf("Se alcanzó el máximo de entradas\n");
            break;
        }

    }

    // Con esto ya hemos leido el fichero de configuración
    ficheroConfiguracionLeido = 1;
    //Cerramos archivo 
    fclose(archivo);
}

//Funcion para acceder a los valores de el archivo .conf por Clave y poder usarlos en el resto del programa fácilmente
// Devuelve el valor de la clave del fichero de configuración 
// o un valor por defecto
const char *obtener_valor_configuracion(const char *clave, const char *valor_por_defecto) {
    // Si no hemos leído todavía el fichero de configuración, leerlo.  La primera vez, siempre se leerá
    if (ficheroConfiguracionLeido == 0) {
        leer_archivo_configuracion(FICHERO_CONFIGURACION);
    }
    for (int i = 0; i < num_entradas; i++) {
        if (strcmp(configuracion[i].clave, clave) == 0) {
            return configuracion[i].valor;
        }
    }
    //Si no he encontrado la clave en el .conf devulevo el valor por defecto
    return valor_por_defecto;
}
#pragma endregion FicheroConfiguracion


// ------------------------------------------------------------------
// FUNCIONES PARA ESCRITURA EN LOS FICHEROS DE LOG
// ------------------------------------------------------------------
#pragma region FicherosLog
/*
    Vamos a manejar dos ficheros de log:
        El primero será un log detallado que ayudará a ver el funcionamiento de la aplicación y nos permita depurar (clave de .conf LOG_FILE_APP)
        El segundo será el log que se pde en la práctica (clave de .conf LOG_FILE)

        Para ello emplearemos una variable adicional de tipo NivelLog con los siguientes valores (clave de .conf LOG_LEVEL)
            LOG_GENERAL: mensajes generales solicitados en el enunciado de la práctica, se escriben en ambos ficheros de log LOG_FILE_APP y LOG_FILE
            LOG_DEBUG: mensajes muy detallados utilizados para depurar el funcionamiento de la aplicación en desarrollo, se escribe en LOG_FILE_APP
            LOG_INFO: mensajes informativos acerca del funcionamiento de la aplicación, se escribe en LOG_FILE_APP
            LOG_WARNING: mensajes de advertencia (de momento no los he utilizado), se escribe en LOG_FILE_APP
            LOG_ERROR: mensajes de error que indican que algo ha funcionado incorrectamente, se escribe en LOG_FILE_APP
*/

//Mutex para seguridad de los hilos, garantiza que dos hilos no podrán escribir a la vez en el fichero de log
pthread_mutex_t mutex_escritura_log = PTHREAD_MUTEX_INITIALIZER;

#define ARCHIVO_LOG "file_log.log"
#define ARCHIVO_LOG_APP "logfile_app.log"

//Damos los posibles valores a el Nivel de Depuración que queremos para los ficheros de log
typedef enum LOG_LEVEL_TYPE {
    LOG_GENERAL,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} NivelLog;


/*
    Función de escritura en fichero de log segura para hilos
        NivelLog es el nivel de log para ese mensaje
        Módulo es una cadena que describe la parte del programa que ha generado el mensaje de log; ejemplo: hilopatronfraude
        Formato es cadena de formato C que aplicaremos para formatear los parametros del mensaje
    ... Número variable de parametro que compondrán el mensaje de log; ejemplo: numHilo, numRegistrosProcesados, etc
    
    Ejemplo de como llamar a la función: 
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_2", "Hilo %02d: Registro que cumple el patrón Clave: %s, Registros a la vez: %d\n", id_hilo, registro->clave, registro->cantidad);
*/
void escribirEnLog(NivelLog nivelLog, const char *modulo, const char *formato, ...) {

    // Primero comparar el nivel de log del mensaje con el nivel de log solicitado 
    // en el fichero de configuración para escribir en el log únicamente si es necesario

    // Obtener el nivel de log solicitado en el fichero de configuración
    const char *log_level_configuracion;
    log_level_configuracion = obtener_valor_configuracion("LOG_LEVEL", "LOG_INFO");
    // Calculo el nivel_log_solicitado (según typedef) a partir de la cadena log_level_configuracion
    NivelLog nivel_log_solicitado;
    if (strcmp(log_level_configuracion, "DEBUG") == 0) {
        nivel_log_solicitado = LOG_DEBUG;
    } else if (strcmp(log_level_configuracion, "GENERAL") == 0) {
        nivel_log_solicitado = LOG_GENERAL;
    } else if (strcmp(log_level_configuracion, "INFO") == 0) {
        nivel_log_solicitado = LOG_INFO;
    } else if (strcmp(log_level_configuracion, "WARNING") == 0) {
        nivel_log_solicitado = LOG_WARNING;
    } else if (strcmp(log_level_configuracion, "ERROR") == 0) {
        nivel_log_solicitado = LOG_ERROR;
    } else {
        // En caso de error ponemos LOG_DEBUG
        nivel_log_solicitado = LOG_DEBUG;
    }
        
    // Ver si es necesario escribir en el log, en función del niveLog y log_level

    // En principio no es necesario escribir en el log
    int escribirLog = 0;
    //Ahora vemos si hay que escribir
    if (nivel_log_solicitado == LOG_DEBUG) {
        // Si en el fichero de configuracion LOG_DEBUG, registramos todos los mensajes
        escribirLog = 1;
    } else if (nivelLog == LOG_GENERAL) {
        // Los mensajes de nivel de depuración LOG_GENERAL siempre se escriben
        escribirLog = 1;
    } else if (nivel_log_solicitado == LOG_INFO && (nivelLog == LOG_INFO || nivelLog == LOG_WARNING || nivelLog == LOG_ERROR)) {
        // Si en el fichero de configuracion LOG_INFO, registramos LOG_INFO, LOG_WARNING, LOG_ERROR
        escribirLog = 1;
    } else if (nivel_log_solicitado == LOG_WARNING && (nivelLog == LOG_WARNING || nivelLog == LOG_ERROR)) {
        // Si en el fichero de configuracion LOG_WARNING, registramos , LOG_WARNING, LOG_ERROR
        escribirLog = 1;
    } else if (nivel_log_solicitado == LOG_ERROR && nivelLog == LOG_ERROR) {
        // Si en el fichero de configuracion LOG_ERROR, registramos LOG_ERROR
        escribirLog = 1;
    }
    
    // Si no hay que escribir en el log, retornamos sin escribir en log
    if (escribirLog == 0) {
        return;
    }    

    //Si el programa llega hasta aquí es que hay que escribir
    // Bloquear el mutex
    pthread_mutex_lock(&mutex_escritura_log);

    // Obtener el nombre del archivo de log de aplicacion del fichero de configuración
    const char *fichero_log_aplicacion;
    fichero_log_aplicacion = obtener_valor_configuracion("LOG_FILE_APP", ARCHIVO_LOG_APP);
    FILE *archivo_log_aplicacion = fopen(fichero_log_aplicacion, "a");
    //Si hay un error, liberar el mutex para que no se quede estancado el programa
    if (archivo_log_aplicacion == NULL) {
        fprintf(stderr, "Error al abrir el archivo de log de aplicacion\n");
        pthread_mutex_unlock(&mutex_escritura_log);
        return;
    }

    // Obtener el nombre del archivo de log general del fichero de configuración
    const char *fichero_log_general;
    fichero_log_general = obtener_valor_configuracion("LOG_FILE", ARCHIVO_LOG);
    FILE *archivo_log_general = fopen(fichero_log_general, "a");
    //Si hay un error, liberar el mutex para que no se quede estancado el programa
    if (archivo_log_general == NULL) {
        fprintf(stderr, "Error al abrir el archivo de log general\n");
        pthread_mutex_unlock(&mutex_escritura_log);
        return;
    }

    // Obtener fecha y hora actual
    char fechaHora[20];
    obtenerFechaHora(fechaHora);
    char fechaHora2[20];
    obtenerFechaHora2(fechaHora2);

    // Obtener cadena de nivel de depuración de manera más estética
    const char *cadenaDepuracion;
    switch (nivelLog) {
        case LOG_DEBUG:
            cadenaDepuracion = "DEBUG   ";
            break;
        case LOG_INFO:
            cadenaDepuracion = "INFO    ";
            break;
        case LOG_WARNING:
            cadenaDepuracion = "WARNING ";
            break;
        case LOG_ERROR:
            cadenaDepuracion = "ERROR   ";
            break;
        case LOG_GENERAL:
            cadenaDepuracion = "GENERAL ";
            break;
        default:
            cadenaDepuracion = "UNKNOWN ";
            break;
    }

    
    // Escribir entrada de registro en el log de aplicación LOG_FILE_APP
    fprintf(archivo_log_aplicacion, "%s - [%s] - %s: ", fechaHora, cadenaDepuracion, modulo);
    //Todavía no hemos metido salto de línea

    // Si hay argumentos adicionales, escribirlos en una variable y en el fichero
    //Este snippet lo hemos buscado en internet y lo hemos entendido 
    char mensaje[255] = "";
    if (formato != NULL) {
        //va_lists almacenará los parámetros adicionales
        va_list args;
        //va_start es un "bucle" que incializa los parametros adicionales y comprueba el formato
        va_start(args, formato);
        //vsnprinft imprimer en la cadena mensaje los argumentos con el formato
        vsnprintf(mensaje, sizeof(mensaje), formato, args);
        //Lo escribimos en el fichero
        fprintf(archivo_log_aplicacion, "%s", mensaje);
        //Libera args
        va_end(args);
    } 


    // En caso de mensaje con nivel de depuración LOG_GENERAL
    // escribir entrada en log general y en pantalla
    // Formato fichero log aplicación: FECHA:::HORA:::NoPROCESO:::INICIO:::FIN:::NOMBRE_FICHERO:::NoOperacionesConsolidadas
    if (nivelLog == LOG_GENERAL) {
        // Formato fichero log aplicación: FECHA:::HORA:::
        /// Escribir en fichero de log general
        fprintf(archivo_log_general, "%s:::%s", fechaHora2, mensaje);
        // Escribir por pantalla
        printf("%s:::%s", fechaHora2, mensaje);
    }

    // Cerrar los 2 ficheros log
    fclose(archivo_log_general);
    fclose(archivo_log_aplicacion);

    // Desbloquear el mutex
    pthread_mutex_unlock(&mutex_escritura_log);
}
#pragma endregion FicherosLog


// ------------------------------------------------------------------
// FUNCIONES UTILES
// ------------------------------------------------------------------
#pragma region Utilidades

// Función para sleep centésimas de segundo
void sleep_centiseconds(int n) {
    struct timespec sleep_time;
    sleep_time.tv_sec = n / 100;            // Seconds
    sleep_time.tv_nsec = (n % 100) * 10000000;   // Convert centiseconds to nanoseconds

    // Call nanosleep with the calculated sleep time
    nanosleep(&sleep_time, NULL);
}

// Función que simula un retardo según los parámetros del fichero de configuración
void simulaRetardo(const char* mensaje) {
    int retardoMin;
    int retardoMax;
    const char* retardoMinTexto;
    const char* retardoMaxTexto;
    retardoMinTexto = obtener_valor_configuracion("SIMULATE_SLEEP_MIN", "1");
    retardoMaxTexto = obtener_valor_configuracion("SIMULATE_SLEEP_MAX", "2");
    retardoMin = atoi(retardoMinTexto);
    retardoMax = atoi(retardoMaxTexto);
    int retardo;

    // Inicializamos la semilla para generar números aleatorios
    srand(time(NULL));
    
    // Generamos el retardo como un número aleatorio dentro del rango
    retardo = rand() % (retardoMax - retardoMin + 1) + retardoMin;

    escribirEnLog(LOG_INFO,"retardo", "%s entrando en retardo simulado de %0d segundos\n", mensaje, retardo);
    sleep(retardo);

}

// Función para obtener la hora actual en formato HH:MM
char* obtener_hora_actual() {
    // Variable local para almacenar la hora actual
    static char hora_actual[10]; // "HH:MM:SS\0"

    // Obtener la hora actual
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    // Formatear la hora actual como "HH:MM"
    strftime(hora_actual, sizeof(hora_actual), "%H:%M:%S", timeinfo);

    return hora_actual;
}

// Función para obtener la fecha y hora actual (MODELO ESTÁNDAR)
void obtenerFechaHora(char *fechaHora) {
    time_t tiempoActual;
    struct tm *infoTiempo;

    time(&tiempoActual);
    infoTiempo = localtime(&tiempoActual);
    strftime(fechaHora, 20, "%Y-%m-%d %H:%M:%S", infoTiempo);
}

// Función para obtener la fecha y hora actual (MODELO PEDIDO POR MARLON)
void obtenerFechaHora2(char *fechaHora2) {
    time_t tiempoActual;
    struct tm *infoTiempo;

    time(&tiempoActual);
    infoTiempo = localtime(&tiempoActual);
    strftime(fechaHora2, 25, "%Y-%m-%d:::%H:%M:%S", infoTiempo);
}


#pragma endregion Utilidades


// ------------------------------------------------------------------
// DETECCION DE PATRONES DE FRAUDE
// ------------------------------------------------------------------
#pragma region DeteccionPatronesFraude

// Utilizaremos este semaforo para asegurar el acceso de los threads 
// a recursos compartidos (ficheros de entraa¡da y fichero consolidado)
sem_t *semaforo_consolidar_ficheros_entrada;
// Este es el nombre del semáforo
const char *semName;

// En esta matriz guardamos los mutex que utilizaremos para bloquear los hilos hasta que se recibe una notificación del pipe
pthread_mutex_t mutex_array[NUM_PATRONES_FRAUDE];

// Tamaño de los mensajes que se reciben a través del named pipe desde FileProcessor
#define MESSAGE_SIZE 100

// Pipe por el que recibiremos datos desde FileProcessor
int pipefd;

// Diccionario para patron_fraude_1
typedef struct REGISTRO_PATRON {
    char* clave;
    int cantidad;
    int operacion1Presente;
    int operacion2Presente;
    int operacion3Presente;
    int operacion4Presente;
} RegistroPatron;

void free_registroPatronF1(gpointer data) {
    RegistroPatron* registro = (RegistroPatron*)data;
    //g_free(registro->usuario);
    g_free(registro);
}

// Función para eliminar los últimos caracteres de una cadena
char *eliminarUltimosCaracteres(char *cadena, int n) {
    // La podemos utilizar para eliminar el :MM:DD de una fecha-hora de tipo YYYY-MM-DD HH:MM:SS (llamando con n=5)
    // La utilizamos para quitar el " €" en los importes": 120 €" (llamando con n=5)
    int longitud = strlen(cadena);
    if (longitud >= n) {
        cadena[longitud - n] = '\0';
    }
    return cadena;
}

// Función para concatenar 2 cadenas
char* concatenar_cadenas(char cadena1[30], char cadena2[30], char cadena3[30]) {
    static char resultado[100]; // Almacenará el resultado de la concatenación
    strcpy(resultado, cadena1); // Copia la primera cadena en resultado
    
    // Concatena la segunda cadena a resultado
    strcat(resultado, cadena2);

    // Concatena la tercera cadena a resultado
    strcat(resultado, cadena3);

    return resultado; // Devuelve la cadena concatenada
}


// Función para indicar a un hilo de patrón de fraude que se active (1) o desactive (0)
void activarHiloPatronFraude(int numPatron, int estado) {
    // La utilizamos para mantener los hilos bloqueados, hasta que se recibe una notificación en el pipe
    if (estado) {
        // Bloquear el mutex --> de esta forma bloquea la activación del hilo
        pthread_mutex_lock(&mutex_array[numPatron - 1]);
        escribirEnLog(LOG_DEBUG, "Monitor: activarHiloPatronFraude", "Hilo %02d activado en estado %01d\n", numPatron, estado);
    } else {
        // Desbloquear el mutex --> permite activar el hilo
        pthread_mutex_unlock(&mutex_array[numPatron - 1]);
        escribirEnLog(LOG_DEBUG, "Monitor: activarHiloPatronFraude", "Hilo %02d desactivado en estado %01d\n", numPatron, estado);
    }
}

// Función para escribir el resultado de los registros que cumplen con el patrón de fraude en un fichero
void escribirResultadoPatron(int patron, const char* mensaje) {
    const char *carpeta_datos;
    carpeta_datos = obtener_valor_configuracion("PATH_FILES", "../datos");
    const char *raiz_fichero_resultado;
    raiz_fichero_resultado = obtener_valor_configuracion("RESULTS_FILE", "resultado_patron_");
    char nombre_completo_fichero_resultado[PATH_MAX];
    sprintf(nombre_completo_fichero_resultado, "%s/%s%02d.csv", carpeta_datos, raiz_fichero_resultado, patron);

    // Escribir el registro en el fichero resultado
    FILE *fichero_resultado = fopen(nombre_completo_fichero_resultado, "a");
    //Si hay un error loguearlo
    if (fichero_resultado == NULL) {
        escribirEnLog(LOG_ERROR, "Error al escribir en fichero resultado %s\n", nombre_completo_fichero_resultado);
        return;
    }
    fprintf(fichero_resultado, "%s", mensaje);
    fclose(fichero_resultado);

    return;
}

// Función para eliminar el fichero resultado
void eliminarFicheroResultado(int patron) {
    const char *carpeta_datos;
    carpeta_datos = obtener_valor_configuracion("PATH_FILES", "../datos");
    const char *raiz_fichero_resultado;
    raiz_fichero_resultado = obtener_valor_configuracion("RESULTS_FILE", "resultado_patron_");
    char nombre_completo_fichero_resultado[PATH_MAX];
    sprintf(nombre_completo_fichero_resultado, "%s/%s%02d.csv", carpeta_datos, raiz_fichero_resultado, patron);
    remove(nombre_completo_fichero_resultado);
    return;
}

// Detección de patrón de fraude de tipo 1
// Más de 5 transacciones por usuario en una hora
void *hilo_patron_fraude_1(void *arg) {
    int id_hilo = *((int *)arg);
    const char *carpeta_datos;
    char clave[100];
    char separador1[30] = "@";
    char separador2[30] = ":00";
    int cantidad;
    char mensaje[150];

    carpeta_datos = obtener_valor_configuracion("PATH_FILES", "../datos");
    const char *fichero_datos;
    fichero_datos = obtener_valor_configuracion("INVENTORY_FILE", "file.csv");
    char nombre_completo_fichero_datos[PATH_MAX];
    sprintf(nombre_completo_fichero_datos, "%s/%s", carpeta_datos, fichero_datos);
    
    escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_1", "Hilo %02d: procesando fichero %s \n", id_hilo, nombre_completo_fichero_datos);

    // Bucle infinito para observar la carpeta
    while (1) {
        // Esperar a que el hilo se active
        activarHiloPatronFraude(id_hilo, 1);
        
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_1", "Hilo %02d: se ha activado\n", id_hilo);
        // Obtener acceso exclusivo al fichero consolidado
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_1", "Hilo %02d: solicitando acceso al semáforo\n", id_hilo);
        sem_wait(semaforo_consolidar_ficheros_entrada);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_1", "Hilo %02d: comenzando comprobación patrón fraude 1 en fichero %s\n", id_hilo, nombre_completo_fichero_datos);

        //Implementación patrón de fraude tipo 1
        FILE *archivo_consolidado;
        archivo_consolidado = fopen(nombre_completo_fichero_datos, "r");
        if (archivo_consolidado == NULL) {

            // No se ha conseguido abrir el fichero
            escribirEnLog(LOG_ERROR, "hilo_patron_fraude", "Hilo %02d: error al abrir el archivo en patrón fraude 1 en fichero %s\n", id_hilo, nombre_completo_fichero_datos);

            // Simular retardo
             //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
            snprintf(mensaje, sizeof(mensaje), "Monitor: hilo_patron_fraude_1: Hilo %02d: ", id_hilo);
            simulaRetardo(mensaje);
            //Liberar semáforo y continuar
            sem_post(semaforo_consolidar_ficheros_entrada);
            continue;
        }

        GHashTable *usuariosPF1 = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_registroPatronF1);

        char line[MAX_LINE_LENGTH];
        while (fgets(line, sizeof(line), archivo_consolidado)) {
            // Formato: SUC001;OPE0001;12/03/2024 09:47:00;12/03/2024 10:14:00;USER144;COMPRA01;1;73 €;Finalizado
            char *sucursal = strtok(line, ";");
            char *operacion = strtok(NULL, ";");
            char *fechaHora1 = strtok(NULL, ";");
            char *fechaHora2 = strtok(NULL, ";");
            char *usuario = strtok(NULL, ";");
            char *tipoOperacion1 = strtok(NULL, ";");
            char *tipoOperacion2 = strtok(NULL, ";");
            char *importeTexto = strtok(NULL, ";");
            char *estado = strtok(NULL, ";");
            escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_1", "Hilo %02d: datos de registro %s;%s;%s;%s;%s;%s;%s;%s;%s;\n", id_hilo, sucursal, operacion, fechaHora1, fechaHora2, usuario, tipoOperacion1, tipoOperacion2, importeTexto, estado);

            // Para este patrón la clave del diccionario va a ser usuario+fecha+hora comienzo
            if (usuario) {
                strncpy(clave, usuario, sizeof(clave)-1);
                strncat(clave, separador1, sizeof(clave)-1);
                strncat(clave, eliminarUltimosCaracteres(fechaHora1, 6), sizeof(clave)-1);
                strncat(clave, separador2, sizeof(clave)-1);
                cantidad = 1;
                RegistroPatron *registro = g_hash_table_lookup(usuariosPF1, clave);
                if (registro == NULL) {
                    registro = g_new(RegistroPatron, 1);
                    registro->clave = g_strdup(clave);
                    registro->cantidad = cantidad;
                    g_hash_table_insert(usuariosPF1, registro->clave, registro);
                } else {
                    registro->cantidad += cantidad;
                }
            }
        }

        fclose(archivo_consolidado);

        // Imprimir resultados del diccionario en el log
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_1", "Hilo %02d: Diccionario del patrón\n", id_hilo);
        GHashTableIter iter;
        gpointer clave, valor;
        g_hash_table_iter_init(&iter, usuariosPF1);
        while (g_hash_table_iter_next(&iter, &clave, &valor)) {
            RegistroPatron *registro = (RegistroPatron *)valor;
            escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_1", "Hilo %02d: Diccionario del patrón Clave: %s, Número Registros: %d\n", id_hilo, registro->clave, registro->cantidad);
        }
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_1", "Hilo %02d: Terminado diccionario del patrón\n", id_hilo);

        // Eliminar fichero resultado
        eliminarFicheroResultado(id_hilo);
        
        // Revisar resultados que cumplen el patrón
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_1", "Hilo %02d: Registros que cumplen el patrón\n", id_hilo);
        g_hash_table_iter_init(&iter, usuariosPF1);
        while (g_hash_table_iter_next(&iter, &clave, &valor)) {
            RegistroPatron *registro = (RegistroPatron *)valor;
            // Más de 5 movimientos en una hora
            if (registro->cantidad > 5) {
                escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_1", "Hilo %02d: Registro que cumple el patrón Clave: %s, Registros en la Misma Hora: %d\n", id_hilo, registro->clave, registro->cantidad);
                // Componer el mensaje para el log y monitor
                snprintf(mensaje, sizeof(mensaje), "%02d:::Registro fraude patrón 1:::Clave=%s:::Registros en la Misma Hora=%d\n", id_hilo, registro->clave, registro->cantidad);
                escribirEnLog(LOG_GENERAL, "Monitor: hilo_patron_fraude_1", mensaje);
                // Escribir en fichero resultado patron 1
                escribirResultadoPatron(id_hilo, mensaje);
            }
        }
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_1", "Hilo %02d: Terminados registros que cumplen el patrón\n", id_hilo);

        // Liberar memoria
        g_hash_table_destroy(usuariosPF1);

        // Una vez terminado, dejamos el estado del hilo en bloqueado, así nos aseguramos de que no se vuelva a ejecutar hasta
        // que llegue un aviso a través del pipe

        //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
        snprintf(mensaje, sizeof(mensaje), "Monitor: hilo_patron_fraude_1: Hilo %02d: ", id_hilo);
        simulaRetardo(mensaje);

        // Liberar acceso exclusivo al fichero consolidado
        sem_post(semaforo_consolidar_ficheros_entrada);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_1", "Hilo %02d: liberado semáforo.\n", id_hilo);

    }

    return NULL;
}

// Detección de patrón de fraude de tipo 2
// Un usuario realiza más de 3 retiros a la vez
// Entendemos que quiere decir que el usuario realiza tres retiros en la misma hora:minuto:segundo
void *hilo_patron_fraude_2(void *arg) {
    int id_hilo = *((int *)arg);
    const char *carpeta_datos;
    char clave[100];
    char separador1[30] = "@";
    int cantidad;
    char mensaje[150];

    carpeta_datos = obtener_valor_configuracion("PATH_FILES", "../datos");
    const char *fichero_datos;
    fichero_datos = obtener_valor_configuracion("INVENTORY_FILE", "file.csv");
    char nombre_completo_fichero_datos[PATH_MAX];
    sprintf(nombre_completo_fichero_datos, "%s/%s", carpeta_datos, fichero_datos);

    escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_2", "Hilo %02d: observando carpeta %s \n", id_hilo, carpeta_datos);

    // Bucle infinito para observar la carpeta
    while (1) {
        // Esperar a que el hilo se active
        activarHiloPatronFraude(id_hilo, 1);

        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_2", "Hilo %02d: se ha activado\n", id_hilo);
        // Obtener acceso exclusivo al fichero consolidado
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_2", "Hilo %02d: esperando semáforo...\n", id_hilo);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_2", "Hilo %02d: solicitando acceso al semáforo\n", id_hilo);
        sem_wait(semaforo_consolidar_ficheros_entrada);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_2", "Hilo %02d: comenzando comprobación patrón fraude 2\n", id_hilo);

        // Implentación de patrón_fraude_2
        FILE *archivo_consolidado;
        archivo_consolidado = fopen(nombre_completo_fichero_datos, "r");
        if (archivo_consolidado == NULL) {

            // No se ha conseguido abrir el fichero
            escribirEnLog(LOG_ERROR, "hilo_patron_fraude", "Hilo %02d: error al abrir el archivo en patrón fraude 1 en fichero %s\n", id_hilo, nombre_completo_fichero_datos);

            // Simular retardo
             //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
            char mensaje[100];
            snprintf(mensaje, sizeof(mensaje), "Monitor: hilo_patron_fraude_2: Hilo %02d: ", id_hilo);
            simulaRetardo(mensaje);
            //Liberar semáforo y continuar
            sem_post(semaforo_consolidar_ficheros_entrada);
            continue;
        }

        GHashTable *usuariosPF1 = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_registroPatronF1);
        char line[MAX_LINE_LENGTH];
        int importe;
        while (fgets(line, sizeof(line), archivo_consolidado)) {
            // Formato: SUC001;OPE0001;12/03/2024 09:47:00;12/03/2024 10:14:00;USER144;COMPRA01;1;73 €;Finalizado
            char *sucursal = strtok(line, ";");
            char *operacion = strtok(NULL, ";");
            char *fechaHora1 = strtok(NULL, ";");
            char *fechaHora2 = strtok(NULL, ";");
            char *usuario = strtok(NULL, ";");
            char *tipoOperacion1 = strtok(NULL, ";");
            char *tipoOperacion2 = strtok(NULL, ";");
            char *importeTexto = strtok(NULL, ";");
            char *estado = strtok(NULL, ";");
            escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_2", "Hilo %02d: datos de registro %s;%s;%s;%s;%s;%s;%s;%s;%s;\n", id_hilo, sucursal, operacion, fechaHora1, fechaHora2, usuario, tipoOperacion1, tipoOperacion2, importeTexto, estado);

            // Para este patrón la clave del diccionario va a ser usuario y fecha-hora completa
            // Comprobar que es un movimiento de retirar dinero
            importe = atoi(importeTexto);
            if (usuario && (importe < 0)) {
                strncpy(clave, usuario, sizeof(clave)-1);
                strncat(clave, separador1, sizeof(clave)-1);
                strncat(clave, fechaHora1, sizeof(clave)-1);
                cantidad = 1;
                RegistroPatron *registro = g_hash_table_lookup(usuariosPF1, clave);
                if (registro == NULL) {
                    registro = g_new(RegistroPatron, 1);
                    registro->clave = g_strdup(clave);
                    registro->cantidad = cantidad;
                    g_hash_table_insert(usuariosPF1, registro->clave, registro);
                } else {
                    registro->cantidad += cantidad;
                }
            }
        }

        fclose(archivo_consolidado);

        // Imprimir resultados del diccionario en el log
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_2", "Hilo %02d: Diccionario del patrón\n", id_hilo);
        GHashTableIter iter;
        gpointer clave, valor;
        g_hash_table_iter_init(&iter, usuariosPF1);
        while (g_hash_table_iter_next(&iter, &clave, &valor)) {
            RegistroPatron *registro = (RegistroPatron *)valor;
            escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_2", "Hilo %02d: Diccionario del patrón Clave: %s, Número Registros: %d\n", id_hilo, registro->clave, registro->cantidad);
        }
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_2", "Hilo %02d: Terminado diccionario del patrón\n", id_hilo);

        // Eliminar fichero resultado
        eliminarFicheroResultado(id_hilo);

        // Revisar resultados que cumplen el patrón
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_2", "Hilo %02d: Registros que cumplen el patrón\n", id_hilo);
        g_hash_table_iter_init(&iter, usuariosPF1);
        while (g_hash_table_iter_next(&iter, &clave, &valor)) {
            RegistroPatron *registro = (RegistroPatron *)valor;
            // Más de 3 retiros a la vez
            if (registro->cantidad > 3) {
                escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_2", "Hilo %02d: Registro que cumple el patrón Clave: %s, Registros a la vez: %d\n", id_hilo, registro->clave, registro->cantidad);
                // Componer el mensaje para el log y monitor
                snprintf(mensaje, sizeof(mensaje), "%02d:::Registro fraude patrón 2:::Clave=%s:::Registros a la vez=%d\n", id_hilo, registro->clave, registro->cantidad);
                escribirEnLog(LOG_GENERAL, "Monitor: hilo_patron_fraude_2", mensaje);

                // Escribir en fichero resultado patron
                escribirResultadoPatron(id_hilo, mensaje);
            }
        }
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_2", "Hilo %02d: Terminados registros que cumplen el patrón\n", id_hilo);

        // Liberar memoria
        g_hash_table_destroy(usuariosPF1);

        // Una vez terminado, dejamos el estado del hilo en bloqueado, así nos aseguramos de que no se vuelva a ejecutar hasta
        // que llegue un aviso a través del pipe

        //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
        snprintf(mensaje, sizeof(mensaje), "Monitor: hilo_patron_fraude_2: Hilo %02d: ", id_hilo);
        simulaRetardo(mensaje);

        // Liberar acceso exclusivo al fichero consolidado
        sem_post(semaforo_consolidar_ficheros_entrada);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_2", "Hilo %02d: liberado semáforo.\n", id_hilo);
    }

    return NULL;
}

// Detección de patrón de fraude de tipo 3
// Un usuario comete más de 3 errores durante 1 día
void *hilo_patron_fraude_3(void *arg) {
    int id_hilo = *((int *)arg);
    const char *carpeta_datos;
    char clave[100];
    char separador1[30] = "@";
    int cantidad;
    char mensaje[150];

    carpeta_datos = obtener_valor_configuracion("PATH_FILES", "../datos");
    const char *fichero_datos;
    fichero_datos = obtener_valor_configuracion("INVENTORY_FILE", "file.csv");
    char nombre_completo_fichero_datos[PATH_MAX];
    sprintf(nombre_completo_fichero_datos, "%s/%s", carpeta_datos, fichero_datos);

    escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_3", "Hilo %02d: observando carpeta %s \n", id_hilo, carpeta_datos);

    // Bucle infinito para observar la carpeta
    while (1) {
        // Esperar a que el hilo se active
        activarHiloPatronFraude(id_hilo, 1);

        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_3", "Hilo %02d: se ha activado\n", id_hilo);
        // Obtener acceso exclusivo al fichero consolidado
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_3", "Hilo %02d: esperando semáforo...\n", id_hilo);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_3", "Hilo %02d: solicitando acceso al semáforo\n", id_hilo);
        sem_wait(semaforo_consolidar_ficheros_entrada);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_3", "Hilo %02d: comenzando comprobación patrón fraude 3\n", id_hilo);

        // Implementación patrón fraude tipo 3
        FILE *archivo_consolidado;
        archivo_consolidado = fopen(nombre_completo_fichero_datos, "r");
        if (archivo_consolidado == NULL) {

            // No se ha conseguido abrir el fichero
            escribirEnLog(LOG_ERROR, "hilo_patron_fraude", "Hilo %02d: error al abrir el archivo en patrón fraude 1 en fichero %s\n", id_hilo, nombre_completo_fichero_datos);

            // Simular retardo
             //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
            char mensaje[100];
            snprintf(mensaje, sizeof(mensaje), "Monitor: hilo_patron_fraude_3: Hilo %02d: ", id_hilo);
            simulaRetardo(mensaje);
            //Liberar semáforo y continuar
            sem_post(semaforo_consolidar_ficheros_entrada);
            continue;
        }

        GHashTable *usuariosPF1 = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_registroPatronF1);
        char line[MAX_LINE_LENGTH];
        while (fgets(line, sizeof(line), archivo_consolidado)) {
            // Formato: SUC001;OPE0001;12/03/2024 09:47:00;12/03/2024 10:14:00;USER144;COMPRA01;1;73 €;Finalizado
            char *sucursal = strtok(line, ";");
            char *operacion = strtok(NULL, ";");
            char *fechaHora1 = strtok(NULL, ";");
            char *fechaHora2 = strtok(NULL, ";");
            char *usuario = strtok(NULL, ";");
            char *tipoOperacion1 = strtok(NULL, ";");
            char *tipoOperacion2 = strtok(NULL, ";");
            char *importeTexto = strtok(NULL, ";");
            char *estado = strtok(NULL, ";");
            escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_3", "Hilo %02d: datos de registro %s;%s;%s;%s;%s;%s;%s;%s;%s;\n", id_hilo, sucursal, operacion, fechaHora1, fechaHora2, usuario, tipoOperacion1, tipoOperacion2, importeTexto, estado);

            // Para este patrón la clave del diccionario va a ser usuario y el día del mes
            // Comprobar que es un movimiento de error
            if (usuario && (strcmp(estado, "Error"))) {
                strncpy(clave, usuario, sizeof(clave)-1);
                strncat(clave, separador1, sizeof(clave)-1);
                strncat(clave, eliminarUltimosCaracteres(fechaHora1, 9), sizeof(clave)-1);
                cantidad = 1;
                RegistroPatron *registro = g_hash_table_lookup(usuariosPF1, clave);
                if (registro == NULL) {
                    registro = g_new(RegistroPatron, 1);
                    registro->clave = g_strdup(clave);
                    registro->cantidad = cantidad;
                    g_hash_table_insert(usuariosPF1, registro->clave, registro);
                } else {
                    registro->cantidad += cantidad;
                }
            }
        }

        fclose(archivo_consolidado);

        // Imprimir resultados del diccionario en el log
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_3", "Hilo %02d: Diccionario del patrón\n", id_hilo);
        GHashTableIter iter;
        gpointer clave, valor;
        g_hash_table_iter_init(&iter, usuariosPF1);
        while (g_hash_table_iter_next(&iter, &clave, &valor)) {
            RegistroPatron *registro = (RegistroPatron *)valor;
            escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_3", "Hilo %02d: Diccionario del patrón Clave: %s, Número Registros: %d\n", id_hilo, registro->clave, registro->cantidad);
        }
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_3", "Hilo %02d: Terminado diccionario del patrón\n", id_hilo);

        // Eliminar fichero resultado
        eliminarFicheroResultado(id_hilo);

        // Revisar resultados que cumplen el patrón
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_3", "Hilo %02d: Registros que cumplen el patrón\n", id_hilo);
        g_hash_table_iter_init(&iter, usuariosPF1);
        while (g_hash_table_iter_next(&iter, &clave, &valor)) {
            RegistroPatron *registro = (RegistroPatron *)valor;
            // Más de tres errores en un día
            if (registro->cantidad > 3) {
                escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_3", "Hilo %02d: Registro que cumple el patrón Clave: %s, Registros con Error: %d\n", id_hilo, registro->clave, registro->cantidad);
                // Componer el mensaje para el log y monitor
                snprintf(mensaje, sizeof(mensaje), "%02d:::Registro fraude patrón 3:::Clave=%s:::Registros con Error=%d\n", id_hilo, registro->clave, registro->cantidad);
                escribirEnLog(LOG_GENERAL, "Monitor: hilo_patron_fraude_3", mensaje);

                // Escribir en fichero resultado patron
                escribirResultadoPatron(id_hilo, mensaje);
            }
        }
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_3", "Hilo %02d: Terminados registros que cumplen el patrón\n", id_hilo);

        // Liberar memoria
        g_hash_table_destroy(usuariosPF1);

        // Una vez terminado, dejamos el estado del hilo en bloqueado, así nos aseguramos de que no se vuelva a ejecutar hasta
        // que llegue un aviso a través del pipe

        //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
        snprintf(mensaje, sizeof(mensaje), "Monitor: hilo_patron_fraude_3: Hilo %02d: ", id_hilo);
        simulaRetardo(mensaje);

        // Liberar acceso exclusivo al fichero consolidado
        sem_post(semaforo_consolidar_ficheros_entrada);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_3", "Hilo %02d: liberado semáforo.\n", id_hilo);
    }
    
    return NULL;
}

// Detección de patrón de fraude de tipo 4
// Un usuario realiza una operación por cada tipo de operaciones durante el mismo día
// Suponemos que este patrón de fraude se da cuando en el mismo día hay 1 registro de cada
// uno de estos tipos de operaciones: 1, 2, 3, 4
void *hilo_patron_fraude_4(void *arg) {
    int id_hilo = *((int *)arg);
    const char *carpeta_datos;
    char clave[100];
    char separador1[30] = "@";
    int operacion1, operacion2, operacion3, operacion4;
    char mensaje[150];

    carpeta_datos = obtener_valor_configuracion("PATH_FILES", "../datos");
    const char *fichero_datos;
    fichero_datos = obtener_valor_configuracion("INVENTORY_FILE", "file.csv");
    char nombre_completo_fichero_datos[PATH_MAX];
    sprintf(nombre_completo_fichero_datos, "%s/%s", carpeta_datos, fichero_datos);

    escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_4", "Hilo %02d: observando carpeta %s \n", id_hilo, carpeta_datos);

    // Bucle infinito para observar la carpeta
    while (1) {
        // Esperar a que el hilo se active
        activarHiloPatronFraude(id_hilo, 1);

        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_4", "Hilo %02d: se ha activado\n", id_hilo);
        // Obtener acceso exclusivo al fichero consolidado
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_4", "Hilo %02d: esperando semáforo...\n", id_hilo);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_4", "Hilo %02d: solicitando acceso al semáforo\n", id_hilo);
        sem_wait(semaforo_consolidar_ficheros_entrada);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_4", "Hilo %02d: comenzando comprobación patrón fraude 3\n", id_hilo);

        // Implementación patrón fraude tipo 3
        FILE *archivo_consolidado;
        archivo_consolidado = fopen(nombre_completo_fichero_datos, "r");
        if (archivo_consolidado == NULL) {

            // No se ha conseguido abrir el fichero
            escribirEnLog(LOG_ERROR, "hilo_patron_fraude", "Hilo %02d: error al abrir el archivo en patrón fraude 1 en fichero %s\n", id_hilo, nombre_completo_fichero_datos);

            // Simular retardo
             //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
            char mensaje[100];
            snprintf(mensaje, sizeof(mensaje), "Monitor: hilo_patron_fraude_4: Hilo %02d: ", id_hilo);
            simulaRetardo(mensaje);
            //Liberar semáforo y continuar
            sem_post(semaforo_consolidar_ficheros_entrada);
            continue;
        }

        GHashTable *usuariosPF1 = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_registroPatronF1);
        char line[MAX_LINE_LENGTH];
        while (fgets(line, sizeof(line), archivo_consolidado)) {
            // Formato: SUC001;OPE0001;12/03/2024 09:47:00;12/03/2024 10:14:00;USER144;COMPRA01;1;73 €;Finalizado
            char *sucursal = strtok(line, ";");
            char *operacion = strtok(NULL, ";");
            char *fechaHora1 = strtok(NULL, ";");
            char *fechaHora2 = strtok(NULL, ";");
            char *usuario = strtok(NULL, ";");
            char *tipoOperacion1 = strtok(NULL, ";");
            char *tipoOperacion2 = strtok(NULL, ";");
            int numTipoOperacion2 = atoi(tipoOperacion2);
            char *importeTexto = strtok(NULL, ";");
            char *estado = strtok(NULL, ";");
            escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_4", "Hilo %02d: datos de registro %s;%s;%s;%s;%s;%s;%s;%s;%s;\n", id_hilo, sucursal, operacion, fechaHora1, fechaHora2, usuario, tipoOperacion1, tipoOperacion2, importeTexto, estado);
            
            //Comprobar el tipo de operación

            if (numTipoOperacion2 == 1) {operacion1 = 1;} else {operacion1 = 0;}
            if (numTipoOperacion2 == 2) {operacion2 = 1;} else {operacion2 = 0;}
            if (numTipoOperacion2 == 3) {operacion3 = 1;} else {operacion3 = 0;}
            if (numTipoOperacion2 == 4) {operacion4 = 1;} else {operacion4 = 0;}
            
            // Para este patrón la clave del diccionario va a ser usuario y el día del mes
            // Tenemos que ir acumulando los tipos de operación
            if (usuario && (strcmp(estado, "Error"))) {
                strncpy(clave, usuario, sizeof(clave)-1);
                strncat(clave, separador1, sizeof(clave)-1);
                strncat(clave, eliminarUltimosCaracteres(fechaHora1, 9), sizeof(clave)-1);
                
                RegistroPatron *registro = g_hash_table_lookup(usuariosPF1, clave);
                if (registro == NULL) {
                    registro = g_new(RegistroPatron, 1);
                    registro->clave = g_strdup(clave);
                    registro->cantidad = 0;
                    registro->operacion1Presente = operacion1;
                    registro->operacion2Presente = operacion2;
                    registro->operacion3Presente = operacion3;
                    registro->operacion4Presente = operacion4;
                    g_hash_table_insert(usuariosPF1, registro->clave, registro);
                } else {
                    registro->operacion1Presente = registro->operacion1Presente + operacion1;
                    registro->operacion2Presente = registro->operacion2Presente + operacion2;
                    registro->operacion3Presente = registro->operacion3Presente + operacion3;
                    registro->operacion4Presente = registro->operacion4Presente + operacion4;
                }
            }
        }

        fclose(archivo_consolidado);

        // Imprimir resultados del diccionario en el log
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_4", "Hilo %02d: Diccionario del patrón\n", id_hilo);
        GHashTableIter iter;
        gpointer clave, valor;
        g_hash_table_iter_init(&iter, usuariosPF1);
        while (g_hash_table_iter_next(&iter, &clave, &valor)) {
            RegistroPatron *registro = (RegistroPatron *)valor;
            escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_4", "Hilo %02d: Diccionario del patrón Clave: %s, Número Registros: %d\n", id_hilo, registro->clave, registro->cantidad);
        }
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_4", "Hilo %02d: Terminado diccionario del patrón\n", id_hilo);

        // Eliminar fichero resultado
        eliminarFicheroResultado(id_hilo);

        // Revisar resultados que cumplen el patrón
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_4", "Hilo %02d: Registros que cumplen el patrón\n", id_hilo);
        g_hash_table_iter_init(&iter, usuariosPF1);
        while (g_hash_table_iter_next(&iter, &clave, &valor)) {
            RegistroPatron *registro = (RegistroPatron *)valor;
            // Todos los tipos de operación presentes
            if (registro->operacion1Presente > 0 && registro->operacion2Presente > 0 && registro->operacion3Presente > 0 && registro->operacion4Presente > 0 ) {
                escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_4", "Hilo %02d: Registro que cumple el patrón Clave: %s, Registros con Todos los Tipos de Operaciones\n", id_hilo, registro->clave);
                // Componer el mensaje para el log y monitor
                snprintf(mensaje, sizeof(mensaje), "%02d:::Registro fraude patrón 4:::Clave=%s:::Registros con Todos los Tipos de Operaciones\n", id_hilo, registro->clave);
                escribirEnLog(LOG_GENERAL, "Monitor: hilo_patron_fraude_4", mensaje);

                // Escribir en fichero resultado patron
                escribirResultadoPatron(id_hilo, mensaje);
            }
        }
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_4", "Hilo %02d: Terminados registros que cumplen el patrón\n", id_hilo);

        // Liberar memoria
        g_hash_table_destroy(usuariosPF1);

        // Una vez terminado, dejamos el estado del hilo en bloqueado, así nos aseguramos de que no se vuelva a ejecutar hasta
        // que llegue un aviso a través del pipe

        //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
        snprintf(mensaje, sizeof(mensaje), "Monitor: hilo_patron_fraude_4: Hilo %02d: ", id_hilo);
        simulaRetardo(mensaje);

        // Liberar acceso exclusivo al fichero consolidado
        sem_post(semaforo_consolidar_ficheros_entrada);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_4", "Hilo %02d: liberado semáforo.\n", id_hilo);
    }
    
    return NULL;
}

// Detección de patrón de fraude de tipo 5
// La cantidad de dinero retirado (-) es mayor que la cantidad de dinero ingresado (+) por un usuario en 1 día
void *hilo_patron_fraude_5(void *arg) {
    int id_hilo = *((int *)arg);
    const char *carpeta_datos;
    char clave[100];
    char separador1[30] = "@";
    int cantidad;
    int importe;
    char mensaje[150];

    carpeta_datos = obtener_valor_configuracion("PATH_FILES", "../datos");
    const char *fichero_datos;
    fichero_datos = obtener_valor_configuracion("INVENTORY_FILE", "file.csv");
    char nombre_completo_fichero_datos[PATH_MAX];
    sprintf(nombre_completo_fichero_datos, "%s/%s", carpeta_datos, fichero_datos);
    
    escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_5", "Hilo %02d: observando carpeta %s \n", id_hilo, carpeta_datos);

    // Bucle infinito para observar la carpeta
    while (1) {
        // Esperar a que el hilo se active
        activarHiloPatronFraude(id_hilo, 1);

        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_5", "Hilo %02d: se ha activado\n", id_hilo);
        // Obtener acceso exclusivo al fichero consolidado
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_5", "Hilo %02d: esperando semáforo...\n", id_hilo);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_5", "Hilo %02d: solicitando acceso al semáforo\n", id_hilo);
        sem_wait(semaforo_consolidar_ficheros_entrada);
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_5", "Hilo %02d: comenzando comprobación patrón fraude 5\n", id_hilo);

        // Implentación de patrón_fraude_5
        FILE *archivo_consolidado;
        archivo_consolidado = fopen(nombre_completo_fichero_datos, "r");
        if (archivo_consolidado == NULL) {

            // No se ha conseguido abrir el fichero
            escribirEnLog(LOG_ERROR, "hilo_patron_fraude", "Hilo %02d: error al abrir el archivo en patrón fraude 1 en fichero %s\n", id_hilo, nombre_completo_fichero_datos);

            // Simular retardo
             //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
            char mensaje[100];
            snprintf(mensaje, sizeof(mensaje), "Monitor: hilo_patron_fraude_5: Hilo %02d: ", id_hilo);
            simulaRetardo(mensaje);
            //Liberar semáforo y continuar
            sem_post(semaforo_consolidar_ficheros_entrada);
            continue;
        }

        GHashTable *usuariosPF1 = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_registroPatronF1);
        char line[MAX_LINE_LENGTH];
        while (fgets(line, sizeof(line), archivo_consolidado)) {
            // Formato: SUC001;OPE0001;12/03/2024 09:47:00;12/03/2024 10:14:00;USER144;COMPRA01;1;73 €;Finalizado
            char *sucursal = strtok(line, ";");
            char *operacion = strtok(NULL, ";");
            char *fechaHora1 = strtok(NULL, ";");
            char *fechaHora2 = strtok(NULL, ";");
            char *usuario = strtok(NULL, ";");
            char *tipoOperacion1 = strtok(NULL, ";");
            char *tipoOperacion2 = strtok(NULL, ";");
            char *importeTexto = strtok(NULL, ";");
            char *estado = strtok(NULL, ";");
            escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_5", "Hilo %02d: datos de registro %s;%s;%s;%s;%s;%s;%s;%s;%s;\n", id_hilo, sucursal, operacion, fechaHora1, fechaHora2, usuario, tipoOperacion1, tipoOperacion2, importeTexto, estado);

            // Para este patrón la clave del diccionario va a ser usuario y fecha completa
            importe = atoi(importeTexto);
            if (usuario) {
                strncpy(clave, usuario, sizeof(clave)-1);
                strncat(clave, separador1, sizeof(clave)-1);
                strncat(clave, eliminarUltimosCaracteres(fechaHora1, 9), sizeof(clave)-1);;
                cantidad = importe;
                RegistroPatron *registro = g_hash_table_lookup(usuariosPF1, clave);
                if (registro == NULL) {
                    registro = g_new(RegistroPatron, 1);
                    registro->clave = g_strdup(clave);
                    registro->cantidad = cantidad;
                    g_hash_table_insert(usuariosPF1, registro->clave, registro);
                } else {
                    registro->cantidad += cantidad;
                }
            }
        }

        fclose(archivo_consolidado);

        // Imprimir resultados del diccionario en el log
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_5", "Hilo %02d: Diccionario del patrón\n", id_hilo);
        GHashTableIter iter;
        gpointer clave, valor;
        g_hash_table_iter_init(&iter, usuariosPF1);
        while (g_hash_table_iter_next(&iter, &clave, &valor)) {
            RegistroPatron *registro = (RegistroPatron *)valor;
            escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_5", "Hilo %02d: Diccionario del patrón Clave: %s, Número Registros: %d\n", id_hilo, registro->clave, registro->cantidad);
        }
        escribirEnLog(LOG_DEBUG, "Monitor: hilo_patron_fraude_5", "Hilo %02d: Terminado diccionario del patrón\n", id_hilo);

        // Eliminar fichero resultado
        eliminarFicheroResultado(id_hilo);

        // Revisar resultados que cumplen el patrón
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_5", "Hilo %02d: Registros que cumplen el patrón\n", id_hilo);
        g_hash_table_iter_init(&iter, usuariosPF1);
        while (g_hash_table_iter_next(&iter, &clave, &valor)) {
            RegistroPatron *registro = (RegistroPatron *)valor;
            // Suma de dinero ingresado y retirado es negativa
            if (registro->cantidad < 0) {
                escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_5", "Hilo %02d: Registro que cumple el patrón Clave: %s, Número Registros: %d\n", id_hilo, registro->clave, registro->cantidad);
                // Componer el mensaje para el log y monitor
                snprintf(mensaje, sizeof(mensaje), "%02d:::Registro fraude patrón 5:::Clave=%s:::Saldo negativo=%d\n", id_hilo, registro->clave, registro->cantidad);
                escribirEnLog(LOG_GENERAL, "Monitor: hilo_patron_fraude_5", mensaje);
                
                // Escribir en fichero resultado patron
                escribirResultadoPatron(id_hilo, mensaje);
            }
        }
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_5", "Hilo %02d: Terminados registros que cumplen el patrón\n", id_hilo);

        // Liberar memoria
        g_hash_table_destroy(usuariosPF1);

        // Una vez terminado, dejamos el estado del hilo en bloqueado, así nos aseguramos de que no se vuelva a ejecutar hasta
        // que llegue un aviso a través del pipe

        //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
        snprintf(mensaje, sizeof(mensaje), "Monitor: hilo_patron_fraude_5: Hilo %02d: ", id_hilo);
        simulaRetardo(mensaje);

        // Liberar acceso exclusivo al fichero consolidado
        sem_post(semaforo_consolidar_ficheros_entrada);
        escribirEnLog(LOG_INFO, "Monitor: hilo_patron_fraude_5", "Hilo %02d: liberado semáforo.\n", id_hilo);
    }

    return NULL;
}



// Función que crea los hilos de detección de los patrones de fraude
int crear_hilos_patrones_fraude() {
    // Obtener el número de hilos a crear
    int num_hilos; 
    num_hilos = NUM_PATRONES_FRAUDE; // 1 hilo por cada patrón de fraude
    escribirEnLog(LOG_INFO, "Monitor: crea_hilos_patrones_fraude", "Necesario crear %02d hilos de patrones de fraude\n", num_hilos);

    // Dimensionar pool de hilos observadores
    pthread_t tid[num_hilos];
    int id[num_hilos];

    // Crear los hilos de detección de patrones de fraude
    // Esta variable servirá para apuntar a la función de implementación del hilo
    void *(*ptr_hilo_patron_fraude)(void *);
    for (int i = 0; i < num_hilos; i++) {
        id[i] = i + 1; //id[i] tiene el número de hilo

        // Ponemos el procesado de este hilo a 1: de momento está bloqueado el mutex
        activarHiloPatronFraude(id[i], 1);

        // Identificar la función para la creación del hilo según sea el valor de i
        if (id[i]==1) {
            ptr_hilo_patron_fraude = &hilo_patron_fraude_1;
        } else if (id[i]==2) {
            ptr_hilo_patron_fraude = &hilo_patron_fraude_2;
        } else if (id[i]==3) {
            ptr_hilo_patron_fraude = &hilo_patron_fraude_3;
        } else if (id[i]==4) {
            ptr_hilo_patron_fraude = &hilo_patron_fraude_4;
        } else if (id[i]==5) {
            ptr_hilo_patron_fraude = &hilo_patron_fraude_5;
        }    

        // Crear el hilo que apunta a la función identificada anteriormente
        escribirEnLog(LOG_INFO, "Monitor: crea_hilos_patrones_fraude", "Creado hilo de detección de patrón de fraude %02d\n", id[i]);
        if (pthread_create(&tid[i], NULL, ptr_hilo_patron_fraude, (void *)&id[i]) != 0) {
            escribirEnLog(LOG_ERROR, "Monitor: crea_hilos_patrones_fraude", "Error al crear el hilo de de detección de patrón de fraude %02d\n", id[i]);
            exit(EXIT_FAILURE);
        }
    }

    // Desanclar los hilos para que se ejecuten de forma independiente
    for (int i = 0;i < num_hilos; i++) {
        if (pthread_detach(tid[i]) != 0) {
            escribirEnLog(LOG_ERROR, "Monitor: crea_hilos_patrones_fraude", "Error al desanclar el hilo de observación detección de patrón de fraude 1");
            exit(EXIT_FAILURE);
        }
    }

    escribirEnLog(LOG_INFO, "Monitor: crea_hilos_patrones_fraude", "hilos de detección de patrones de fraude creados\n");
    return 0;
}

#pragma endregion DeteccionPatronesFraude

// ------------------------------------------------------------------
// MAIN: INICIALIZACIÓN Y CREACIÓN DEL DEMONIO MONITOR
// ------------------------------------------------------------------
#pragma region Main

// Función de manejador de señal CTRL-C
void ctrlc_handler(int sig) {
    printf("Monitor: Se ha presionado CTRL-C. Terminando la ejecución.\n");
    escribirEnLog(LOG_INFO, "file_processor: ctrlc_handler", "Se ha pulsado CTRL-C\n");

    // Acciones que hay que realizar al terminar el programa
    // Cerrar el semáforo
    sem_close(semaforo_consolidar_ficheros_entrada);
    // Borrar el semáforo
    sem_unlink(semName);

    escribirEnLog(LOG_INFO, "Monitor: ctrlc_handler", "Semáforo semaforo_consolidar_ficheros_entrada cerrado\n");
    close(pipefd);
    escribirEnLog(LOG_INFO, "Monitor: ctrlc_handler", "pipe cerrado\n");
    escribirEnLog(LOG_INFO, "Monitor: ctrlc_handler", "Proceso terminado\n");

    // Fin del programa
    exit(EXIT_SUCCESS);
}

// Función main que se activa al llamar desde línea de comandos
int main(int argc, char *argv[]) {
    // Parámetros: argc es el contador de parámetros y argv es el valor de estos parámetros

    escribirEnLog(LOG_GENERAL, "Monitor: main", "Iniciando ejecución Monitor\n");

    // Vamos a crear un semáforo con un nombre común para file procesor y monitor de forma que podamos utilizarlo
    // para asegurar el acceso a recursos comunes desde ambos procesos. 
    // Creamos un semáforo de 1 recursos con nombre definido en SEMAPHORE_NAME y permisos de lectura y escritura
    semName = obtener_valor_configuracion("SEMAPHORE_NAME", "/semaforo");

    semaforo_consolidar_ficheros_entrada = sem_open(semName, O_CREAT , 0644, 1);
    if (semaforo_consolidar_ficheros_entrada == SEM_FAILED){
        escribirEnLog(LOG_ERROR, "Monitor: main", "Error al abrir el semáforo %s\n", semName);
        exit(EXIT_FAILURE);
    }
    escribirEnLog(LOG_INFO, "Monitor: main", "Semáforo %s creado\n", semName);
    escribirEnLog(LOG_INFO, "Monitor:main", "Semáforo semaforo_consolidar_ficheros_entrada creado\n");

    // Registra el manejador de señal para SIGINT para CTRL-C
    if (signal(SIGINT, ctrlc_handler) == SIG_ERR) {
        escribirEnLog(LOG_ERROR, "Monitor: main", "No se pudo capturar SIGINT\n");
        return EXIT_FAILURE;
    }
    
    // Crear el named pipe
    const char * pipeName;
    pipeName = obtener_valor_configuracion("PIPE_NAME", "/tmp/pipeAudita");
    escribirEnLog(LOG_INFO, "Monitor: main", "Creando pipe %s\n", pipeName);
    mkfifo(pipeName, 0666);

    // Variables de lectura del pipe
    char buffer[MESSAGE_SIZE];
    int bytes_read;

    //Creación de los hilos de observación de ficheros de las sucursales
    crear_hilos_patrones_fraude();

    // Abrir el pipe
    escribirEnLog(LOG_INFO, "Monitor: main", "Abriendo pipe %s\n", pipeName);
    pipefd = open(pipeName, O_RDONLY);
    escribirEnLog(LOG_INFO, "Monitor: main", "Entrando en ejecucion indefinida\n");
    

    while (1) {
        bytes_read = read(pipefd, buffer, sizeof(buffer));
        if (bytes_read > 0) {
            // Recibido mensaje en el pipe
            escribirEnLog(LOG_INFO, "Monitor: main", "Recibido %s\n", buffer);    
            escribirEnLog(LOG_GENERAL, "Monitor: main", "%s\n", buffer);    

            // Desbloquear los hilos de detección de patrón de fraude
            for (int i = 1; i <= NUM_PATRONES_FRAUDE; i++) {
                activarHiloPatronFraude(i, 0);
            }

            //printf("%s\n", buffer);
            memset(buffer, 0, sizeof(buffer));
        }
        // Ponemos 1 centésima de segundo para evitar el consumo excesivo de la CPU
        sleep_centiseconds(1);
    }

    // Código inaccesible, el programa lo acabará le usuario con CTRL+C 
    // de forma que los semáforos y recursos se liberarán en el manejador
    return 0;

}
#pragma endregion Main
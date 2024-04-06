/**
FileProcessor.c

    Funcionalidad:
        Proceso de tipo demonio que, a partir de los valores del fichero de configuración,
        lee los ficheros CSV depositados por las sucursales bancarias en la carpeta de datos
        y consolida los resultados en el fichero de consolidación.

        Crea tantos hilos como sucursales bancarias se hayan configurado.

        Se comunica con el proceso Monitor utilizando named pipe, y se sincroniza con dicho proceso
        utilizando un semáforo común.

        Escribe datos de la operación en los ficheros de log.

    Compilación:
        gcc FileProcessor.c -o FileProcessor

    Ejecución:
        ./FileProcessor
        ./FileProcessor -g -s 3 -l 5
        ./FileProcessor --generar --sucursales 3 --lineas 5

    Parámetros:
        -g/--generar 
        -s/--sucursales <NUMERO SUCURSALES> 
        -l/--lineas <NUMERO LINEAS> 
        -h/--help
*/ 

// Nombre del fichero de configuración
#define FICHERO_CONFIGURACION "FileProcessor.conf"

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

#include "FileProcessor.h"  // Declaración de funciones de este módulo
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
        escribirEnLog(LOG_INFO, "hilo_patron_fraude_2", "Hilo %02d: Registro que cumple el patrón Clave: %s, Registros a la vez: %d\n", id_hilo, registro->clave, registro->cantidad);
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
// FUNCIÓN PARA ESCRITURA EN EL PIPE
// ------------------------------------------------------------------
#pragma region EscrituraPipe

// Mutex para seguridad de hilos
pthread_mutex_t mutex_escritura_pipe = PTHREAD_MUTEX_INITIALIZER;

// Tamaño de mensaje para el pipe de comunicación entre FileProcessor y Monitor
#define MESSAGE_SIZE 100

// Función de escritura en el pipe de un mensaje
int pipe_send(const char *message) {

    // Crear el named pipe
    // 0666: 
    //    El primer dígito (6) representa los permisos del propietario del archivo.
    //    El segundo dígito (6) representa los permisos del grupo al que pertenece el archivo.
    //    El tercer dígito (6) representa los permisos para otros usuarios que no sean el propietario ni estén en el grupo.
    //
    // Si el pipe ya existiera la función mkfifo simplemente verificará si ya existe un archivo 
    // con el nombre especificado. Si el archivo ya existe, mkfifo no lo sobrescribirá ni realizará 
    // ninguna acción que modifique el archivo existente. En cambio, simplemente retornará éxito 
    // y seguirá adelante sin hacer nada adicional.

    // Leer variable de configuración para ver si hace falta utilizar el pipe
    const char *monitor_activo;
    monitor_activo = obtener_valor_configuracion("MONITOR_ACTIVO", "NO");
    if (strcmp(monitor_activo, "NO") == 0) {
        // Si el monitor no está activo retornamos
        // printf("Monitor no activo, mensaje monitor: %s", message);
        return 0;
    }

    // Bloquear el mutex
    pthread_mutex_lock(&mutex_escritura_pipe);

    const char * pipeName;
    pipeName = obtener_valor_configuracion("PIPE_NAME", "/tmp/pipeAudita");
    mkfifo(pipeName, 0666);

    // Abrir el pipe para escritura
    int pipefd = open(pipeName, O_WRONLY);

    char buffer[MESSAGE_SIZE];
    snprintf(buffer, sizeof(buffer), "%s", message);

    // Escribir el mensaje en el pipe
    write(pipefd, buffer, (strlen(buffer)+1));
    escribirEnLog(LOG_DEBUG, "file_processor: pipe_send", "Escribiendo mensaje %s en pipe %s\n", message, pipeName );

    // Hay que poner un sleep, de otra forma hay veces que los mensajes llegan muy seguidos
    // y entonces no todos los registros se escriben correctamente en el pipe
    sleep_centiseconds(10);

    // Cerrar el pipe
    close(pipefd);

    // Desbloquear el mutex
    pthread_mutex_unlock(&mutex_escritura_pipe);

    return 0;
}
// ------------------------------------------------------------------
#pragma endregion EscrituraPipe


// ------------------------------------------------------------------
// FUNCIONES DE FILE PROCESSOR
// ------------------------------------------------------------------
#pragma region FileProcessor

// Semáforo de sincronización entre FileProcessor y Monitor
// Este nombre de semáforo tiene que ser igual en FileProcessor y Monitor
// Utilizaremos este semaforo para asegurar el acceso de los threads 
// a recursos compartidos (ficheros de entrada y fichero consolidado)
sem_t *semaforo_consolidar_ficheros_entrada;
// Nombre del semáforo
const char *semName;

// Función que se encarga de crear tantos hilos de configuración como se hayan definido 
// en el fichero de configuración
// Los hilos se implementan en la función hilo_observador
void crear_hilos_observacion(){
    // Obtener el número de hilos a crear
    int num_hilos; 
    
    //atoi recibe un string (numero de hilos a crear en este caso) y lo convierte en integer
    num_hilos = atoi(obtener_valor_configuracion("NUM_PROCESOS", "5"));
    escribirEnLog(LOG_INFO, "file_processor: crear_hilos_observacion", "Necesario crear %02d hilos de observación\n",num_hilos);

     // Dimensionar pool de hilos observadores
    pthread_t tid[num_hilos];
    int id[num_hilos];


    // Crear los hilos observadores
    for (int i = 0; i < num_hilos; i++) {
        id[i] = i + 1;
        escribirEnLog(LOG_INFO, "file_processor: crear_hilos_observacion", "Creado hilo número %02d\n", i);
        if (pthread_create(&tid[i], NULL, hilo_observador, (void *)&id[i]) != 0) {
            escribirEnLog(LOG_ERROR, "file_processor: crear_hilos_observacion", "Error al crear el hilo de observación");
            exit(EXIT_FAILURE);
        }
    }

    // Desanclar los hilos para que se ejecuten de forma independiente
    for (int i = 0; i < num_hilos; i++) {
        //El detach se utiliza para que el create no tenga que esperar a un join
        if (pthread_detach(tid[i]) != 0) {
            escribirEnLog(LOG_ERROR, "file_processor: crear_hilos_observacion", "Error al desanclar el hilo de observación");
            exit(EXIT_FAILURE);
        }
    }
    return;
}

// Función que implementa el Hilo que se encarga de procesar los ficheros 
// que aparezcan en la carpeta de datos de entrada y que cumplan con un patron de nombre
//      1) En primer lugar los mueve a una carpeta de procesados (dentro de datos) propia del hilo
//      2) Y después añade todos los registros CSV al fichero de consolidación en la carpeta de datos
void *hilo_observador(void *arg) {
    int id_hilo = *((int *)arg);
    const char *carpeta_datos;
    carpeta_datos = obtener_valor_configuracion("PATH_FILES", "../Datos");
    const char *prefijo_carpeta_procesos;
    prefijo_carpeta_procesos = obtener_valor_configuracion("PREFIJO_CARPETAS_PROCESO", "procesados");
    int contador_archivos = 1;
    char *horaInicioTexto;
    char *horaFinalTexto;

    // Patrón de nombre de ficheros a procesar por este hilo "SU001"
    // Recuperar  el prefijo del fichero de configuración
    const char *prefijo_ficheros;
    prefijo_ficheros = obtener_valor_configuracion("PREFIJO_FICHEROS", "SU");
    // Crear el patrón de nombre de los ficheros a procesar
    char patronNombre[20];
    sprintf(patronNombre,"%s%03d",prefijo_ficheros, id_hilo);
    char sucursal[10];
    snprintf(sucursal,sizeof(sucursal), "%s%03d", prefijo_ficheros, id_hilo);

    // Preparar la ruta del archivo de consolidación
    const char *archivo_consolidado;
    archivo_consolidado = obtener_valor_configuracion("INVENTORY_FILE", "consolidado.csv");
    // Preparar la ruta completa de archivo consolidado
    char archivo_consolidado_completo[PATH_MAX];
    snprintf(archivo_consolidado_completo, sizeof(archivo_consolidado_completo), "%s/%s", carpeta_datos, archivo_consolidado);

    // Preparar la ruta de la carpeta de "en proceso"
    char carpeta_proceso[PATH_MAX];
    snprintf(carpeta_proceso, sizeof(carpeta_proceso), "%s/%s%03d", carpeta_datos, prefijo_carpeta_procesos, id_hilo);
    

    escribirEnLog(LOG_INFO, "file_processor: hilo_observador", "Hilo observación %02d: observando carpeta %s patrón nombre: %s\n", id_hilo, carpeta_datos, patronNombre);

    // Bucle infinito para observar la carpeta
    while (1) {
        DIR *dir;
        struct dirent *entrada;

        // Abrir la carpeta de datos
        dir = opendir(carpeta_datos);
        if (dir == NULL) {
            perror("Error al abrir el directorio");
            exit(EXIT_FAILURE);
        }

        // Comprobar archivos en la carpeta de datos
        while ((entrada = readdir(dir)) != NULL) {
            struct stat info;
            char ruta_archivo[PATH_MAX];
            char archivo_origen[PATH_MAX];
            char archivo_origen_corto[PATH_MAX];
            char archivo_destino[PATH_MAX];
            char mensaje[100];
            
            // Verificar si el nombre del archivo cumple con el patrón del nombre
            if (strncmp(entrada->d_name, patronNombre, 5) == 0) {

                // Cada vez que llegue un fichero nuevo al directorio, la recepción de este debe mostrarse en pantalla 
                // y escribirse en el fichero de log. Usar un mensaje creativo basado en * u otro símbolo. 
                escribirEnLog(LOG_GENERAL, "file_processor: hilo_observador", "%02d:::Iniciando proceso fichero %s\n", id_hilo, entrada->d_name);

                // Registrar hora inicio (se utiliza en el log)
                horaInicioTexto = obtener_hora_actual();

                // Construir la ruta completa del archivo
                snprintf(ruta_archivo, sizeof(ruta_archivo), "%s/%s", carpeta_datos, entrada->d_name);
                
                // Crear el nombre corto del fichero de origen
                snprintf(archivo_origen_corto, sizeof(archivo_origen_corto), "%s", entrada->d_name);
                
                // Crear el path completo del fichero de origen
                snprintf(archivo_origen, sizeof(archivo_origen), "%s/%s", carpeta_datos, entrada->d_name);

                // Crear el path completo al fichero destino
                // Utilizamos (volatile size_t){sizeof(archivo_destino)} para evitar el truncation warning de compilación
                // (ver https://stackoverflow.com/questions/51534284/how-to-circumvent-format-truncation-warning-in-gcc)
                snprintf(archivo_destino, (volatile size_t){sizeof(archivo_destino)}, "%s/%s", carpeta_proceso, entrada->d_name);

                // Obtener información sobre el archivo
                if (stat(ruta_archivo, &info) != 0) {
                    escribirEnLog(LOG_ERROR, "file_processor: hilo_observador", "Hilo %02d: Error al obtener información del archivo\n", id_hilo);
                    exit(EXIT_FAILURE);
                }

                // Verificar si es un archivo regular
                if (S_ISREG(info.st_mode)) {

                    // Esperar en el semáforo para evitar colisiones
                    escribirEnLog(LOG_INFO, "file_processor: hilo_observador", "Hilo %02d: esperando semáforo...\n", id_hilo);
                    sem_wait(semaforo_consolidar_ficheros_entrada);

                    // Comprobar si la carpeta de "en proceso" existe, en caso contrario la creamos
                    struct stat st = {0};
                    if (stat(carpeta_proceso, &st) == -1) {
                        mkdir(carpeta_proceso, 0700);
                    }

                    // Mover el archivo a la carpeta de "en proceso"
                    if (mover_archivo(id_hilo, archivo_origen, archivo_destino) == EXIT_SUCCESS) {
                        // Una vez movido, hay que copiar las líneas al fichero de consolidación
                        int num_registros;
                        
                        num_registros = copiar_registros(id_hilo, sucursal, archivo_destino, archivo_consolidado_completo);
                        // Devuelve -1 en caso de error
                        if (num_registros != -1) {
                            // Copia de los registros correcta
                            contador_archivos++;

                            // Escribir el log
                            // Registrar hora final (se utiliza en el log)
                            horaFinalTexto = obtener_hora_actual();
                            // Formato: NoPROCESO:::INICIO:::FIN:::NOMBRE_FICHERO:::NoOperacionesConsolidadas 
                            escribirEnLog(LOG_GENERAL, "file_processor: hilo_observador", "%02d:::%s:::%s:::%s:::%0d\n", id_hilo, horaInicioTexto, horaFinalTexto, archivo_origen_corto, num_registros);

                            // Informar al Monitor
                            // Utilizamos (volatile size_t){sizeof(mensaje)} para evitar el truncation warning de compilación
                            // (ver https://stackoverflow.com/questions/51534284/how-to-circumvent-format-truncation-warning-in-gcc)
                            snprintf(mensaje, (volatile size_t){sizeof(mensaje)}, "file_processor: hilo_observador  %02d: fichero %s consolidado\n", id_hilo, archivo_origen_corto);
                        }
                        
                        //Cada proceso simulará un retardo aleatorio entre SIMULATE_SLEEP_MAX y SIMULATE_SLEEP_MIN
                        char mensaje[100];
                        snprintf(mensaje, sizeof(mensaje), "file_processor: hilo_observador: Hilo %02d: ", id_hilo);
                        simulaRetardo(mensaje);
                    }

                    // Liberar el semáforo
                    sem_post(semaforo_consolidar_ficheros_entrada);
                    escribirEnLog(LOG_INFO, "file_processor: hilo_observador", "Hilo %02d: liberado semáforo.\n", id_hilo);
                }
            }
        }

        // Cerrar la carpeta de datos
        closedir(dir);

        // Dormir por 1 segundo antes de revisar nuevamente
        sleep(1);
    }

    return NULL;
}

// Función para mover un archivo a otra carpeta
// Se utiliza para mover los archivos de las sucursales de la carpeta de Datos 
// a las carpetas de Procesados
int mover_archivo(int id_hilo, const char *archivo_origen, const char *archivo_destino) {
    escribirEnLog(LOG_INFO, "hilo_observacion", "Hilo %02d: Moviendo de %s a %s\n", id_hilo, archivo_origen, archivo_destino);

    // Mover el archivo a la carpeta de destino
    if (rename(archivo_origen, archivo_destino) != 0) {
        escribirEnLog(LOG_ERROR, "hilo_observacion", "Hilo %02d: Error al mover el archivo %s a %s\n", id_hilo, archivo_origen, archivo_destino);
        return EXIT_FAILURE;
    }

    escribirEnLog(LOG_INFO, "hilo_observacion", "Hilo %02d: Movido de %s a %s\n", id_hilo, archivo_origen, archivo_destino);
    return EXIT_SUCCESS;
}

// Función que copia los registros CSV de un archivo en otro
// Se utiliza para copiar los registros de los ficheros CSV de las sucursales al 
// fichero consolidado
int copiar_registros(int id_hilo, const char *sucursal, const char *archivo_origen, const char *archivo_consolidado) {
    escribirEnLog(LOG_INFO, "hilo_observacion", "Hilo %02d: Copiando registros CSV de %s a %s\n", id_hilo, archivo_origen, archivo_consolidado);

    FILE *archivo_entrada, *archivo_salida;

    // Abre el archivo de entrada en modo lectura
    archivo_entrada = fopen(archivo_origen, "r");
    if (archivo_entrada == NULL) {
        escribirEnLog(LOG_ERROR, "hilo_observacion", "Hilo %02d: Error al abrir el archivo de entrada\n", id_hilo);
        return -1;
    }

    // Abre el archivo de salida en modo anexar (append)
    archivo_salida = fopen(archivo_consolidado, "a");
    if (archivo_salida == NULL) {
        escribirEnLog(LOG_ERROR, "hilo_observacion", "Hilo %02d: Error al abrir el archivo de salida", id_hilo);
        fclose(archivo_entrada);
        return -1;
    }

    // Lee línea por línea del archivo de entrada
    char linea[MAX_LINE_LENGTH];
    char linea_escribir[MAX_LINE_LENGTH];
    int num_registros = 0;
    while (fgets(linea, MAX_LINE_LENGTH, archivo_entrada) != NULL) {
        // Escribe la línea leída en el archivo de salida
        
        // Primero hay que escribir el número de la sucursal
        // Utilizamos (volatile size_t){sizeof(linea_escribir)} para evitar el truncation warning de compilación
        // (ver https://stackoverflow.com/questions/51534284/how-to-circumvent-format-truncation-warning-in-gcc)
        snprintf(linea_escribir, (volatile size_t){sizeof(linea_escribir)}, "%s;%s",sucursal, linea);

        fputs(linea_escribir, archivo_salida);
        num_registros++;
    }
    // Cierra los archivos
    fclose(archivo_entrada);
    fclose(archivo_salida);
    

    escribirEnLog(LOG_INFO, "hilo_observacion", "Hilo %02d: Copiados registros CSV de %s a %s\n", id_hilo, archivo_origen, archivo_consolidado);

    // Enviar mensaje a Monitor a través del named pipe
    snprintf(linea_escribir, (volatile size_t){sizeof(linea_escribir)}, "Fichero consolidado actualizado por FileProcessor Hilo %02d con %01d registros", id_hilo, num_registros);
    pipe_send(linea_escribir);
    escribirEnLog(LOG_INFO, "hilo_observacion", "Hilo %02d: Escrito mensaje en pipe: %s\n", id_hilo, linea_escribir);

    return num_registros;
}

//-------------------------------------------------------------------------------------------------------------------------------
#pragma endregion FileProcessor


// ------------------------------------------------------------------
// MAIN: PROCESAMIENTO DE PARÁMETROS, INICIALIZACIÓN Y 
//       CREACIÓN DEL DEMONIO FILE PROCESSOR 
// ------------------------------------------------------------------
#pragma region Main


// Imprime en la consola la forma de utilizar FileProcessor
void imprimirUso() {
    printf("Uso: ./FileProcessor -g/--generar -s/--sucursales <NUMERO SUCURSALES> -l/--lineas <NUMERO LINEAS> -h/--help\n");
}


// Procesamiento de los parámetros de llamada desde la línea de comando
int procesarParametrosLlamada(int argc, char *argv[]) {
    int num_lineas = 0;
    int generar_pruebas = 0;
    int num_sucursales = 0;

    // Iterar sobre los argumentos proporcionados
    for (int i = 1; i < argc; i++) {
        // Si el argumento es "-g" o "--generar"
        if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--generar") == 0) {
            generar_pruebas = 1;
        }
        // Si el argumento es "-s" o "--sucursales"
        else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--sucursales") == 0) {
            // Verificar si hay otro argumento después
            if (i + 1 < argc) {
                num_sucursales = atoi(argv[i + 1]);
                i++;  // Saltar el siguiente argumento ya que es el número de sucursales
            } else {
                printf("Error: Falta el numero de sucursales.\n");
                return 1;
            }
        }
        // Si el argumento es "-l" o "--lineas"
        else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--lineas") == 0) {
            // Verificar si hay otro argumento después
            if (i + 1 < argc) {
                num_lineas = atoi(argv[i + 1]);
                i++;  // Saltar el siguiente argumento ya que es el número de líneas
            } else {
                printf("Error: Falta el número de líneas.\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            imprimirUso();
            return 1;
        }
    }

    if (generar_pruebas == 1) {
        // FileProcessor puede ser usado mediante parámetros para ejecutar los script. 
        
        // Mostrar los parámetros
        // printf("Parámetros de ejecución: generar=%01d num_lineas=%01d num_sucursales=%01d\n", generar_pruebas, num_lineas, num_sucursales);
        
        // Ejecutar el script de creación de datos
        printf("Inicializando file_procesor con parámetros\n\n");
        printf("Comenzando la preparación de datos de prueba...\n");
        char comando[255];
        sprintf(comando, "(rm -f *log; cd ../Test; ./genera_ficheros_prueba.sh --lineas %01d --sucursales %01d --operaciones 1 --ficheros 1 --path ../Datos/)", num_lineas, num_sucursales);
        printf("Ejecutando comando shell:\n %s\n", comando);
        system(comando);
        printf("Terminada la preparación de datos de prueba.\n\n");
        printf("Iniciando el programa file_processor...\n\n");
        return 0;
    } else {
        return 0;
    }
}


// Función de manejador de señal CTRL-C
void ctrlc_handler(int sig) {
    printf("file_processor: Se ha presionado CTRL-C. Terminando la ejecución.\n");
    escribirEnLog(LOG_INFO, "file_processor: ctrlc_handler", "Se ha pulsado CTRL-C\n");

    // Acciones que hay que realizar al terminar el programa
    // Cerrar el semáforo
    sem_close(semaforo_consolidar_ficheros_entrada);
    // Borrar el semáforo
    sem_unlink(semName);
    
    escribirEnLog(LOG_INFO, "file_processor: ctrlc_handler", "Semáforo semaforo_consolidar_ficheros_entrada cerrado y borrado\n");
    escribirEnLog(LOG_INFO, "file_processor: ctrlc_handler", "Proceso terminado\n");

    // Fin del programa
    exit(EXIT_SUCCESS);
}

// Función main
int main(int argc, char *argv[]) //argc es el contador de parámetros y argv es el valor de estos parámetros
{
    // Procesar parámetros de llamada
    if (procesarParametrosLlamada(argc, argv) == 1) {
        // Ha habido un error con los parámetros
        escribirEnLog(LOG_ERROR, "file_processor: main", "Error procesando los parámetros de llamada\n");
        return 1;
    }

    // Escritura en log de inicio de ejecución
    escribirEnLog(LOG_GENERAL, "file_processor: main", "Iniciando ejecución FileProcessor\n");

    // Registra el manejador de señal para SIGINT para CTRL-C
    if (signal(SIGINT, ctrlc_handler) == SIG_ERR) {
        escribirEnLog(LOG_ERROR, "file_processor: main", "No se pudo capturar SIGINT\n");
        return EXIT_FAILURE;
    }


    // Vamos a crear un semáforo con un nombre común para file procesor y monitor de forma que podamos utilizarlo
    // para asegurar el acceso a recursos comunes desde ambos procesos. 
    // Creamos un semáforo de 1 recursos con nombre definido en SEMAPHOR_NAME y permisos de lectura y escritura
    semName = obtener_valor_configuracion("SEMAPHORE_NAME", "/semaforo");
    semaforo_consolidar_ficheros_entrada = sem_open(semName, O_CREAT , 0644, 1);
    if (semaforo_consolidar_ficheros_entrada == SEM_FAILED){
        perror("semaforo");
        escribirEnLog(LOG_ERROR, "file_processor: main", "Error al abrir el semáforo\n");
        exit(EXIT_FAILURE);
    }
    escribirEnLog(LOG_INFO, "file_processor:main", "Semáforo %s creado\n", semName);

    //Creación de los hilos de observación de ficheros de las sucursales
    crear_hilos_observacion();
    
    //Bucle infinito para que el proceso sea un demonio
    escribirEnLog(LOG_INFO, "file_processor: main", "Entrando en ejecucion indefinida\n");

    while (1){
        sleep(10); //Solo para evitar que el main salga y se consuma mucha CPU
    }

    // Código inaccesible, el programa lo acabará le usuario con CTRL+C 
    // de forma que los semáforos y recursos se liberarán en el manejador
    return 0;

}
#pragma endregion Main
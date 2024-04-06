#!/bin/bash

# Script para probar completamente la solución
#   - Compila la solución
#   - Genera datos de prueba
#   - Ejecuta en segundo plato Monitor y FileProcessor
#   - Analiza los resultados comprobando que todos los patrones de fraude
#     son detectados correctamente
#   - Termina los procesos Monitor y File Processor

clear

echo
echo
echo "PRUEBA COMPLETA DE LA SOLUCIÓN"
echo

# -------------------------------------------------
# COMPILAR LA SOLUCION
# -------------------------------------------------

echo
echo "COMPILANDO LA SOLUCION"
echo

# Compilar FileProcessor.c

# Nombre del archivo del programa C
archivo_programa="../FileProcessor/FileProcessor.c"
echo "Compilando $archivo_programa"

# Nombre del ejecutable después de la compilación
ejecutable="FileProcessor"

# Opciones de compilación para GLib
# cflags=$(pkg-config --cflags glib-2.0)

# Opciones de enlace para GLib
# ldflags=$(pkg-config --libs glib-2.0)

# Compilar el programa C con GLib
gcc "$archivo_programa" -o "$ejecutable" $cflags $ldflags

# Verificar si hubo errores durante la compilación
if [ $? -eq 0 ]; then
    echo "El programa $archivo_programa se ha compilado correctamente en $ejecutable."
else
    echo "Hubo errores durante la compilación de $archivo_programa."
    exit -1;
fi

# Compilar Monitor.c

# Nombre del archivo del programa C
# TODO: cambiar esta carpeta
archivo_programa="../Monitor/Monitor.c"
echo "Compilando $archivo_programa"

# Nombre del ejecutable después de la compilación
ejecutable="Monitor"

# Opciones de compilación para GLib
cflags=$(pkg-config --cflags glib-2.0)

# Opciones de enlace para GLib
ldflags=$(pkg-config --libs glib-2.0)

# Compilar el programa C con GLib
gcc "$archivo_programa" -o "$ejecutable" $cflags $ldflags

# Verificar si hubo errores durante la compilación
if [ $? -eq 0 ]; then
    echo "El programa $archivo_programa se ha compilado correctamente en $ejecutable."
else
    echo "Hubo errores durante la compilación de $archivo_programa."
    exit -1;
fi


# -------------------------------------------------
# GENERAR DATOS DE PRUEBA
# -------------------------------------------------

echo
echo "GENERANDO LOS DATOS DE PRUEBA"
echo

# Los datos de prueba se van a generar en ./Datos
# Por lo tanto, primero lo elimino, luego lo creo
carpetaDatos="../Pruebas/Datos"
rm -fR ../Pruebas/Datos
mkdir ../Pruebas/Datos

# Composición del nombre del fichero a generar
fechaFormateada=$(date +"%d%m%Y")
nombreCompletoFichero="${carpetaDatos}/SU001_OPE001_${fechaFormateada}_001.csv"
touch $nombreCompletoFichero

# El siguiente snipnet se ejecuta en otro directorio y luego vuelvo a la ruta actual
# (por eso está entre paréntesis)
(
    cd ../GenerarDatos
    ./genera_transacciones_fraude.sh --patronFraude patron_fraude_1 --usuario FRAU001 --numeroOperacionComienzo 500 >> $nombreCompletoFichero
    ./genera_transacciones_fraude.sh --patronFraude patron_fraude_2 --usuario FRAU002 --numeroOperacionComienzo 600 >> $nombreCompletoFichero
    ./genera_transacciones_fraude.sh --patronFraude patron_fraude_3 --usuario FRAU003 --numeroOperacionComienzo 700 >> $nombreCompletoFichero
    ./genera_transacciones_fraude.sh --patronFraude patron_fraude_4 --usuario FRAU004 --numeroOperacionComienzo 800 >> $nombreCompletoFichero
    ./genera_transacciones_fraude.sh --patronFraude patron_fraude_5 --usuario FRAU005 --numeroOperacionComienzo 900 >> $nombreCompletoFichero
)
echo "Datos de prueba generados incluyendo los 5 patrones de fraude"


# -------------------------------------------------
# EJECUTAR EN SEGUNDO PLANO FileProcessor y Monitor
# -------------------------------------------------

echo
echo "EJECUCION DE LA SOLUCION EN SEGUNDO PLANO"
echo

# Eliminamos los ficheros de log
echo "Eliminando los ficheros de log"
rm -f ./*.log

# Ejecuto en segundo plano Monitor y almaceno el número de proceso para luego matarlo
./Monitor > MonitorConsole.log &
pid_Monitor=$!
echo "Ejecutando ./Monitor con PID = $pid_Monitor, salida de consola en MonitorConsole.log"

# Doy un poco de tiempo
segundos=2
echo "Esperando $segundos segundos..."
sleep $segundos;

# Ejecuto en segundo plano FileProcessor y almaceno el número de proceso para luego matarlo
./FileProcessor > FileProcessorConsole.log &
pid_FileProcessor=$!
echo "Ejecutando ./FileProcessor con PID = $pid_FileProcessor, salida de consola en FileProcessorConsole.log"


# -------------------------------------------------
# ESPERAR UN POCO DE TIEMPO PARA QUE DE TIEMPO
# A QUE SE EJECUTEN LOS PROCESOS
# -------------------------------------------------

segundos=30
echo "Esperando $segundos segundos mientras se ejecutan los procesos..."
sleep $segundos;
echo "Los procesos han debido ejecutarse correctamente, vamos a comprobar los resultados..."

# -------------------------------------------------
# COMPROBAR LOS DATOS GENERADOS
# -------------------------------------------------

# Función de ayuda para imprimir el mensaje
function imprimir_resultado_prueba() {
    # Variables locales para los parámetros
    local patron_fraude=$1
    local resultado_esperado=$2
    local resultado_obtenido=$3

    # Colores ANSI
    local COLOR_VERDE='\033[0;32m'
    local COLOR_ROJO='\033[0;31m'
    local COLOR_BLANCO='\033[0m' # Reset de color

    if [ "$resultado_esperado" == "$resultado_obtenido" ]; then
        local resultado="${COLOR_VERDE}PRUEBA CORRECTA${COLOR_BLANCO}"
    else
        local resultado="${COLOR_ROJO}PRUEBA INCORRECTA${COLOR_BLANCO}"
    fi

    # Imprimir con colores (utilizando echo -e)
    echo -e "Resultado del test de $patron_fraude: $resultado Resultado Esperado = $resultado_esperado - Resultado Obtenido = $resultado_obtenido"
    #echo
}

echo
echo "COMPROBACION DE LOS RESULTADOS GENERADOS"
echo
echo "Se han ejecutado los patrones de fraude con los siguientes resultados:"
echo
resultado_esperado=1
resultado_obtenido=$(cat ./Datos/resultado_patron_01.csv 2> /dev/null | grep "FRAU001" | grep "Registros en la Misma Hora=6" | wc -l)
imprimir_resultado_prueba "Patrón de Fraude 1" $resultado_esperado $resultado_obtenido 

resultado_esperado=1
resultado_obtenido=$(cat ./Datos/resultado_patron_02.csv 2> /dev/null | grep "FRAU002" | grep "Registros a la vez=4" | wc -l)
imprimir_resultado_prueba "Patrón de Fraude 2" $resultado_esperado $resultado_obtenido

resultado_esperado=1
resultado_obtenido=$(cat ./Datos/resultado_patron_03.csv 2> /dev/null | grep "FRAU003" | grep "Registros con Error=4" | wc -l)
imprimir_resultado_prueba "Patrón de Fraude 3" $resultado_esperado $resultado_obtenido

resultado_esperado=1
resultado_obtenido=$(cat ./Datos/resultado_patron_04.csv 2> /dev/null | grep "FRAU004" | grep "Registros con Todos los Tipos de Operaciones" | wc -l)
imprimir_resultado_prueba "Patrón de Fraude 4" $resultado_esperado $resultado_obtenido

resultado_esperado=1
resultado_obtenido=$(cat ./Datos/resultado_patron_05.csv 2> /dev/null | grep "FRAU005" | grep "Saldo negativo=-100" | wc -l)
imprimir_resultado_prueba "Patrón de Fraude 5" $resultado_esperado $resultado_obtenido


# -------------------------------------------------
# MATAR LOS PROCESOS EN SEGUNDO PLANO
# -------------------------------------------------

# La señal SIGINT es la misma que CTRL-C, así nos aseguramos de que se cierren ordenadamente los semáforos

echo
echo "TERMINANDO LOS PROCESOS DE PRUEBA"
echo
echo "Ahora es necesario  terminar los procesos de prueba enviando SIGINT..."

echo "Terminando el proceso FileProcessor con PID = $pid_FileProcessor"
kill -SIGINT $pid_FileProcessor
echo "Terminando el proceso Monitor con PID = $pid_Monitor"
kill -SIGINT $pid_Monitor

echo
echo "Script de pruebas terminado."
echo "Puede ver los ficheros generados en ./Datos y los logs de la ejecución en ./"
echo "Fin de pruebas."



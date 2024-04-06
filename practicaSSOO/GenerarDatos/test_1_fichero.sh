#!/bin/bash

# Genera 1 fichero para prueba
#   1) Elimina los ficheros procesados de la SUC01
#   2) Genera 1 fichero en la SUC01

# Ejemplo de llamada:
# chmod +x test_1_fichero.sh
# ./test_1_fichero.sh

# Script permite parámetros largos y cortos
# Parámetros y valores por defecto
#   -l / --lineas <número de líneas que contendrán los ficheros> (por defecto 20) 
#   -s / --sucursal <número de la sucursal a generar> (por defecto 1)
#   -o / --operacion <tipo de operación a generar> (por defecto 1)
#   -f / --fichero <número de fichero a generar (por defecto 1)
#   -p / --path <ruta en la que se almacenarán los ficheros generados (por defecto "../datos/")

# Valores por defecto
sucursal_defecto=1         
operacion_defecto=1        
fichero_defecto=1   
numeroRegistros_defecto=20
pathFichero_defecto="../datos/" 

# En principio asignamos los valores por defecto
sucursal="$sucursal_defecto"
operacion="$operacion_defecto"
fichero="$fichero_defecto"
numeroRegistros="$numeroRegistros_defecto"
pathFichero="$pathFichero_defecto"

# Ahora parseamos los parámetros y si existen los asignamos
while [[ $# -gt 0 ]]; do
    key="$1"
    case $key in
        --lineas|-l)
            numeroRegistros="$2"
            shift
            ;;
        --sucursal|-s)
            sucursal="$2"
            shift
            ;;
        --operacion|-o)
            operacion="$2"
            shift
            ;;
        --fichero|-f)
            fichero="$2"
            shift
            ;;
        --path|-p)
            pathFichero="$2"
            shift
            ;;
        *)
            echo "Parámetro desconocido: $key"
            shift
            exit
            ;;
    esac
    shift
done

# Genera los datos de prueba

# Genera el nombre del fichero
textoSucursal="SU$(printf "%03d" $sucursal)"
textoOperacion="OPE$(printf "%03d" $operacion)"
fechaFormateada=$(date +"%d%m%Y")
textoNumeroFichero="$(printf "%03d" $fichero)"
nombreFichero="${textoSucursal}_${textoOperacion}_${fechaFormateada}_${textoNumeroFichero}.csv"
nombreCompletoFichero="${pathFichero}${nombreFichero}"
nombreCompletoFicheroProcesados="${pathFichero}procesados$(printf "%03d" $sucursal)/${nombreFichero}"

# Borra el fichero en procesados
rm "${nombreCompletoFicheroProcesados}"

# General las transacciones en el fichero
./genera_transacciones.sh --usernamePrefix USER --userFrom 100 --userTo 200 --lineas $numeroRegistros > $nombreCompletoFichero

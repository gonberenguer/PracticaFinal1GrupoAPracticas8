#!/bin/bash

# Script para compilar Monitor.c en Monitor

# Nombre del archivo del programa C
archivo_programa="Monitor.c"

# Nombre del ejecutable después de la compilación
ejecutable="Monitor"

# Opciones de compilación para GLib
cflags="$(pkg-config --cflags glib-2.0) -pthread"

# Opciones de enlace para GLib
ldflags=$(pkg-config --libs glib-2.0)

# Compilar el programa C con GLib
gcc "$archivo_programa" -o "$ejecutable" $cflags $ldflags

# Verificar si hubo errores durante la compilación
if [ $? -eq 0 ]; then
    echo "El programa $archivo_programa se ha compilado correctamente en $ejecutable."
else
    echo "Hubo errores durante la compilación."
fi
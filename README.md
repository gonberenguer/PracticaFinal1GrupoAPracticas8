# PracticaFinal1GrupoAPracticas8

## Abstracto
El objetivo de la práctica es diseñar y desarrollar una solución para UFVAudita, una empresa de auditoría de transacciones bancarias, destinada a procesar informes procedentes de distintas sucursales bancarias para detectar patrones indicativos de comportamientos irregulares o delictivos por parte de los usuarios. La metodología consiste en crear una solución parametrizable, FileProcessor, capaz de procesar ficheros depositados aleatoriamente desde distintas sucursales en un directorio común utilizando un pool de procesos ligerosMecanismos de sincronización como semáforos aseguran el correcto funcionamiento del sistema, mientras que un proceso de monitorización detecta posibles patrones de comportamiento fraudulento comunicados a través de tuberías. Los resultados incluyen un archivo consolidado que refleja todas las transacciones y un archivo de registro que contiene información sobre los archivos procesados. La solución también incorpora procesos de detección de patrones capaces de identificar comportamientos irregulares específicos señalados en las especificaciones. La conclusión subraya el éxito de la implantación de una solución escalable y eficiente que cumple los requisitos establecidos, facilitada por un trabajo en equipo eficaz y unos protocolos de prueba rigurosos.

## Compilación/Ejecución
1)	Compilar FileProcessor y Monitor de acuerdo con lo indicado en “Compilación de la Solución”
2)	Abrir una terminal en Linux a la que nos referiremos como “Consola Monitor”
3)	Borramos completamente el contenido de la carpeta ./Datos con el comando rm -fR ./Datos/*
4)	Borramos los logs de Monitor mediante el comando rm ./Monitor/*log
5)	Cambiar a ruta ./Monitor
6)	Ejecutar el proceso Monitor con el comando ./Monitor
7)	Abrir una terminal en Linux a la que nos referiremos como “Consola FileProcessor”
8)	Borramos los logs de FileProcessor mediante el comando rm ./FileProcessor/*log
9)	Cambiar a ruta ./FileProcessor
10)	Ejecutar el proceso FileProcessor con el comando ./FileProcessor
11)	Abrir una terminal en Linux a la que nos referiremos como “Consola Datos”
12)	Cambiar a ruta ./GenerarDatos
13)	Generar datos de prueba con ejecutando el comando ./test.sh
14)	Dejar funcionar el sistema durante unos minutos, hasta que se acaben de procesar todos los datos
15)	Podemos generar 1 fichero adicional de prueba yendo a la “Consola Datos” y ejecutando el comando ./test_1_fichero.sh
16)	Repetir las pruebas anteriores observando los resultados
17)	Para finalizar la ejecución, pulsar CTRL-C en “Consola Monitor” y CTRL-C en “Consola FileProcessor”


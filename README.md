# PracticaFinal1GrupoAPracticas8

## Abstract
The purpose of the practice is to design and develop a solution for UFVAudita, a banking transaction auditing company, aimed at processing reports from various bank branches to detect patterns indicative of irregular or criminal behavior by users. The methodology involves creating a parameterizable solution, FileProcessor, capable of processing randomly deposited files from differents branches in a common directory using a pool of lightweight processesSynchronization mechanisms like semaphores ensure proper system functioning, while a monitoring process detects potential fraudulent behavior patterns communicated via pipes. Results include a consolidated file reflecting all transactions and a log file containing information on processed files. The solution also incorporates pattern detection processes capable of identifying specific irregular behaviors outlined in the specifications. The conclusion emphasizes the successful implementation of a scalable and efficient solution meeting the requirements set forth, facilitated by effective teamwork and rigorous testing protocols.

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


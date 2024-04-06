/**
FileProcessor.h

    Declaración de funciones de FileProcessor.c
*/ 

// Para evitar que se puedan llegar a declarar  las funciones varias veces
#pragma once

void *hilo_observador(void *arg);
int mover_archivo(int id_hilo, const char *archivo_origen, const char *archivo_destino);
int copiar_registros(int id_hilo, const char *sucursal, const char *archivo_origen, const char *archivo_consolidado);
void imprimirUso();
int procesarParametrosLlamada(int argc, char *argv[]);
int pipe_send(const char *message);
void simulaRetardo(const char *mensaje);
void sleep_centiseconds(int n);
void obtenerFechaHora2(char * fechaHora2);
void obtenerFechaHora(char * fechaHora);
char * obtener_hora_actual();

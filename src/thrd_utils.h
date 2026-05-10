#ifndef THRD_UTILS_H
#define THRD_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>
#include <stdatomic.h>

/*
 * Macro per controllare le chiamate della libreria <threads.h>
 * Verifica se il valore di ritorno è diverso da thrd_success.
 * Poiché perror() non funziona con questi codici, usiamo fprintf su stderr.
 */
#define THRD_CHECK(func_call, msg) do { \
    int thrd_res = (func_call); \
    if (thrd_res != thrd_success) { \
        fprintf(stderr, "[Errore Thread] %s (Codice errore: %d)\n", msg, thrd_res); \
        exit(EXIT_FAILURE); \
    } \
} while(0)

#endif
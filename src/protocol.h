#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <unistd.h>
#include <sys/types.h>

/* Legge esattamente n byte. Ritorna n, o < n se EOF, o -1 se errore */
ssize_t readn(int fd, void *ptr, size_t n);

/* Scrive esattamente n byte. Ritorna n, o -1 se errore */
ssize_t writen(int fd, const void *ptr, size_t n);

/* Header per le coppie <token, valore> (Pag. 8 del PDF) */
typedef struct {
    int token_len;
    int value_len;
} mr_pair_header_t;

#endif
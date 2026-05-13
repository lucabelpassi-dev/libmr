#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <unistd.h>
#include <sys/types.h>
#include "mr.h"  /* per mr_file_line_t */

/* ============================================================
 *   LIVELLO BASSO: lettura/scrittura "tutti o nulla" sui fd
 * ============================================================
 * Una singola read()/write() puo' trasferire MENO byte del richiesto.
 * Queste funzioni iterano finche' non hanno letto/scritto tutti i byte
 * richiesti, oppure trovano EOF/errore. */

/* Ritorna n se ha letto tutti i byte richiesti, < n se ha incontrato EOF,
 * -1 in caso di errore. */
ssize_t readn(int fd, void *ptr, size_t n);

/* Ritorna n se ha scritto tutti i byte richiesti, -1 in caso di errore. */
ssize_t writen(int fd, const void *ptr, size_t n);

/* ============================================================
 *   INTESTAZIONI DEI TRE MESSAGGI
 * ============================================================ */

/* Messaggio "Riga": processo principale -> mapper */
typedef struct {
    int            file_name_len;
    int            line_len;
    unsigned long  line_number;
} mr_line_header_t;

/* Messaggio "Coppia": mapper -> reducer */
typedef struct {
    int token_len;
    int value_len;
} mr_pair_header_t;

/* Messaggio "Risultato": reducer -> processo principale */
typedef struct {
    int token_len;
    int result_len;
} mr_result_header_t;

/* ============================================================
 *   STRUTTURE DI APPOGGIO PER LA LETTURA
 * ============================================================
 * Quando leggiamo un messaggio dal fd, allochiamo i buffer per
 * conservare i dati. Queste struct rendono esplicito chi possiede
 * i buffer (il chiamante deve liberarli). */

typedef struct {
    char         *file_name;     /* allocato, NUL-terminato */
    size_t        file_name_len;
    unsigned long line_number;
    char         *line;          /* allocato, NUL-terminato */
    size_t        line_len;
} mr_line_msg_t;

typedef struct {
    char  *token;     /* allocato, NUL-terminato */
    size_t token_len;
    void  *value;     /* allocato, opaco */
    size_t value_size;
} mr_pair_msg_t;

typedef struct {
    char  *token;     /* allocato, NUL-terminato */
    size_t token_len;
    void  *result;    /* allocato, opaco */
    size_t result_size;
} mr_result_msg_t;

/* ============================================================
 *   FUNZIONI DI ALTO LIVELLO: scrivono/leggono un messaggio intero
 * ============================================================ */

/* --- Messaggio "Riga" --- */
int  protocol_write_line(int fd, const mr_file_line_t *line);
int  protocol_read_line (int fd, mr_line_msg_t *out);  /* 1=ok, 0=EOF, -1=err */
void protocol_line_msg_free(mr_line_msg_t *msg);

/* --- Messaggio "Coppia" --- */
int  protocol_write_pair(int fd, const char *token, size_t token_len,
                                 const void *value, size_t value_size);
int  protocol_read_pair (int fd, mr_pair_msg_t *out);
void protocol_pair_msg_free(mr_pair_msg_t *msg);

/* --- Messaggio "Risultato" --- */
int  protocol_write_result(int fd, const char *token, size_t token_len,
                                   const void *result, size_t result_size);
int  protocol_read_result (int fd, mr_result_msg_t *out);
void protocol_result_msg_free(mr_result_msg_t *msg);

#endif
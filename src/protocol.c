#include "protocol.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

/* src/protocol.c: funzioni che raccontano che forma hanno e come viaggiano i dati sulle pipe del framework */

/* Funzione che itera per forzare la lettura di tutti gli n byte richiesti */
ssize_t readn(int fd, void *ptr, size_t n) {
    size_t nleft = n;
    ssize_t nread;
    char *bufptr = ptr;

    while (nleft > 0) {
        if ((nread = read(fd, bufptr, nleft)) < 0) {
            if (errno == EINTR) nread = 0; // Interrotto da segnale, riprova
            else return -1;                // Errore reale
        } else if (nread == 0) break;      // EOF (Pipe chiusa dall'altra parte)

        nleft -= nread;
        bufptr += nread;
    }
    return (n - nleft);
}

/* Funzione che itera per forzare la scrittura di tutti gli n byte richiesti */
ssize_t writen(int fd, const void *ptr, size_t n) {
    size_t nleft = n;
    ssize_t nwritten;
    const char *bufptr = ptr;

    while (nleft > 0) {
        if ((nwritten = write(fd, bufptr, nleft)) <= 0) {
            if (nwritten < 0 && errno == EINTR) nwritten = 0; // Riprova
            else return -1;                                   // Errore
        }
        nleft -= nwritten;
        bufptr += nwritten;
    }
    return n;
}

/* ============================================================
 *   Helper interno: validazione lunghezza letta dall'intestazione
 * ============================================================ */

#define PROTOCOL_MAX_PAYLOAD (16 * 1024 * 1024)  /* 16MB per campo */

/* Misura di sicurezza per essere certi che in una pipe viaggino header "sensati" */
static int validate_len(int n) {
    if (n < 0)                        return -1;
    if ((size_t)n > PROTOCOL_MAX_PAYLOAD) return -1;
    return 0;
}

/* ============================================================
 *   MESSAGGIO "RIGA"
 * ============================================================ */

/* Serializza una mr_file_line_t in tre writen consecutive: header, file_name, line. */
int protocol_write_line(int fd, const mr_file_line_t *line) {
    if (line == NULL) { errno = EINVAL; return -1; }
    if (line->file_name_len > INT_MAX || line->line_len > INT_MAX) {
        errno = EOVERFLOW; return -1;
    }

    /* 1. Prepara l'intestazione */
    mr_line_header_t hdr = {
        .file_name_len = (int)line->file_name_len,
        .line_len      = (int)line->line_len,
        .line_number   = line->line_number,
    };

    /* 2. Scrivi intestazione + payload con una pipeline di writen. */
    if (writen(fd, &hdr, sizeof hdr) == -1) return -1;                      // header 
    if (line->file_name_len > 0 &&
        writen(fd, line->file_name, line->file_name_len) == -1) return -1;  // file_name (se non vuoto)
    if (line->line_len > 0 &&
        writen(fd, line->line, line->line_len) == -1) return -1;            // line (se non vuota)

    return 0;
}

/* Lettura messaggio "riga" */
int protocol_read_line(int fd, mr_line_msg_t *out) {
    if (out == NULL) { errno = EINVAL; return -1; }
    out->file_name = NULL;
    out->line      = NULL;

    /* 1. Leggi l'header (16 byte) */
    mr_line_header_t hdr;
    ssize_t r = readn(fd, &hdr, sizeof hdr);
    if (r == 0)             return 0;   /* EOF pulito: nessun messaggio iniziato */
    if (r != (ssize_t)sizeof hdr) {     /* EOF a metà header = errore */
        if (r >= 0) errno = EPROTO;
        return -1;
    }

    /* 2. Valida le lunghezze prima di allocare */
    if (validate_len(hdr.file_name_len) == -1 ||
        validate_len(hdr.line_len) == -1) {
        errno = EPROTO; return -1;
    }

    /* 3. Alloca i buffer (con +1 byte per il '\0' terminatore) */
    out->file_name = malloc((size_t)hdr.file_name_len + 1);
    out->line      = malloc((size_t)hdr.line_len + 1);
    if (out->file_name == NULL || out->line == NULL) goto err;

    /* 4. Leggi i payload */
    if (hdr.file_name_len > 0 &&
        readn(fd, out->file_name, hdr.file_name_len) != hdr.file_name_len) goto err;
    if (hdr.line_len > 0 &&
        readn(fd, out->line, hdr.line_len) != hdr.line_len) goto err;

    /* 5. NUL-terminazione */
    out->file_name[hdr.file_name_len] = '\0';
    out->line[hdr.line_len]           = '\0';
    out->file_name_len = (size_t)hdr.file_name_len;
    out->line_len      = (size_t)hdr.line_len;
    out->line_number   = hdr.line_number;

    return 1;

err:
    free(out->file_name); out->file_name = NULL;
    free(out->line);      out->line      = NULL;
    return -1;
}

void protocol_line_msg_free(mr_line_msg_t *msg) {
    if (msg == NULL) return;
    free(msg->file_name); msg->file_name = NULL;
    free(msg->line);      msg->line      = NULL;
}

/* ============================================================
 *   MESSAGGIO "COPPIA"  (struttura identica per "risultato")
 * ============================================================ */

int protocol_write_pair(int fd, const char *token, size_t token_len,
                                const void *value, size_t value_size) {
    if (token == NULL && token_len > 0) { errno = EINVAL; return -1; }
    if (value == NULL && value_size > 0) { errno = EINVAL; return -1; }
    if (token_len > INT_MAX || value_size > INT_MAX) {
        errno = EOVERFLOW; return -1;
    }

    mr_pair_header_t hdr = {
        .token_len = (int)token_len,
        .value_len = (int)value_size,
    };

    if (writen(fd, &hdr, sizeof hdr) == -1) return -1;
    if (token_len  > 0 && writen(fd, token, token_len)  == -1) return -1;
    if (value_size > 0 && writen(fd, value, value_size) == -1) return -1;
    return 0;
}

int protocol_read_pair(int fd, mr_pair_msg_t *out) {
    if (out == NULL) { errno = EINVAL; return -1; }
    out->token = NULL; out->value = NULL;

    mr_pair_header_t hdr;
    ssize_t r = readn(fd, &hdr, sizeof hdr);
    if (r == 0) return 0;
    if (r != (ssize_t)sizeof hdr) { if (r >= 0) errno = EPROTO; return -1; }

    if (validate_len(hdr.token_len) == -1 ||
        validate_len(hdr.value_len) == -1) { errno = EPROTO; return -1; }

    out->token = malloc((size_t)hdr.token_len + 1);
    out->value = (hdr.value_len > 0) ? malloc((size_t)hdr.value_len) : NULL;
    if (out->token == NULL || (hdr.value_len > 0 && out->value == NULL)) goto err;

    if (hdr.token_len > 0 &&
        readn(fd, out->token, hdr.token_len) != hdr.token_len) goto err;
    if (hdr.value_len > 0 &&
        readn(fd, out->value, hdr.value_len) != hdr.value_len) goto err;

    out->token[hdr.token_len] = '\0';
    out->token_len  = (size_t)hdr.token_len;
    out->value_size = (size_t)hdr.value_len;
    return 1;

err:
    free(out->token); out->token = NULL;
    free(out->value); out->value = NULL;
    return -1;
}

void protocol_pair_msg_free(mr_pair_msg_t *msg) {
    if (msg == NULL) return;
    free(msg->token); msg->token = NULL;
    free(msg->value); msg->value = NULL;
}

/* ============================================================
 *   MESSAGGIO "RISULTATO"  (copia conforme di "coppia")
 * ============================================================
 * Stesso layout della coppia, ma teniamo i nomi semantici distinti. */

int protocol_write_result(int fd, const char *token, size_t token_len,
                                   const void *result, size_t result_size) {
    /* Riusiamo write_pair: layout identico. */
    return protocol_write_pair(fd, token, token_len, result, result_size);
}

int protocol_read_result(int fd, mr_result_msg_t *out) {
    if (out == NULL) { errno = EINVAL; return -1; }
    mr_pair_msg_t tmp;
    int r = protocol_read_pair(fd, &tmp);
    if (r != 1) return r;
    /* "Trasferiamo" i puntatori dall'una all'altra (zero copia). */
    out->token       = tmp.token;
    out->token_len   = tmp.token_len;
    out->result      = tmp.value;
    out->result_size = tmp.value_size;
    return 1;
}

void protocol_result_msg_free(mr_result_msg_t *msg) {
    if (msg == NULL) return;
    free(msg->token);  msg->token  = NULL;
    free(msg->result); msg->result = NULL;
}
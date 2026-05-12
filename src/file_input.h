#ifndef FILE_INPUT_H
#define FILE_INPUT_H

#include "mr.h"  /* per mr_file_line_t */

/*
 * Sorgente di righe logiche da uno o piu' file di testo.
 *
 * Modulo non thread-safe: previsto per essere usato da un singolo
 * thread (il main thread del processo principale).
 *
 * Tipo opaco: i campi sono privati a file_input.c.
 */
typedef struct file_input file_input_t;

/*
 * Apre la sorgente di input.
 *
 * `path` puo' essere:
 *   - un file regolare: verra' letto solo quel file;
 *   - una directory: vengono letti tutti i file regolari contenuti
 *     direttamente in essa, in ordine lessicografico per nome
 *     (no scansione ricorsiva).
 *
 * Al ritorno con successo, *fi punta a un nuovo file_input_t da
 * rilasciare con file_input_close().
 *
 * Ritorna 0 su successo, -1 su errore (errno impostato).
 */
int file_input_open(file_input_t **fi, const char *path);

/*
 * Legge la prossima riga logica.
 *
 * Al ritorno con valore 1, i campi di *line sono validi FINO ALLA
 * PROSSIMA CHIAMATA di file_input_next() o file_input_close() sullo
 * stesso oggetto. Il chiamante NON deve liberare nulla.
 *
 * Il contenuto puntato da line->line non include l'eventuale '\n'
 * terminale: line_len e' la lunghezza effettiva senza terminatore.
 *
 * Il line_number riparte da 1 a ogni nuovo file della sorgente.
 *
 * Ritorna:
 *    1 = riga letta;
 *    0 = fine input (nessuna riga residua in nessun file);
 *   -1 = errore (errno impostato).
 */
int file_input_next(file_input_t *fi, mr_file_line_t *line);

/*
 * Chiude la sorgente e libera tutte le risorse.
 * Safe da chiamare anche con fi == NULL.
 */
void file_input_close(file_input_t *fi);

#endif
#ifndef GROUP_TABLE_H
#define GROUP_TABLE_H

#include <stddef.h>
#include "mr.h"  /* per mr_value_t */

/* Tipo opaco della tabella: il consumatore non vede i campi interni. */
typedef struct group_table group_table_t;

/* Struct di un'entry: ESPOSTA per comodita' del consumatore
 * (reducer_proc.c leggera' direttamente token, values, values_count).
 *
 * I campi con prefisso "_" sono privati: il consumatore NON li tocca. */
typedef struct group_entry {
    /* --- Campi pubblici (leggibili dopo finalize) --- */
    char       *token;            /* stringa C NUL-terminata */
    size_t      token_len;
    mr_value_t *values;           /* array contiguo, lungo values_count */
    size_t      values_count;

    /* --- Campi privati: implementazione interna --- */
    void                *_values_head;   /* lista valori durante l'inserimento */
    void                *_values_tail;
    struct group_entry  *_next;          /* concatenamento nel bucket */
} group_entry_t;

/* Crea una tabella vuota con il numero di bucket indicato.
 * Valori tipici: 1024 o 4096. Piu' alto = meno collisioni ma piu' RAM.
 * Ritorna NULL su errore (errno impostato). */
group_table_t *group_table_create(size_t num_buckets);

/* Distrugge la tabella e libera TUTTO cio' che essa possiede:
 * token, valori, entry, bucket array. */
void group_table_destroy(group_table_t *t);

/* Inserisce una coppia (token, valore) nella tabella.
 *
 * La tabella ESEGUE COPIA INTERNA sia del token (strdup) sia dei byte
 * del valore (memcpy). Il chiamante puo' liberare/riusare i propri
 * buffer subito dopo il ritorno.
 *
 * Se il token e' nuovo: viene creata una nuova entry.
 * Se il token esiste gia': il valore viene aggiunto in fondo alla
 * sua lista, preservando l'ordine di inserimento.
 *
 * Ritorna 0 in caso di successo, -1 in caso di errore (errno impostato). */
int group_table_insert(group_table_t *t,
                       const char *token, size_t token_len,
                       const void *value, size_t value_size);

/* Costruisce la "vista finale" della tabella, dopo l'ultimo insert.
 *
 * Per ogni entry, appiattisce la lista valori in un array contiguo
 * mr_value_t[] (riempie i campi pubblici values e values_count).
 * Poi raccoglie tutte le entry in un array, ordinato LESSICOGRAFICAMENTE
 * per token, e lo restituisce.
 *
 * Output (su successo):
 *   *out_entries: array malloc'd di puntatori a group_entry_t.
 *                 Il chiamante deve liberarlo con free().
 *                 Le entry puntate restano della tabella.
 *                 In caso di tabella vuota e' NULL.
 *   *out_count:   numero di entry distinte (puo' essere 0).
 *
 * Ritorna 0 su successo, -1 su errore (errno impostato).
 * Va chiamata UNA SOLA VOLTA, dopo che tutti gli insert sono terminati. */
int group_table_finalize(group_table_t *t,
                         group_entry_t ***out_entries,
                         size_t *out_count);

#endif
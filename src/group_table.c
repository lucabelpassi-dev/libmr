/* src/group_table.c
 *
 * Tabella di hash con catene lineari per il raggruppamento dei valori
 * per token nel processo reducer.
 *
 * Non thread-safe: il pattern d'uso è "un thread inserisce, chiama
 * finalize, e DOPO più thread leggono in sola lettura".
 *
 * Spec di riferimento: §6.2 (Processo reducer), §8 (Output deterministico).
 */

#include "group_table.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ===== Strutture interne ===== */

/* Nodo della lista valori durante l'inserimento. */
typedef struct value_node {
    void              *data;
    size_t             size;
    struct value_node *next;
} value_node_t;

struct group_table {
    group_entry_t **buckets;        /* array di puntatori a entry */
    size_t          num_buckets;
    size_t          num_entries;
};

/* ===== Funzione di hash (FNV-1a 32-bit) =====
 * Semplice, veloce, distribuzione decente per stringhe corte. */
/* Distribuisce uniformemente token alfanumerici sui bucket. */
static uint32_t fnv1a_hash(const char *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];  /* XOR del byte corrente */
        h *= 16777619u;         /* moltiplica per la costante FNV */
    }
    return h;
}

/* ===== Helper interni ===== */

/* Cerca un'entry con il token specificato nella catena di un bucket.
 * Ritorna il puntatore all'entry o NULL se non presente. */
static group_entry_t *find_in_chain(group_entry_t *head,
                                    const char *token, size_t token_len) {
    for (group_entry_t *e = head; e != NULL; e = e->_next) {
        if (e->token_len == token_len &&
            memcmp(e->token, token, token_len) == 0) {
            return e;
        }
    }
    return NULL;
}

/* Crea una nuova entry per il token (copia interna) e la inserisce
 * in testa al bucket. NON allinea ancora valori — quelli arrivano
 * con append_value. */
static group_entry_t *new_entry(group_table_t *t, size_t bucket,
                                const char *token, size_t token_len) {
    group_entry_t *e = calloc(1, sizeof(*e));
    if (e == NULL) return NULL;

    e->token = malloc(token_len + 1);
    if (e->token == NULL) { free(e); return NULL; }
    memcpy(e->token, token, token_len);
    e->token[token_len] = '\0';
    e->token_len = token_len;

    /* Inserzione in testa: O(1). L'ordine nei bucket non e' rilevante,
     * tanto poi sortiamo tutto alla finalize. */
    e->_next = t->buckets[bucket];
    t->buckets[bucket] = e;
    t->num_entries++;

    return e;
}

/* Accoda un nuovo valore (copia interna dei byte) alla lista
 * dell'entry. Mantiene l'ordine di inserimento. */
static int append_value(group_entry_t *e,
                        const void *value, size_t value_size) {
    value_node_t *node = malloc(sizeof(*node));
    if (node == NULL) return -1;

    if (value_size > 0) {
        node->data = malloc(value_size);
        if (node->data == NULL) { free(node); return -1; }
        memcpy(node->data, value, value_size);
    } else {
        node->data = NULL;
    }
    node->size = value_size;
    node->next = NULL;

    /* Aggancio in coda per preservare l'ordine di inserimento. */
    value_node_t *tail = (value_node_t *)e->_values_tail;
    if (tail == NULL) {
        e->_values_head = node;
    } else {
        tail->next = node;
    }
    e->_values_tail = node;
    e->values_count++;
    return 0;
}

/* Confronto fra puntatori a group_entry_t* per qsort.
 * Ordine lessicografico per token (entrambi NUL-terminati ASCII). */
static int compare_entry_ptrs(const void *a, const void *b) {
    const group_entry_t *ea = *(const group_entry_t * const *)a;
    const group_entry_t *eb = *(const group_entry_t * const *)b;
    return strcmp(ea->token, eb->token);
}

/* ===== API pubblica ===== */

group_table_t *group_table_create(size_t num_buckets) {
    if (num_buckets == 0) { errno = EINVAL; return NULL; }

    group_table_t *t = calloc(1, sizeof(*t));                       /* calloc per la struct */
    if (t == NULL) return NULL;

    t->buckets = calloc(num_buckets, sizeof(group_entry_t *));      /* calloc per i bucket, inizializzati a NULL */
    if (t->buckets == NULL) { free(t); return NULL; }

    t->num_buckets = num_buckets;
    t->num_entries = 0;
    return t;
}

void group_table_destroy(group_table_t *t) {
    if (t == NULL) return;

    /* Per ogni bucket, scorri la catena delle entry. */
    for (size_t i = 0; i < t->num_buckets; i++) {
        group_entry_t *e = t->buckets[i];
        while (e != NULL) {
            group_entry_t *next_e = e->_next;

            /* Libera i valori. Due casi a seconda di se finalize
             * è stata chiamata:
             *  - finalize chiamata: e->values è l'array; i suoi
             *    elementi puntano ai buffer dei valori.
             *  - finalize NON chiamata: i valori sono nella lista
             *    concatenata interna. */
            if (e->values != NULL) {
                /* Caso post-finalize. */
                for (size_t j = 0; j < e->values_count; j++) {
                    free((void *)e->values[j].data);
                }
                free(e->values);
            } else {
                /* Caso pre-finalize. */
                value_node_t *n = (value_node_t *)e->_values_head;
                while (n != NULL) {
                    value_node_t *next_n = n->next;
                    free(n->data);
                    free(n);
                    n = next_n;
                }
            }

            free(e->token);
            free(e);
            e = next_e;
        }
    }
    free(t->buckets);
    free(t);
}

int group_table_insert(group_table_t *t,
                       const char *token, size_t token_len,
                       const void *value, size_t value_size) {
    if (t == NULL || token == NULL || token_len == 0) {
        errno = EINVAL; return -1;
    }
    if (value == NULL && value_size > 0) {
        errno = EINVAL; return -1;
    }

    /* 1. Calcola l'indice del bucket */
    uint32_t h = fnv1a_hash(token, token_len);
    size_t bucket = (size_t)h % t->num_buckets;

    /* 2. Cerca l'entry esistente, o creane una nuova */
    group_entry_t *e = find_in_chain(t->buckets[bucket], token, token_len);
    if (e == NULL) {
        e = new_entry(t, bucket, token, token_len);
        if (e == NULL) return -1;
    }

    /* 3. Aggiungi il valore alla lista dell'entry */
    return append_value(e, value, value_size);
}

int group_table_finalize(group_table_t *t,
                         group_entry_t ***out_entries,
                         size_t *out_count) {
    if (t == NULL || out_entries == NULL || out_count == NULL) {
        errno = EINVAL; return -1;
    }

    *out_entries = NULL;
    *out_count   = 0;

    if (t->num_entries == 0) return 0;  /* tabella vuota: niente da fare */

    /* 1. Alloca l'array dei puntatori alle entry */
    group_entry_t **arr = malloc(t->num_entries * sizeof(group_entry_t *));
    if (arr == NULL) return -1;

    /* 2. Per ogni entry: "appiattisci" la lista valori in array
     *    contiguo, e raccogli il puntatore nell'array globale */
    size_t idx = 0;
    for (size_t i = 0; i < t->num_buckets; i++) {                           /* scorro i bucket */
        for (group_entry_t *e = t->buckets[i]; e != NULL; e = e->_next) {   /* scorro le entry nel bucket */

            /* Appiattimento: alloca array di values_count elementi,
             * trasferisci i puntatori data dai nodi della lista. */
            if (e->values_count > 0) {
                e->values = malloc(e->values_count * sizeof(mr_value_t));
                if (e->values == NULL) { free(arr); return -1; }

                value_node_t *n = (value_node_t *)e->_values_head;
                size_t k = 0;
                while (n != NULL) {
                    value_node_t *next_n = n->next;
                    /* Trasferimento ownership: l'array prende i byte
                     * direttamente dal nodo, niente memcpy. */
                    e->values[k].data = n->data;
                    e->values[k].size = n->size;
                    k++;
                    free(n);  /* libera il nodo, NON n->data (trasferito) */
                    n = next_n;
                }
                /* cancello esplicitamente head e tail, così distingui i casi in group_table_destroy */
                e->_values_head = NULL;
                e->_values_tail = NULL;
            }

            arr[idx++] = e; // alla fine, idx == t->num_entries 
        }
    }

    /* 3. Ordina l'array per token (lessicografico). O(NlogN), in place. */
    qsort(arr, t->num_entries, sizeof(group_entry_t *), compare_entry_ptrs);

    *out_entries = arr;
    *out_count   = t->num_entries;
    return 0;
}
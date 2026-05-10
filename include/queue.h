#ifndef QUEUE_H
#define QUEUE_H

#include <threads.h>
#include <stdbool.h>
#include "mr.h" // Ci serve per conoscere mr_value_t

// Nodo della lista concatenata
typedef struct q_node {
    mr_value_t data;
    struct q_node *next;
} q_node_t;

// La struttura della Coda Sincronizzata
typedef struct {
    q_node_t *head;
    q_node_t *tail;
    mtx_t mutex;       // Il lucchetto per l'accesso esclusivo
    cnd_t cond;        // La variabile di condizione per "svegliare" i worker
    bool is_closed;    // Flag vitale: dice ai worker se sono finiti i dati
} thread_queue_t;

// Funzioni pubbliche della coda
void queue_init(thread_queue_t *q);
void queue_push(thread_queue_t *q, mr_value_t value);
bool queue_pop(thread_queue_t *q, mr_value_t *out_value);
void queue_close(thread_queue_t *q);
void queue_destroy(thread_queue_t *q);

#endif // QUEUE_H
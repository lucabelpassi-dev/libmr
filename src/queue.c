#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

// Inizializza la coda vuota e crea i mutex/cond
int queue_init(thread_queue_t *q) {
    q->head = NULL;
    q->tail = NULL;
    q->is_closed = 0;

    if (mtx_init(&q->mutex, mtx_plain) != thrd_success) {
        return -1;
    }

    if (cnd_init(&q->cond) != thrd_success) {
        mtx_destroy(&q->mutex);   /* rollback: libero il mutex appena creato */
        return -1;
    }

    return 0;
}

// Il Produttore aggiunge un elemento alla coda
void queue_push(thread_queue_t *q, mr_value_t value) {
    q_node_t *new_node = malloc(sizeof(q_node_t));
    if (new_node == NULL) {
        perror("queue_push: malloc");
        exit(EXIT_FAILURE);
    }
    new_node->data = value;
    new_node->next = NULL;

    // chiudiamo il lucchetto! Nessun altro può toccare la coda ora.
    mtx_lock(&q->mutex);

    // inseriamo il nodo
    if (q->tail == NULL) { // Coda vuota
        q->head = new_node;
        q->tail = new_node;
    } else {
        q->tail->next = new_node;
        q->tail = new_node;
    }

    // svegliamo ALMENO UN thread worker che stava dormendo
    cnd_signal(&q->cond);

    // rilasciamo il lucchetto
    mtx_unlock(&q->mutex);
}

// Il Consumatore pesca un elemento. Ritorna 'false' se la coda è chiusa e vuota.
int queue_pop(thread_queue_t *q, mr_value_t *out_value) {
    mtx_lock(&q->mutex);

    // se la coda è vuota ma NON è chiusa, il worker si mette a dormire aspettando dati
    while (q->head == NULL && !q->is_closed) {
        cnd_wait(&q->cond, &q->mutex);
    }

    // se ci svegliamo e la coda è vuota E chiusa, significa che abbiamo finito il lavoro
    if (q->head == NULL && q->is_closed) {
        mtx_unlock(&q->mutex);
        return 0; 
    }

    // altrimenti, prendiamo l'elemento in cima (head)
    q_node_t *temp = q->head;
    *out_value = temp->data;
    
    q->head = q->head->next;
    if (q->head == NULL) {
        q->tail = NULL; // la coda è tornata vuota
    }

    mtx_unlock(&q->mutex);
    free(temp); // puliamo la memoria del nodo
    
    return 1; // abbiamo pescato con successo
}

// Chiude la coda (non accetta più dati) e sveglia tutti i worker addormentati
void queue_close(thread_queue_t *q) {
    mtx_lock(&q->mutex);
    q->is_closed = 1;
    cnd_broadcast(&q->cond); // sveglia tutti
    mtx_unlock(&q->mutex);
}

// Distrugge i lucchetti (da chiamare alla fine di tutto)
void queue_destroy(thread_queue_t *q) {
    /* Drena eventuali nodi residui: libera sia il buffer opaco che il nodo */
    q_node_t *cur = q->head;
    while (cur != NULL) {
        q_node_t *next = cur->next;
        free((void *)cur->data.data);  /* cast: la queue e' proprietaria del buffer */
        free(cur);
        cur = next;
    }
    q->head = q->tail = NULL;
    mtx_destroy(&q->mutex);
    cnd_destroy(&q->cond);
}

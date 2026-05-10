#include <stdlib.h>
#include "queue.h"

// Inizializza la coda vuota e crea i mutex/cond
void queue_init(thread_queue_t *q) {
    q->head = NULL;
    q->tail = NULL;
    q->is_closed = false;
    mtx_init(&q->mutex, mtx_plain);
    cnd_init(&q->cond);
}

// Il Produttore aggiunge un elemento alla coda
void queue_push(thread_queue_t *q, mr_value_t value) {
    // 1. Creiamo il nuovo nodo
    q_node_t *new_node = malloc(sizeof(q_node_t));
    new_node->data = value;
    new_node->next = NULL;

    // 2. Chiudiamo il lucchetto! Nessun altro può toccare la coda ora.
    mtx_lock(&q->mutex);

    // 3. Inseriamo il nodo
    if (q->tail == NULL) { // Coda vuota
        q->head = new_node;
        q->tail = new_node;
    } else {
        q->tail->next = new_node;
        q->tail = new_node;
    }

    // 4. Svegliamo ALMENO UN thread worker che stava dormendo
    cnd_signal(&q->cond);

    // 5. Rilasciamo il lucchetto
    mtx_unlock(&q->mutex);
}

// Il Consumatore pesca un elemento. Ritorna 'false' se la coda è chiusa e vuota.
bool queue_pop(thread_queue_t *q, mr_value_t *out_value) {
    mtx_lock(&q->mutex);

    // Se la coda è vuota ma NON è chiusa, il worker si mette a dormire aspettando dati
    while (q->head == NULL && !q->is_closed) {
        cnd_wait(&q->cond, &q->mutex);
    }

    // Se ci svegliamo e la coda è vuota E chiusa, significa che abbiamo finito il lavoro
    if (q->head == NULL && q->is_closed) {
        mtx_unlock(&q->mutex);
        return false; 
    }

    // Altrimenti, prendiamo l'elemento in cima (head)
    q_node_t *temp = q->head;
    *out_value = temp->data;
    
    q->head = q->head->next;
    if (q->head == NULL) {
        q->tail = NULL; // La coda è tornata vuota
    }

    mtx_unlock(&q->mutex);
    free(temp); // Puliamo la memoria del nodo
    
    return true; // Abbiamo pescato con successo
}

// Chiude la coda (non accetta più dati) e sveglia tutti i worker addormentati
void queue_close(thread_queue_t *q) {
    mtx_lock(&q->mutex);
    q->is_closed = true;
    cnd_broadcast(&q->cond); // SVEGLIA TUTTI! È l'ora di chiudere.
    mtx_unlock(&q->mutex);
}

// Distrugge i lucchetti (da chiamare alla fine di tutto)
void queue_destroy(thread_queue_t *q) {
    mtx_destroy(&q->mutex);
    cnd_destroy(&q->cond);
}
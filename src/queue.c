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
    q_node_t *new_node = malloc(sizeof(q_node_t));
    if (new_node == NULL) {
        perror("queue_push: malloc");
        exit(EXIT_FAILURE);
    }
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
    /* Drena eventuali nodi residui: libera sia il buffer opaco che il nodo */
    q_node_t *cur = q->head;
    while (cur != NULL) {
        q_node_t *next = cur->next;
        free(cur->data.data);  /* il buffer opaco dentro mr_value_t */
        free(cur);
        cur = next;
    }
    q->head = q->tail = NULL;
    mtx_destroy(&q->mutex);
    cnd_destroy(&q->cond);
}

// destroy importante
/*
    la coda è un trasportatore. il produttore alloca il buffer, lo "regala" alla coda con push, 
    il consumatore lo "ritira" con pop e diventa responsabile di liberarlo.
    In condizioni normali la coda è vuota quando viene distrutta — perfetto.
    Ma se per qualche motivo viene distrutta non-vuota (terminazione anomala, errore, segnale),
    nessuno libera quei buffer abbandonati. Il destroy deve quindi farsi carico della pulizia dei
    "residui": questa è la regola "chi possiede, libera" applicata in modo difensivo.
    Se invece il prof ti chiede "e i buffer dentro mr_value_t chi li libera in condizioni normali?",
    la risposta è: il consumatore dopo aver chiamato pop, non la coda. La coda li libera solo come
    fail-safe in destroy.
 */
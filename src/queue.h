#ifndef QUEUE_H
#define QUEUE_H

#include <threads.h>
#include "mr.h" 

/* Nodo della lista concatenata */
typedef struct q_node {
    mr_value_t data;        /* dato opaco da processare (token, chiave, valore, ecc.) */ 
    struct q_node *next;    /* puntatore al nodo successivo */
} q_node_t;

/* Struttura della Coda Sincronizzata */
typedef struct {
    q_node_t *head;     /* punta al primo nodo (il più vecchio) */
    q_node_t *tail;     /* punta all'ultimo nodo (il più nuovo). è dove inseriremo i nuovi dati, ci serve per evitare di scorrere tutta la lista ogni volta che vogliamo pushare un nuovo elemento */
    mtx_t mutex;        /* il lucchetto per l'accesso esclusivo */
    cnd_t cond;         /* la variabile di condizione per "svegliare" i worker o farli dormire se non c'è lavoro da fare */
    int is_closed;      /* flag vitale: dice ai worker se sono finiti i dati (possono smettere di lavorare!). il produttore chiama queue_close(), imposta questo int a 1 (true), lancia un cnd_broadcast() per svegliare tutti i worker, e quando i worker si svegliano, se vedono che is_closed è true e la coda è vuota, capiscono che è finito il lavoro e possono uscire */
} thread_queue_t;

/* --- Funzioni pubbliche della coda --- */

int queue_init(thread_queue_t *q);                      /* prepara il terreno: azzera i puntatori e inizializza i mutex e le condition variables */
void queue_push(thread_queue_t *q, mr_value_t value);    /* pusha un dato in fondo alla coda e e fa cnd_signal per svegliare almeno un worker che stava dormendo */
int queue_pop(thread_queue_t *q, mr_value_t *out_value); /* tenta di prendere un dato. se non c'è, dorme. se la coda è chiusa, ritorna 0 (false) */
void queue_close(thread_queue_t *q);                     /* chiude la coda: imposta is_closed a true, e lancia un cnd_broadcast() per svegliare tutti i worker che stanno dormendo, così vedono che is_closed è true, capiscono che è finito il lavoro e possono uscire */
void queue_destroy(thread_queue_t *q);                   /* pulizia: distrugge mutex e condition variables. */

#endif 
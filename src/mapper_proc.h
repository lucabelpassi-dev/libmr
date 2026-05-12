#ifndef MAPPER_PROC_H
#define MAPPER_PROC_H

#include "mr.h"

/* Contesto del processo mapper: tutto cio' che la pipeline interna
 * del mapper ha bisogno di sapere dal padre. Allocato sullo stack
 * nel padre prima di fork() (quindi inerito dal figlio per copia). */
struct mapper_ctx {
    mr_mapper_t mapper;     /* callback dell'utente */
    void       *user_arg;   /* opaco, passato alla callback */
    size_t      num_threads;
    size_t      queue_size;
    /* TODO passata 2: int log_fd; sem_t *log_sem; */
};

/*
 * Punto di ingresso del processo mapper.
 *
 * stdin  = pipe dal processo principale (righe serializzate)
 * stdout = pipe verso il processo reducer (coppie serializzate)
 *
 * Passata 1: stub che drena stdin e termina.
 * Passata 2: 1 thread lettore + ctx->num_threads worker, coda interna,
 *            invocazione di ctx->mapper per ogni riga, emit verso stdout.
 *
 * Ritorna 0 in caso di successo.
 */
int mapper_process_main(struct mapper_ctx *ctx);

#endif
#ifndef MAPPER_PROC_H
#define MAPPER_PROC_H

#include "mr.h"

/* Contesto del processo mapper: tutto cio' che la pipeline interna
 * del mapper ha bisogno di sapere dal padre. Allocato sullo stack
 * nel padre prima di fork() (quindi inerito dal figlio per copia). */
struct mapper_ctx {
    mr_mapper_t mapper;         /* callback dell'utente */
    void       *user_arg;       /* opaco, passato alla callback */
    size_t      num_threads;
    size_t      queue_size;
};

int mapper_process_main(struct mapper_ctx *ctx);

#endif
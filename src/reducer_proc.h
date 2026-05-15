/* reducer_proc.h */
#ifndef REDUCER_PROC_H
#define REDUCER_PROC_H

#include "mr.h"

struct reducer_ctx {
    mr_reducer_t reducer;
    void        *user_arg;
    size_t       num_threads;
    size_t       queue_size;
};

int reducer_process_main(struct reducer_ctx *ctx);

#endif
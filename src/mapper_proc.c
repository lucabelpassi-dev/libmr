/* src/mapper_proc.c
 *
 * Processo mapper: 1 thread lettore + N thread worker + 1 coda interna.
 * Spec di riferimento: §6.1.
 */

#include "mapper_proc.h"
#include "queue.h"
#include "protocol.h"
#include "thrd_utils.h"
#include "log.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

struct mapper_state {
    thread_queue_t *queue;
    mr_mapper_t     mapper_fn;
    void           *user_arg;
    int             reducer_fd;
    mtx_t           out_mutex;
    atomic_size_t   pairs_count;
};

struct worker_args {
    struct mapper_state *state;
    int id;
};

static inline int is_token_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9');
}

static int validate_token(const char *token, size_t *out_len) {
    if (token == NULL) return -1;
    size_t len = 0;
    while (token[len] != '\0') {
        if (!is_token_char((unsigned char)token[len])) return -1;
        len++;
    }
    if (len == 0) return -1;
    *out_len = len;
    return 0;
}

static int mapper_emit(const char *token,
                       const void *value, size_t value_size,
                       void *emit_arg) {
    struct mapper_state *st = (struct mapper_state *)emit_arg;

    size_t token_len;
    if (validate_token(token, &token_len) == -1) {
        errno = EINVAL;
        return -1;
    }

    /* FIX BUG A: rimossa la fprintf di debug. */

    if (mtx_lock(&st->out_mutex) != thrd_success) {
        errno = EAGAIN;
        return -1;
    }

    int rc = protocol_write_pair(st->reducer_fd,
                                 token, token_len,
                                 value, value_size);

    mtx_unlock(&st->out_mutex);

    if (rc == 0) atomic_fetch_add(&st->pairs_count, 1);
    return rc;
}

static int mapper_worker_main(void *arg) {
    struct worker_args *a   = (struct worker_args *)arg;
    struct mapper_state *st = a->state;

    char thread_name[16];
    snprintf(thread_name, sizeof thread_name, "worker-%d", a->id);
    log_set_thread_name(thread_name);
    log_event("THREAD_START", NULL);

    int my_ret = 0;
    mr_value_t item;
    while (queue_pop(st->queue, &item)) {
        mr_line_msg_t *msg = (mr_line_msg_t *)item.data;

        mr_file_line_t line = {
            .file_name     = msg->file_name,
            .file_name_len = msg->file_name_len,
            .line_number   = msg->line_number,
            .line          = msg->line,
            .line_len      = msg->line_len,
        };

        int rc = st->mapper_fn(&line, mapper_emit, st, st->user_arg);

        protocol_line_msg_free(msg);
        free(msg);

        if (rc != 0) { my_ret = 1; break; }
    }

    log_event("THREAD_END", "rc=%d", my_ret);
    return my_ret;
}

static int mapper_reader_main(void *arg) {
    struct mapper_state *st = (struct mapper_state *)arg;
    int ret = 0;

    log_set_thread_name("reader");
    log_event("THREAD_START", NULL);

    for (;;) {
        mr_line_msg_t *msg = calloc(1, sizeof(*msg));
        if (msg == NULL) { ret = 1; break; }

        int r = protocol_read_line(STDIN_FILENO, msg);
        if (r == 0)  { free(msg); break; }
        if (r == -1) { free(msg); ret = 1; break; }

        mr_value_t item = { .data = msg, .size = sizeof(*msg) };
        queue_push(st->queue, item);
    }

    queue_close(st->queue);
    log_event("THREAD_END", "rc=%d", ret);
    return ret;
}

int mapper_process_main(struct mapper_ctx *ctx) {
    if (ctx == NULL || ctx->mapper == NULL || ctx->num_threads == 0) {
        return 1;
    }

    int ret = 0;

    /* === FIX BUG B: pre-alloca workers E wargs PRIMA di partire con
     * i thread. Se uno dei due calloc fallisce, non ci sono thread
     * in giro da gestire. === */
    thrd_t *workers = calloc(ctx->num_threads, sizeof(thrd_t));
    struct worker_args *wargs = calloc(ctx->num_threads, sizeof(*wargs));
    if (workers == NULL || wargs == NULL) {
        free(workers); free(wargs);
        return 1;
    }

    thread_queue_t queue;
    queue_init(&queue);

    struct mapper_state state = {
        .queue      = &queue,
        .mapper_fn  = ctx->mapper,
        .user_arg   = ctx->user_arg,
        .reducer_fd = STDOUT_FILENO,
    };
    if (mtx_init(&state.out_mutex, mtx_plain) != thrd_success) {
        free(workers); free(wargs);
        queue_destroy(&queue);
        return 1;
    }
    atomic_init(&state.pairs_count, 0);

    /* Avvio dei thread */
    thrd_t reader;
    THRD_CHECK(thrd_create(&reader, mapper_reader_main, &state),
               "mapper: thrd_create(reader)");

    for (size_t i = 0; i < ctx->num_threads; i++) {
        wargs[i].state = &state;
        wargs[i].id    = (int)i;
        THRD_CHECK(thrd_create(&workers[i], mapper_worker_main, &wargs[i]),
                   "mapper: thrd_create(worker)");
    }

    /* Join: prima reader (chiude la coda), poi i worker */
    int reader_ret = 0;
    thrd_join(reader, &reader_ret);
    if (reader_ret != 0) ret = 1;

    for (size_t i = 0; i < ctx->num_threads; i++) {
        int worker_ret = 0;
        thrd_join(workers[i], &worker_ret);
        if (worker_ret != 0) ret = 1;
    }

    /* Drain difensivo (di norma la coda e' gia' vuota) */
    {
        mr_value_t item;
        while (queue_pop(&queue, &item)) {
            mr_line_msg_t *msg = (mr_line_msg_t *)item.data;
            protocol_line_msg_free(msg);
            free(msg);
        }
    }

    log_event("PAIRS_EMITTED", "count=%zu", atomic_load(&state.pairs_count));

    free(workers);
    free(wargs);
    mtx_destroy(&state.out_mutex);
    queue_destroy(&queue);

    return ret;
}
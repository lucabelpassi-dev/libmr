/* src/reducer_proc.c
 *
 * Processo reducer in pipeline a tre fasi:
 *   FASE 1: il thread lettore legge le coppie e le inserisce in group_table.
 *           A EOF, finalize della tabella (sort lessicografico).
 *   FASE 2: i thread worker prendono entry tramite contatore atomico,
 *           invocano la callback utente, accumulano i risultati serializzati
 *           in buffer per-entry.
 *   FASE 3: il main thread flusha i buffer in ordine lessicografico
 *           sulla pipe verso il processo principale.
 *
 * Spec di riferimento: §6.2, §8.
 */

#include "reducer_proc.h"
#include "group_table.h"
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

struct reducer_state {
    group_table_t  *table;

    group_entry_t **entries;
    size_t          entries_count;

    char  **out_buffers; // out_buffers[i] = puntatore al buffer del gruppo i
    size_t *out_buffer_lens; // out_buffer_lens[i] = quanti byte ha scritto finora il worker
    size_t *out_buffer_caps; // out_buffer_caps[i] = capienza attuale del buffer

    atomic_size_t next_entry_idx;
    atomic_size_t results_count;     

    mr_reducer_t reducer_fn;
    void        *user_arg;
};

struct worker_args {
    struct reducer_state *state;
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

struct reducer_emit_ctx {
    char   **buf_p;
    size_t  *len_p;
    size_t  *cap_p;
    atomic_size_t *results_counter;   /* puntatore al contatore globale */
};

static int reducer_emit(const char *token,
                        const void *result, size_t result_size,
                        void *emit_arg) {
    struct reducer_emit_ctx *ec = (struct reducer_emit_ctx *)emit_arg;

    size_t token_len;
    if (validate_token(token, &token_len) == -1) {
        errno = EINVAL;
        return -1;
    }

    mr_result_header_t hdr = {
        .token_len  = (int)token_len,
        .result_len = (int)result_size,
    };
    size_t total = sizeof(hdr) + token_len + result_size;

    if (*ec->len_p + total > *ec->cap_p) {
        size_t new_cap = (*ec->cap_p == 0) ? 256 : *ec->cap_p * 2;
        while (new_cap < *ec->len_p + total) new_cap *= 2;
        char *new_buf = realloc(*ec->buf_p, new_cap);
        if (new_buf == NULL) return -1;
        *ec->buf_p = new_buf;
        *ec->cap_p = new_cap;
    }

    char *dst = *ec->buf_p + *ec->len_p;
    memcpy(dst, &hdr, sizeof hdr);                       dst += sizeof hdr;
    memcpy(dst, token, token_len);                       dst += token_len;
    if (result_size > 0) memcpy(dst, result, result_size);
    *ec->len_p += total;

    atomic_fetch_add(ec->results_counter, 1);
    return 0;
}

static int reducer_reader_main(void *arg) {
    struct reducer_state *st = (struct reducer_state *)arg;
    int ret = 0;

    log_set_thread_name("reader");
    log_event("THREAD_START", NULL);

    for (;;) {
        mr_pair_msg_t pair;
        int r = protocol_read_pair(STDIN_FILENO, &pair);
        if (r == 0) break;
        if (r == -1) { ret = 1; break; }

        int ins = group_table_insert(st->table,
                                     pair.token, pair.token_len,
                                     pair.value, pair.value_size);
        protocol_pair_msg_free(&pair);
        if (ins == -1) { ret = 1; break; }
    }

    log_event("THREAD_END", "rc=%d", ret);
    return ret;
}

static int reducer_worker_main(void *arg) {
    struct worker_args  *a  = (struct worker_args *)arg;
    struct reducer_state *st = a->state;

    char thread_name[16];
    snprintf(thread_name, sizeof thread_name, "worker-%d", a->id);
    log_set_thread_name(thread_name);
    log_event("THREAD_START", NULL);

    int my_ret = 0;
    for (;;) {
        size_t i = atomic_fetch_add(&st->next_entry_idx, 1);
        if (i >= st->entries_count) break;

        group_entry_t *entry = st->entries[i];

        struct reducer_emit_ctx ec = {
            .buf_p           = &st->out_buffers[i],
            .len_p           = &st->out_buffer_lens[i],
            .cap_p           = &st->out_buffer_caps[i],
            .results_counter = &st->results_count,
        };

        int rc = st->reducer_fn(entry->token,
                                entry->values, entry->values_count,
                                reducer_emit, &ec,
                                st->user_arg);
        if (rc != 0) { my_ret = 1; break; }
    }

    log_event("THREAD_END", "rc=%d", my_ret);
    return my_ret;
}

int reducer_process_main(struct reducer_ctx *ctx) {
    if (ctx == NULL || ctx->reducer == NULL || ctx->num_threads == 0) {
        return 1;
    }

    int ret = 0;

    struct reducer_state state = {
        .reducer_fn = ctx->reducer,
        .user_arg   = ctx->user_arg,
    };
    atomic_init(&state.next_entry_idx, 0);
    atomic_init(&state.results_count,  0);

    state.table = group_table_create(1024);
    if (state.table == NULL) return 1;

    /* === FASE 1: reader === */
    thrd_t reader;
    THRD_CHECK(thrd_create(&reader, reducer_reader_main, &state),
               "reducer: thrd_create(reader)");
    int reader_ret = 0;
    thrd_join(reader, &reader_ret);
    if (reader_ret != 0) { ret = 1; goto cleanup; }

    /* === Finalize: appiattisce i valori e ordina === */
    if (group_table_finalize(state.table,
                             &state.entries,
                             &state.entries_count) == -1) {
        ret = 1; goto cleanup;
    }

    log_event("TOKENS_GROUPED", "count=%zu", state.entries_count);

    /* === FASE 2: worker (solo se ci sono entry) === */
    if (state.entries_count > 0) {
        state.out_buffers     = calloc(state.entries_count, sizeof(char *));
        state.out_buffer_lens = calloc(state.entries_count, sizeof(size_t));
        state.out_buffer_caps = calloc(state.entries_count, sizeof(size_t));
        if (state.out_buffers == NULL ||
            state.out_buffer_lens == NULL ||
            state.out_buffer_caps == NULL) {
            ret = 1; goto cleanup;
        }

        thrd_t *workers = calloc(ctx->num_threads, sizeof(thrd_t));
        struct worker_args *wargs = calloc(ctx->num_threads, sizeof(*wargs));
        if (workers == NULL || wargs == NULL) {
            free(workers); free(wargs);
            ret = 1; goto cleanup;
        }

        for (size_t i = 0; i < ctx->num_threads; i++) {
            wargs[i].state = &state;
            wargs[i].id    = (int)i;
            THRD_CHECK(thrd_create(&workers[i], reducer_worker_main, &wargs[i]),
                       "reducer: thrd_create(worker)");
        }
        for (size_t i = 0; i < ctx->num_threads; i++) {
            int wret = 0;
            thrd_join(workers[i], &wret);
            if (wret != 0) ret = 1;
        }
        free(workers);
        free(wargs);

        /* === FASE 3: flush ordinato === */
        if (ret == 0) {
            for (size_t i = 0; i < state.entries_count; i++) {
                if (state.out_buffer_lens[i] > 0) {
                    if (writen(STDOUT_FILENO,
                               state.out_buffers[i],
                               state.out_buffer_lens[i]) == -1) {
                        ret = 1;
                        break;
                    }
                }
            }
        }
    }

    log_event("RESULTS_EMITTED", "count=%zu", atomic_load(&state.results_count));

cleanup:
    if (state.out_buffers != NULL) {
        for (size_t i = 0; i < state.entries_count; i++) {
            free(state.out_buffers[i]);
        }
        free(state.out_buffers);
    }
    free(state.out_buffer_lens);
    free(state.out_buffer_caps);
    free(state.entries);
    group_table_destroy(state.table);
    return ret;
}
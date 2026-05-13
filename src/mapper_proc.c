/* src/mapper_proc.c
 *
 * Processo mapper: 1 thread lettore + N thread worker + 1 coda interna.
 * Spec di riferimento: §6.1.
 */

#include "mapper_proc.h"
#include "queue.h"
#include "protocol.h"
#include "thrd_utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

/* ===== Stato condiviso fra tutti i thread del processo mapper =====
 * Allocato sullo stack di mapper_process_main; ogni thread riceve il
 * suo indirizzo come argomento void*. */
struct mapper_state {
    /* Per reader e worker */
    thread_queue_t *queue;

    /* Per i worker */
    mr_mapper_t mapper_fn;     /* callback dell'utente */
    void       *user_arg;      /* argomento opaco utente */
    int         reducer_fd;    /* normalmente STDOUT_FILENO */
    mtx_t       out_mutex;     /* serializza le emit() concorrenti */
};

/* ===== Validazione del token (specifica §3, §10) =====
 * Il token deve essere una stringa C non vuota composta solo da
 * caratteri alfanumerici ASCII [A-Za-z0-9]. */

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
    if (len == 0) return -1;  /* token vuoto non ammesso */
    *out_len = len;
    return 0;
}

/* ===== FUNZIONE EMIT =====
 * E' quella che il framework "presta" alla callback mapper dell'utente.
 * L'utente la chiama come:  emit(token, value, value_size, emit_arg);
 * dove emit_arg e' il puntatore opaco passatogli (in realta' un
 * mapper_state* che noi conosciamo). */
static int mapper_emit(const char *token,
                       const void *value, size_t value_size,
                       void *emit_arg) {
    struct mapper_state *st = (struct mapper_state *)emit_arg;

    /* Valida il token: se non rispetta il contratto, fallisce. */
    size_t token_len;
    if (validate_token(token, &token_len) == -1) {
        errno = EINVAL;
        return -1;
    }

    fprintf(stderr, "[mapper-emit] tok=\"%s\" val_sz=%zu\n", token, value_size);
    /* Sezione critica: tutta l'emit (3 write su pipe) deve essere
     * indivisibile rispetto agli altri worker. */
    if (mtx_lock(&st->out_mutex) != thrd_success) {
        errno = EAGAIN;
        return -1;
    }

    int rc = protocol_write_pair(st->reducer_fd,
                                 token, token_len,
                                 value, value_size);

    mtx_unlock(&st->out_mutex);

    return rc;
}

/* ===== THREAD WORKER =====
 * Pesca dalla coda, costruisce il mr_file_line_t "prestato",
 * invoca la callback utente, libera il messaggio.
 * Esce quando la coda e' vuota e chiusa. */
static int mapper_worker_main(void *arg) {
    struct mapper_state *st = (struct mapper_state *)arg;

    for (;;) {
        mr_value_t item;
        if (!queue_pop(st->queue, &item)) {
            /* Coda vuota e chiusa: nessun lavoro residuo. */
            break;
        }

        mr_line_msg_t *msg = (mr_line_msg_t *)item.data;

        /* Riga "in prestito" all'utente: i puntatori sono validi SOLO
         * per la durata della chiamata sotto. Documentato in §A.1
         * della specifica. */
        mr_file_line_t line = {
            .file_name     = msg->file_name,
            .file_name_len = msg->file_name_len,
            .line_number   = msg->line_number,
            .line          = msg->line,
            .line_len      = msg->line_len,
        };

        /* Invoca la callback dell'utente. emit_arg = st permette alla
         * mapper di richiamare emit, che sapra' come accedere al
         * contesto. */
        int rc = st->mapper_fn(&line, mapper_emit, st, st->user_arg);

        /* Il worker e' proprietario del messaggio dopo il pop:
         * libera il payload e poi la struct. */
        protocol_line_msg_free(msg);
        free(msg);

        if (rc != 0) return 1;  /* l'utente ha segnalato errore */
    }

    return 0;
}

/* ===== THREAD LETTORE =====
 * Identico allo step 1, ma ho RIMOSSO la stampa di debug su stderr
 * (ora che i worker fanno qualcosa, la verifica avviene altrove). */
static int mapper_reader_main(void *arg) {
    struct mapper_state *st = (struct mapper_state *)arg;
    int ret = 0;

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
    return ret;
}

/* ===== PUNTO DI INGRESSO DEL PROCESSO MAPPER ===== */
int mapper_process_main(struct mapper_ctx *ctx) {
    if (ctx == NULL || ctx->mapper == NULL || ctx->num_threads == 0) {
        return 1;
    }

    int ret = 0;

    /* --- Inizializzazione coda e stato condiviso --- */
    thread_queue_t queue;
    queue_init(&queue);

    struct mapper_state state = {
        .queue      = &queue,
        .mapper_fn  = ctx->mapper,
        .user_arg   = ctx->user_arg,
        .reducer_fd = STDOUT_FILENO,
    };
    if (mtx_init(&state.out_mutex, mtx_plain) != thrd_success) {
        queue_destroy(&queue);
        return 1;
    }

    /* --- Avvio dei thread --- */
    thrd_t reader;
    THRD_CHECK(thrd_create(&reader, mapper_reader_main, &state),
               "mapper: thrd_create(reader)");

    thrd_t *workers = calloc(ctx->num_threads, sizeof(thrd_t));
    if (workers == NULL) {
        thrd_join(reader, NULL);
        mtx_destroy(&state.out_mutex);
        queue_destroy(&queue);
        return 1;
    }
    for (size_t i = 0; i < ctx->num_threads; i++) {
        THRD_CHECK(thrd_create(&workers[i], mapper_worker_main, &state),
                   "mapper: thrd_create(worker)");
    }

    /* --- Join: prima il reader (chiude la coda), poi i worker
     *     (che svuotano e poi escono). --- */
    int reader_ret = 0;
    thrd_join(reader, &reader_ret);
    if (reader_ret != 0) ret = 1;

    for (size_t i = 0; i < ctx->num_threads; i++) {
        int worker_ret = 0;
        thrd_join(workers[i], &worker_ret);
        if (worker_ret != 0) ret = 1;
    }
    free(workers);

    /* --- Drain difensivo: in pratica la coda e' gia' vuota perche'
     *     i worker l'hanno svuotata. Se per bug fosse rimasto qualcosa,
     *     lo liberiamo per non leakare. --- */
    {
        mr_value_t item;
        while (queue_pop(&queue, &item)) {
            mr_line_msg_t *msg = (mr_line_msg_t *)item.data;
            protocol_line_msg_free(msg);
            free(msg);
        }
    }

    mtx_destroy(&state.out_mutex);
    queue_destroy(&queue);

    return ret;
}
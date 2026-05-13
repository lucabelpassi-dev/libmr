/* src/mapper_proc.c — Step 1: thread lettore.
 *
 * Vedi mapper_proc.h per il tipo struct mapper_ctx (contesto passato
 * dal processo principale tramite fork).
 * Spec di riferimento: §6.1 (Processo mapper).
 */

#include "mapper_proc.h"
#include "queue.h"
#include "protocol.h"
#include "thrd_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <threads.h>

/* ===== Stato condiviso fra i thread del processo mapper =====
 * Allocato sullo stack in mapper_process_main, il suo indirizzo viene
 * passato a ciascun thread tramite l'argomento void* di thrd_create. */
struct mapper_state {
    thread_queue_t *queue;
};

/* ===== THREAD LETTORE =====
 *
 * Compito unico: leggere messaggi-Riga dalla pipe in arrivo dal padre,
 * uno alla volta, e metterli nella coda condivisa.
 *
 * Quando il padre chiude il suo lato di scrittura della pipe,
 * protocol_read_line() vede EOF e ritorna 0: a quel punto il lettore
 * chiude la coda (cartello "STOP") e termina.
 *
 * Ritorna 0 = tutto ok, 1 = errore di lettura/protocollo.
 */
static int mapper_reader_main(void *arg) {
    struct mapper_state *st = (struct mapper_state *)arg;
    int ret = 0;

    for (;;) {
        /* 1. Alloca una nuova struct per il prossimo messaggio.
         *    L'idea e': il lettore alloca, la mette in coda; il worker
         *    la riceve, la usa, la libera. Ownership chiara. */
        mr_line_msg_t *msg = calloc(1, sizeof(*msg));
        if (msg == NULL) {
            ret = 1;
            break;
        }

        /* 2. Leggi un messaggio intero (intestazione + payload).
         *    Tre esiti possibili: */
        int r = protocol_read_line(STDIN_FILENO, msg);

        if (r == 0) {
            /* EOF pulito: il padre ha finito di mandare righe.
             * Il msg non e' stato compilato, lo buttiamo via. */
            free(msg);
            break;
        }
        if (r == -1) {
            /* Errore: protocol_read_line ha gia' liberato i suoi
             * eventuali buffer interni; ci basta liberare la struct. */
            free(msg);
            ret = 1;
            break;
        }

        /* 3. Successo: msg contiene file_name, line, line_number, lunghezze.
         *    Lo avvolgiamo in mr_value_t e lo mettiamo in coda.
         *    NOTA: passiamo il PUNTATORE alla struct, non la copia.
         *    Il consumatore (worker) ricevera' lo stesso puntatore. */
        mr_value_t item = { .data = msg, .size = sizeof(*msg) };
        queue_push(st->queue, item);

        /* === DEBUG TEMPORANEO ===
         * Stampa su stderr ogni riga letta per verificare visivamente
         * che il protocollo funzioni. Rimuovere nello step 2 quando
         * arriveranno i worker. */
        fprintf(stderr, "[mapper-reader] %s:%lu (%zu byte) \"%.*s\"\n",
                msg->file_name, msg->line_number, msg->line_len,
                (int)msg->line_len, msg->line);
    }

    /* 4. Cartello "STOP" sulla coda: i worker (quando ci saranno)
     *    vedranno is_closed=true e usciranno appena la coda e' vuota.
     *    Va chiamato SEMPRE, sia in caso di EOF normale sia in caso
     *    di errore: altrimenti i worker resterebbero a dormire per
     *    sempre. */
    queue_close(st->queue);
    return ret;
}

/* ===== PUNTO DI INGRESSO DEL PROCESSO MAPPER =====
 * Allocato e inizializzato lo stato, avvia il lettore, aspetta che
 * finisca, pulisce. */
int mapper_process_main(struct mapper_ctx *ctx) {
    int ret = 0;
    (void)ctx;  /* ctx->num_threads, ctx->mapper non ancora usati */

    /* 1. Inizializza la coda condivisa. */
    thread_queue_t queue;
    queue_init(&queue);

    /* 2. Prepara lo stato condiviso. */
    struct mapper_state state = { .queue = &queue };

    /* 3. Avvia il thread lettore. */
    thrd_t reader;
    THRD_CHECK(thrd_create(&reader, mapper_reader_main, &state),
               "mapper: thrd_create(reader)");

    /* TODO step 2: qui avvieremo i ctx->num_threads thread worker. */

    /* 4. Aspetta che il lettore termini.
     *    thrd_join() blocca finche' il thread indicato non e' uscito,
     *    e ci consegna il suo return value. */
    int reader_ret = 0;
    THRD_CHECK(thrd_join(reader, &reader_ret),
               "mapper: thrd_join(reader)");
    if (reader_ret != 0) ret = 1;

    /* TODO step 2: qui faremo thrd_join sui worker. */

    /* 5. Drena eventuali residui della coda.
     *    NEL FUTURO (con i worker), la coda dovrebbe essere gia' vuota
     *    a questo punto. Adesso, senza worker, contiene tutti i
     *    messaggi letti: li liberiamo qui per non far leakare niente.
     *    queue_pop() ritorna false quando la coda e' vuota E chiusa,
     *    cosa che e' garantita perche' il lettore ha chiamato
     *    queue_close() prima di uscire. */
    {
        mr_value_t item;
        while (queue_pop(&queue, &item)) {
            mr_line_msg_t *msg = (mr_line_msg_t *)item.data;
            protocol_line_msg_free(msg);
            free(msg);
        }
    }

    queue_destroy(&queue);
    return ret;
}
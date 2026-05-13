/* src/mr.c
 *
 * Cuore del framework: lifecycle (mr_create / mr_destroy) e pipeline
 * a tre processi (mr_start).
 *
 * Vedi mr.h per le firme pubbliche e le specifiche §5 e §5.1 per il
 * modello di processi e la propagazione dell'EOF.
 */

#define _POSIX_C_SOURCE 200809L

#include "mr.h"
#include "mapper_proc.h"
#include "reducer_proc.h"
#include "file_input.h"
#include "protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ----- Definizione "privata" del tipo opaco esposto in mr.h ----- */
struct mr {
    mr_attr_t    attrs;
    mr_mapper_t  mapper;
    mr_reducer_t reducer;
    void        *user_arg;
};

/* ===== Helper interno =====
 * Copia profonda di mr_attr_t: i campi scalari sono copiati per valore,
 * log_file e' duplicato con strdup() in modo che la mr_t sia
 * indipendente dall'attr passato dall'utente. */
static int copy_attrs(mr_attr_t *dst, const mr_attr_t *src) {
    dst->mapper_threads  = src->mapper_threads;
    dst->reducer_threads = src->reducer_threads;
    dst->queue_size      = src->queue_size;
    if (src->log_file != NULL) {
        char *copy = strdup(src->log_file);
        if (copy == NULL) return -1;  /* errno = ENOMEM da strdup */
        dst->log_file = copy;
    } else {
        dst->log_file = NULL;
    }
    return 0;
}

/* ===== mr_create ===== */
int mr_create(mr_t *mr,
              const mr_attr_t *attr,
              mr_mapper_t mapper,
              mr_reducer_t reducer,
              void *user_arg) {
    /* Validazione dei parametri obbligatori. user_arg puo' essere NULL. */
    if (mr == NULL || attr == NULL || mapper == NULL || reducer == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* Difesa in profondita': anche se mr_attr_set_* avrebbe gia' rifiutato
     * questi valori, l'utente potrebbe avere bypassato i setter scrivendo
     * direttamente nei campi della struct. Ricontrolliamo qui. */
    if (attr->mapper_threads == 0 ||
        attr->reducer_threads == 0 ||
        attr->queue_size == 0) {
        errno = EINVAL;
        return -1;
    }

    struct mr *m = calloc(1, sizeof(*m));
    if (m == NULL) return -1;

    if (copy_attrs(&m->attrs, attr) == -1) {
        free(m);
        return -1;
    }

    m->mapper   = mapper;
    m->reducer  = reducer;
    m->user_arg = user_arg;

    *mr = m;
    return 0;
}

/* ===== mr_destroy ===== */
int mr_destroy(mr_t mr) {
    if (mr == NULL) {
        errno = EINVAL;
        return -1;
    }
    free((void *)mr->attrs.log_file);
    free(mr);
    return 0;
}

/* ===== mr_start =====
 *
 * Sequenza:
 *   1. apre file di output
 *   2. crea le 3 pipe
 *   3. fork() del processo mapper
 *   4. fork() del processo reducer
 *   5. chiusura dei descrittori NON usati nel padre
 *   6. apre la sorgente di input (file_input)
 *   7. loop: legge riga -> serializza -> writen() verso mapper [TODO p.2]
 *   8. chiude la pipe verso il mapper (segnala EOF)
 *   9. loop: legge risultati dal reducer -> scrive su output [TODO p.2]
 *  10. chiude la pipe dal reducer
 *  11. waitpid() su entrambi i figli
 *  12. cleanup
 */
int mr_start(mr_t mr, const char *input_path, const char *output_path) {
    if (mr == NULL || input_path == NULL || output_path == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Tutte le risorse partono "non aperte" per permettere cleanup
     * idempotente al label `cleanup`. */
    int ret = -1;
    int out_fd = -1;
    int main_to_mapper[2]    = { -1, -1 };
    int mapper_to_reducer[2] = { -1, -1 };
    int reducer_to_main[2]   = { -1, -1 };
    pid_t mapper_pid  = -1;
    pid_t reducer_pid = -1;
    file_input_t *fi = NULL;

    /* ----- 1. Apri il file di output ----- */
    out_fd = open(output_path,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);  /* 0644 */
    if (out_fd == -1) goto cleanup;

    /* ----- 2. Crea le tre pipe ----- */
    if (pipe(main_to_mapper)    == -1) goto cleanup;
    if (pipe(mapper_to_reducer) == -1) goto cleanup;
    if (pipe(reducer_to_main)   == -1) goto cleanup;

    /* ----- 3. fork() del processo MAPPER -----
     * Specifica §5: il fork avviene mentre il processo principale e'
     * ancora single-threaded. I thread vengono creati DENTRO mapper_proc_main,
     * cioe' nel figlio. */
    mapper_pid = fork();
    if (mapper_pid == -1) goto cleanup;

    if (mapper_pid == 0) {
        /* === FIGLIO MAPPER === */

        /* 3.1 Riconnessione di stdin/stdout alle estremita' di pipe.
         * dup2(src, dst) duplica src su dst, chiudendo dst se aperto.
         * Dopo questa chiamata, leggere da STDIN_FILENO equivale a
         * leggere dalla pipe main_to_mapper, e scrivere su STDOUT_FILENO
         * equivale a scrivere nella pipe mapper_to_reducer. */
        if (dup2(main_to_mapper[0],    STDIN_FILENO)  == -1) _exit(127);
        if (dup2(mapper_to_reducer[1], STDOUT_FILENO) == -1) _exit(127);

        /* 3.2 Chiusura di TUTTI i descrittori originali. I duplicati su
         * stdin/stdout rimangono validi (puntano alla stessa open file
         * description del kernel). */
        close(main_to_mapper[0]);
        close(main_to_mapper[1]);       /* il mapper non scrive verso se' stesso */
        close(mapper_to_reducer[0]);    /* il mapper non legge dal reducer */
        close(mapper_to_reducer[1]);
        close(reducer_to_main[0]);      /* non interessano al mapper */
        close(reducer_to_main[1]);
        close(out_fd);                  /* il mapper non scrive sull'output */

        /* 3.3 Esegui il corpo del processo mapper. Quando ritorna,
         * _exit() (non exit()): exit() chiamerebbe gli handler
         * atexit() del padre, che non vogliamo eseguire qui, e
         * flusherebbe i buffer stdio (che noi non usiamo ma rischierebbero
         * di scrivere doppi output se ereditati). */
        struct mapper_ctx ctx = {
            .mapper      = mr->mapper,
            .user_arg    = mr->user_arg,
            .num_threads = mr->attrs.mapper_threads,
            .queue_size  = mr->attrs.queue_size,
        };
        int rc = mapper_process_main(&ctx);
        _exit(rc == 0 ? 0 : 1);
    }

    /* ----- 4. fork() del processo REDUCER ----- */
    reducer_pid = fork();
    if (reducer_pid == -1) goto cleanup;

    if (reducer_pid == 0) {
        /* === FIGLIO REDUCER === */
        if (dup2(mapper_to_reducer[0], STDIN_FILENO)  == -1) _exit(127);
        if (dup2(reducer_to_main[1],   STDOUT_FILENO) == -1) _exit(127);

        close(main_to_mapper[0]);
        close(main_to_mapper[1]);
        close(mapper_to_reducer[0]);
        close(mapper_to_reducer[1]);
        close(reducer_to_main[0]);
        close(reducer_to_main[1]);
        close(out_fd);

        struct reducer_ctx ctx = {
            .reducer     = mr->reducer,
            .user_arg    = mr->user_arg,
            .num_threads = mr->attrs.reducer_threads,
            .queue_size  = mr->attrs.queue_size,
        };
        int rc = reducer_process_main(&ctx);
        _exit(rc == 0 ? 0 : 1);
    }

    /* ----- 5. PADRE: chiusura dei descrittori non utilizzati -----
     *
     * Manteniamo aperti solo:
     *   main_to_mapper[1]    -> qui ci scriviamo le righe da spedire
     *   reducer_to_main[0]   -> qui leggiamo i risultati dal reducer
     *   out_fd               -> qui scriviamo l'output finale
     *
     * Chiudiamo TUTTO il resto. ATTENZIONE PARTICOLARE alla pipe
     * mapper_to_reducer: il padre non la usa MAI, ma se ne lasciasse
     * aperto anche un solo lato di scrittura, il reducer non riceverebbe
     * mai EOF sul suo stdin -> deadlock permanente. Spec §5.1. */
    close(main_to_mapper[0]);    main_to_mapper[0]    = -1;
    close(mapper_to_reducer[0]); mapper_to_reducer[0] = -1;
    close(mapper_to_reducer[1]); mapper_to_reducer[1] = -1;  /* <-- chiave */
    close(reducer_to_main[1]);   reducer_to_main[1]   = -1;

    /* ----- 6. Apri la sorgente di input ----- */
    if (file_input_open(&fi, input_path) == -1) goto cleanup;

    /* ----- 7. Loop: leggi righe e spediscile al mapper -----
     * Ora attivo: ogni riga viene serializzata in un messaggio "Riga"
     * (intestazione + nome_file + contenuto) e scritta sulla pipe
     * verso il mapper. */
    {
        mr_file_line_t line;
        int r;
        while ((r = file_input_next(fi, &line)) == 1) {
            if (protocol_write_line(main_to_mapper[1], &line) == -1) {
                goto cleanup;
            }
        }
        if (r == -1) goto cleanup;
    }

    /* ----- 8. Chiudi la pipe verso il mapper -----
     * Questo e' il segnale che diciamo "fine input" al mapper. Quando
     * il mapper legge 0 byte dalla sua stdin, sa che non arriveranno
     * piu' righe. */
    close(main_to_mapper[1]); main_to_mapper[1] = -1;

    /* ----- 9. Loop: leggi risultati dal reducer e scrivili in output -----
     * Per la passata 1, drenamo soltanto: nella passata 2 qui andra'
     * il parsing + writen su out_fd nel formato length-prefixed di
     * §8 della specifica. */
    {
        char buf[4096];
        ssize_t n;
        while ((n = read(reducer_to_main[0], buf, sizeof buf)) > 0) {
            /* TODO passata 2: parse header risultato, scrivi su out_fd */
        }
        if (n == -1) goto cleanup;
    }

    /* ----- 10. Chiudi la pipe dal reducer ----- */
    close(reducer_to_main[0]); reducer_to_main[0] = -1;

    /* ----- 11. waitpid() esplicito sui figli -----
     * Usiamo waitpid() con i PID specifici (non wait()) perche'
     * vogliamo distinguere lo stato di mapper e reducer. */
    {
        int mapper_status = 0;
        int reducer_status = 0;

        if (waitpid(mapper_pid, &mapper_status, 0) == -1) goto cleanup;
        mapper_pid = -1;

        if (waitpid(reducer_pid, &reducer_status, 0) == -1) goto cleanup;
        reducer_pid = -1;

        /* Successo solo se entrambi sono usciti normalmente con codice 0. */
        if (!WIFEXITED(mapper_status)  || WEXITSTATUS(mapper_status)  != 0 ||
            !WIFEXITED(reducer_status) || WEXITSTATUS(reducer_status) != 0) {
            errno = EIO;
            goto cleanup;
        }
    }

    ret = 0;  /* tutto andato bene */

cleanup:
    /* ----- 12. Cleanup difensivo -----
     * Chiude tutto cio' che potrebbe essere ancora aperto. La sentinella
     * -1 garantisce idempotenza: una risorsa non aperta non viene chiusa. */
    if (fi != NULL) file_input_close(fi);

    if (main_to_mapper[0]    != -1) close(main_to_mapper[0]);
    if (main_to_mapper[1]    != -1) close(main_to_mapper[1]);
    if (mapper_to_reducer[0] != -1) close(mapper_to_reducer[0]);
    if (mapper_to_reducer[1] != -1) close(mapper_to_reducer[1]);
    if (reducer_to_main[0]   != -1) close(reducer_to_main[0]);
    if (reducer_to_main[1]   != -1) close(reducer_to_main[1]);

    /* Se un figlio e' ancora in vita (siamo arrivati al cleanup per un
     * errore PRIMA del waitpid), proviamo a recuperarlo. Le pipe sono
     * gia' chiuse, quindi i figli vedranno EOF e termineranno
     * naturalmente. */
    if (mapper_pid  > 0) waitpid(mapper_pid,  NULL, 0);
    if (reducer_pid > 0) waitpid(reducer_pid, NULL, 0);

    if (out_fd != -1) close(out_fd);

    return ret;
}
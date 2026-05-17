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
#include "log.h"

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

static int copy_attrs(mr_attr_t *dst, const mr_attr_t *src) {
    dst->mapper_threads  = src->mapper_threads;
    dst->reducer_threads = src->reducer_threads;
    dst->queue_size      = src->queue_size;
    if (src->log_file != NULL) {
        char *copy = strdup(src->log_file);
        if (copy == NULL) return -1;
        dst->log_file = copy;
    } else {
        dst->log_file = NULL;
    }
    return 0;
}

int mr_create(mr_t *mr, const mr_attr_t *attr,
              mr_mapper_t mapper, mr_reducer_t reducer, void *user_arg) {
    if (mr == NULL || attr == NULL || mapper == NULL || reducer == NULL) {
        errno = EINVAL; return -1;
    }
    if (attr->mapper_threads == 0 || attr->reducer_threads == 0 ||
        attr->queue_size == 0) {
        errno = EINVAL; return -1;
    }

    struct mr *m = calloc(1, sizeof(*m));
    if (m == NULL) return -1;
    if (copy_attrs(&m->attrs, attr) == -1) { free(m); return -1; }

    m->mapper   = mapper;
    m->reducer  = reducer;
    m->user_arg = user_arg;
    *mr = m;
    return 0;
}

int mr_destroy(mr_t mr) {
    if (mr == NULL) { errno = EINVAL; return -1; }
    free((void *)mr->attrs.log_file);
    free(mr);
    return 0;
}

int mr_start(mr_t mr, const char *input_path, const char *output_path) {
    if (mr == NULL || input_path == NULL || output_path == NULL) {
        errno = EINVAL; return -1;
    }

    /* === FIX BUG 1: TUTTE le variabili dichiarate qui, PRIMA di
     * qualsiasi cosa che possa fare goto cleanup. Cosi' anche se
     * log_init() fallisce subito, il cleanup vede valori validi. === */
    int   ret                  = -1;
    int   out_fd               = -1;
    int   main_to_mapper[2]    = { -1, -1 };
    int   mapper_to_reducer[2] = { -1, -1 };
    int   reducer_to_main[2]   = { -1, -1 };
    pid_t mapper_pid           = -1;
    pid_t reducer_pid          = -1;
    file_input_t *fi           = NULL;
    char  sem_name[64];

    /* === Setup logging === */
    snprintf(sem_name, sizeof sem_name, "/mr_log_%d", (int)getpid());
    if (log_init(mr->attrs.log_file, sem_name, "main") == -1) goto cleanup;

    log_event("MR_START",
              "input=%s output=%s mapper_threads=%zu reducer_threads=%zu queue_size=%zu",
              input_path, output_path,
              mr->attrs.mapper_threads, mr->attrs.reducer_threads,
              mr->attrs.queue_size);

    /* === 1. Apri il file di output === */
    out_fd = open(output_path,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (out_fd == -1) goto cleanup;
    log_event("OUTPUT_OPEN", "path=%s fd=%d", output_path, out_fd);

    /* === 2. Crea le tre pipe === */
    if (pipe(main_to_mapper) == -1) goto cleanup;
    log_event("PIPE_CREATED", "main_to_mapper read=%d write=%d",
              main_to_mapper[0], main_to_mapper[1]);

    if (pipe(mapper_to_reducer) == -1) goto cleanup;
    log_event("PIPE_CREATED", "mapper_to_reducer read=%d write=%d",
              mapper_to_reducer[0], mapper_to_reducer[1]);

    if (pipe(reducer_to_main) == -1) goto cleanup;
    log_event("PIPE_CREATED", "reducer_to_main read=%d write=%d",
              reducer_to_main[0], reducer_to_main[1]);

    /* === 3. fork del processo MAPPER === */
    mapper_pid = fork();
    if (mapper_pid == -1) goto cleanup;

    if (mapper_pid == 0) {
        /* --- FIGLIO MAPPER --- */
        if (dup2(main_to_mapper[0],    STDIN_FILENO)  == -1) _exit(127);
        if (dup2(mapper_to_reducer[1], STDOUT_FILENO) == -1) _exit(127);

        close(main_to_mapper[0]);    close(main_to_mapper[1]);
        close(mapper_to_reducer[0]); close(mapper_to_reducer[1]);
        close(reducer_to_main[0]);   close(reducer_to_main[1]);
        close(out_fd);

        struct mapper_ctx mctx = {
            .mapper      = mr->mapper,
            .user_arg    = mr->user_arg,
            .num_threads = mr->attrs.mapper_threads,
            .queue_size  = mr->attrs.queue_size,
        };

        log_set_proc_name("mapper");
        log_event("PROC_START", NULL);
        int rc = mapper_process_main(&mctx);
        log_event("PROC_END", "rc=%d", rc);
        log_cleanup(0);

        _exit(rc == 0 ? 0 : 1);
    }

    /* === FIX BUG 3: log SOLO nel padre, dopo il _exit del figlio === */
    log_event("FORK_MAPPER", "pid=%d", (int)mapper_pid);

    /* === 4. fork del processo REDUCER === */
    reducer_pid = fork();
    if (reducer_pid == -1) goto cleanup;

    if (reducer_pid == 0) {
        /* --- FIGLIO REDUCER --- */
        if (dup2(mapper_to_reducer[0], STDIN_FILENO)  == -1) _exit(127);
        if (dup2(reducer_to_main[1],   STDOUT_FILENO) == -1) _exit(127);

        close(main_to_mapper[0]);    close(main_to_mapper[1]);
        close(mapper_to_reducer[0]); close(mapper_to_reducer[1]);
        close(reducer_to_main[0]);   close(reducer_to_main[1]);
        close(out_fd);

        struct reducer_ctx rctx = {
            .reducer     = mr->reducer,
            .user_arg    = mr->user_arg,
            .num_threads = mr->attrs.reducer_threads,
            .queue_size  = mr->attrs.queue_size,
        };

        log_set_proc_name("reducer");
        log_event("PROC_START", NULL);
        int rc = reducer_process_main(&rctx);
        log_event("PROC_END", "rc=%d", rc);
        log_cleanup(0);

        _exit(rc == 0 ? 0 : 1);
    }

    log_event("FORK_REDUCER", "pid=%d", (int)reducer_pid);

    /* === 5. Padre: chiusura descrittori non usati === */
    close(main_to_mapper[0]);    main_to_mapper[0]    = -1;
    close(mapper_to_reducer[0]); mapper_to_reducer[0] = -1;
    close(mapper_to_reducer[1]); mapper_to_reducer[1] = -1;
    close(reducer_to_main[1]);   reducer_to_main[1]   = -1;

    /* === 6. Apri input === */
    if (file_input_open(&fi, input_path) == -1) goto cleanup;
    log_event("INPUT_OPEN", "path=%s", input_path);

    /* === 7. Loop righe -> pipe mapper === */
    {
        size_t lines_sent = 0;
        mr_file_line_t line;
        int r;
        while ((r = file_input_next(fi, &line)) == 1) {
            if (protocol_write_line(main_to_mapper[1], &line) == -1) goto cleanup;
            lines_sent++;
        }
        if (r == -1) goto cleanup;
        log_event("INPUT_LINES", "count=%zu", lines_sent);
    }

    /* === 8. Chiudi pipe verso mapper (EOF segnale) === */
    close(main_to_mapper[1]); main_to_mapper[1] = -1;

    /* === 9. Loop risultati dal reducer -> file output === */
    {
        char buf[4096];
        ssize_t n;
        while ((n = read(reducer_to_main[0], buf, sizeof buf)) > 0) {
            if (writen(out_fd, buf, (size_t)n) == -1) goto cleanup;
        }
        if (n == -1) goto cleanup;
    }

    close(reducer_to_main[0]); reducer_to_main[0] = -1;

    /* === 11. waitpid === */
    {
        int mapper_status = 0, reducer_status = 0;
        if (waitpid(mapper_pid, &mapper_status, 0) == -1) goto cleanup;
        mapper_pid = -1;
        if (waitpid(reducer_pid, &reducer_status, 0) == -1) goto cleanup;
        reducer_pid = -1;

        log_event("WAITPID", "mapper_status=%d reducer_status=%d",
                  WEXITSTATUS(mapper_status), WEXITSTATUS(reducer_status));

        if (!WIFEXITED(mapper_status)  || WEXITSTATUS(mapper_status)  != 0 ||
            !WIFEXITED(reducer_status) || WEXITSTATUS(reducer_status) != 0) {
            errno = EIO;
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    if (fi != NULL) file_input_close(fi);

    if (main_to_mapper[0]    != -1) close(main_to_mapper[0]);
    if (main_to_mapper[1]    != -1) close(main_to_mapper[1]);
    if (mapper_to_reducer[0] != -1) close(mapper_to_reducer[0]);
    if (mapper_to_reducer[1] != -1) close(mapper_to_reducer[1]);
    if (reducer_to_main[0]   != -1) close(reducer_to_main[0]);
    if (reducer_to_main[1]   != -1) close(reducer_to_main[1]);

    if (mapper_pid  > 0) waitpid(mapper_pid,  NULL, 0);
    if (reducer_pid > 0) waitpid(reducer_pid, NULL, 0);

    if (out_fd != -1) close(out_fd);

    log_event("MR_END", "ret=%d", ret);
    log_cleanup(1);

    return ret;
}
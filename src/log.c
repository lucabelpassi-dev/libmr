/* src/log.c
 *
 * Logging cross-process basato su semaforo POSIX named.
 * Vedi log.h per il contratto pubblico.
 * Spec di riferimento: §11.
 */

#define _POSIX_C_SOURCE 200809L

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ===== Stato globale (per processo) ===== */
static int    g_log_fd        = -1;
static sem_t *g_log_sem       = SEM_FAILED;
static char   g_sem_name[64]  = "";
static char   g_proc_name[16] = "main";

/* ===== Stato per-thread (TLS) ===== */
static _Thread_local char g_thread_name[32] = "-";

/* ===== API pubblica ===== */

int log_init(const char *log_path, const char *sem_name, const char *proc_name) {
    if (sem_name == NULL || proc_name == NULL) {
        errno = EINVAL; return -1;
    }

    snprintf(g_sem_name,  sizeof g_sem_name,  "%s", sem_name);
    snprintf(g_proc_name, sizeof g_proc_name, "%s", proc_name);

    /* Apri il file di log: O_APPEND garantisce che ogni write si
     * posizioni atomicamente in fondo. O_TRUNC lo svuota a inizio
     * esecuzione (vogliamo log fresco per ogni mr_start). */
    const char *path = log_path ? log_path : "mr.log";
    g_log_fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_TRUNC, 0644);
    if (g_log_fd == -1) return -1;

    /* Crea il semaforo named, valore iniziale 1 (= mutex).
     * Se per qualche motivo esiste gia' (es. crash precedente non
     * pulito), lo rimuoviamo e ricreiamo. */
    g_log_sem = sem_open(sem_name, O_CREAT | O_EXCL, 0600, 1);
    if (g_log_sem == SEM_FAILED && errno == EEXIST) {
        sem_unlink(sem_name);
        g_log_sem = sem_open(sem_name, O_CREAT | O_EXCL, 0600, 1);
    }
    if (g_log_sem == SEM_FAILED) {
        close(g_log_fd); g_log_fd = -1;
        return -1;
    }
    return 0;
}

void log_set_proc_name(const char *name) {
    if (name != NULL) {
        snprintf(g_proc_name, sizeof g_proc_name, "%s", name);
    }
}

void log_set_thread_name(const char *name) {
    if (name == NULL) strcpy(g_thread_name, "-");
    else snprintf(g_thread_name, sizeof g_thread_name, "%s", name);
}

void log_event(const char *event, const char *fmt, ...) {
    if (g_log_fd == -1 || g_log_sem == SEM_FAILED) return;  /* non-init: noop */

    char line[512];

    /* Timestamp con millisecondi (localtime_r e' thread-safe). */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);

    int n = snprintf(line, sizeof line,
        "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [%-7s] [%-10s] [%-18s] ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec / 1000,
        g_proc_name, g_thread_name, event ? event : "-");
    if (n < 0) return;
    if (n > (int)sizeof line - 2) n = sizeof line - 2;

    /* Corpo del messaggio (opzionale) */
    if (fmt != NULL && fmt[0] != '\0') {
        va_list ap;
        va_start(ap, fmt);
        int m = vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
        va_end(ap);
        if (m > 0) n += m;
        if (n > (int)sizeof line - 2) n = sizeof line - 2;
    }

    line[n++] = '\n';

    /* Sezione critica: solo una write per volta sul fd di log. */
    if (sem_wait(g_log_sem) == 0) {
        ssize_t w = write(g_log_fd, line, (size_t)n);
        (void)w;
        sem_post(g_log_sem);
    }
}

void log_cleanup(int unlink_sem) {
    if (g_log_fd != -1) { close(g_log_fd); g_log_fd = -1; }
    if (g_log_sem != SEM_FAILED) {
        sem_close(g_log_sem);
        g_log_sem = SEM_FAILED;
    }
    if (unlink_sem && g_sem_name[0] != '\0') {
        sem_unlink(g_sem_name);
        g_sem_name[0] = '\0';
    }
}
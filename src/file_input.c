#define _POSIX_C_SOURCE 200809L  /* già in CFLAGS, ridondante ma sicuro */

#include "file_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * Stato interno della sorgente di input.
 *
 *   paths[]      : array di path completi dei file da elaborare,
 *                  gia' in ordine lessicografico.
 *   paths_count  : numero di file.
 *   paths_idx    : indice del file attualmente aperto in paths[].
 *
 *   current_fp   : FILE* del file corrente, NULL se nessuno aperto.
 *   current_name : path del file corrente (alias dentro paths[]).
 *   line_number  : numero della prossima riga (riparte a 1 per file).
 *
 *   line_buf, line_buf_cap : buffer riallocabile riusato da getline().
 */
struct file_input {
    char        **paths;
    size_t        paths_count;
    size_t        paths_idx;

    FILE         *current_fp;
    const char   *current_name;
    size_t        current_name_len;
    unsigned long line_number;

    char         *line_buf;
    size_t        line_buf_cap;
};

/* ---------- Helper statici ---------- */

/*
 * Filtro per scandir(): salta '.', '..' e dotfile.
 * La verifica "e' davvero un file regolare?" e' fatta dopo con stat(),
 * perche' d_type puo' essere DT_UNKNOWN su alcuni filesystem.
 */
static int filter_no_dotfiles(const struct dirent *e) {
    return e->d_name[0] != '.';
}

static int is_regular_file(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) return 0;
    return S_ISREG(st.st_mode);
}

/*
 * Costruisce "dir/name" in un buffer fresco.
 * Ritorna il buffer (chiamante libera) o NULL su errore.
 */
static char *join_path(const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    char *full = malloc(dlen + 1 + nlen + 1);
    if (full == NULL) return NULL;
    memcpy(full, dir, dlen);
    full[dlen] = '/';
    memcpy(full + dlen + 1, name, nlen);
    full[dlen + 1 + nlen] = '\0';
    return full;
}

/*
 * Apre il prossimo file di paths[] (paths_idx).
 * Ritorna:
 *    1 = aperto correttamente, current_fp e current_name sono validi;
 *    0 = niente più file (fine input);
 *   -1 = errore (errno impostato).
 */
static int open_current(file_input_t *fi) {
    if (fi->paths_idx >= fi->paths_count) return 0;

    const char *p = fi->paths[fi->paths_idx];
    fi->current_fp = fopen(p, "r");
    if (fi->current_fp == NULL) return -1;

    fi->current_name     = p;
    fi->current_name_len = strlen(p);
    fi->line_number      = 0;
    return 1;
}

/* ---------- API pubblica ---------- */

int file_input_open(file_input_t **fi_out, const char *path) {
    if (fi_out == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }

    file_input_t *fi = calloc(1, sizeof(*fi));
    if (fi == NULL) return -1;

    struct stat st;
    if (stat(path, &st) == -1) {
        free(fi);
        return -1;
    }

    if (S_ISREG(st.st_mode)) {
        /* Caso "file singolo": paths[] di lunghezza 1 */
        fi->paths = malloc(sizeof(char *));
        if (fi->paths == NULL) goto err;
        fi->paths[0] = strdup(path);
        if (fi->paths[0] == NULL) { free(fi->paths); goto err; }
        fi->paths_count = 1;

    } else if (S_ISDIR(st.st_mode)) {
        /* Caso "directory": scandir con alphasort, poi filtro per
         * file regolari (scartiamo sottocartelle, link, fifo, ...) */
        struct dirent **entries = NULL;
        int n = scandir(path, &entries, filter_no_dotfiles, alphasort);
        if (n < 0) goto err;

        fi->paths = calloc((size_t)n, sizeof(char *));
        if (fi->paths == NULL) {
            for (int i = 0; i < n; i++) free(entries[i]);
            free(entries);
            goto err;
        }

        for (int i = 0; i < n; i++) {
            char *full = join_path(path, entries[i]->d_name);
            free(entries[i]);
            if (full == NULL) {
                /* malloc fallito: cleanup parziale */
                for (int j = i + 1; j < n; j++) free(entries[j]);
                free(entries);
                for (size_t k = 0; k < fi->paths_count; k++) free(fi->paths[k]);
                free(fi->paths);
                goto err;
            }
            if (is_regular_file(full)) {
                fi->paths[fi->paths_count++] = full;
            } else {
                free(full);  /* non e' un file regolare: scartato */
            }
        }
        free(entries);

    } else {
        errno = EINVAL;
        goto err;
    }

    *fi_out = fi;
    return 0;

err:
    free(fi);
    return -1;
}

int file_input_next(file_input_t *fi, mr_file_line_t *line) {
    if (fi == NULL || line == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Loop: salta i file esauriti finche' non trovo una riga o finisco */
    for (;;) {
        if (fi->current_fp == NULL) {
            int r = open_current(fi);
            if (r ==  0) return 0;  /* niente piu' file */
            if (r == -1) return -1; /* errore di apertura */
        }

        errno = 0;
        ssize_t n = getline(&fi->line_buf, &fi->line_buf_cap, fi->current_fp);

        if (n == -1) {
            /* EOF o errore di lettura */
            int read_err = ferror(fi->current_fp);
            fclose(fi->current_fp);
            fi->current_fp = NULL;
            if (read_err) return -1;
            /* EOF naturale: passa al prossimo file */
            fi->paths_idx++;
            continue;
        }

        /* Strip del '\n' terminale, se presente. La specifica vuole
         * che il '\n' NON faccia parte del contenuto passato al mapper. */
        size_t line_len = (size_t)n;
        if (line_len > 0 && fi->line_buf[line_len - 1] == '\n') {
            line_len--;
            fi->line_buf[line_len] = '\0';
        }

        fi->line_number++;

        line->file_name     = fi->current_name;
        line->file_name_len = fi->current_name_len;
        line->line_number   = fi->line_number;
        line->line          = fi->line_buf;
        line->line_len      = line_len;
        return 1;
    }
}

void file_input_close(file_input_t *fi) {
    if (fi == NULL) return;
    if (fi->current_fp != NULL) fclose(fi->current_fp);
    for (size_t i = 0; i < fi->paths_count; i++) free(fi->paths[i]);
    free(fi->paths);
    free(fi->line_buf);
    free(fi);
}
/* src/mr_attr.c
 *
 * Implementazione di mr_attr_t: inizializzazione, distruzione e setter.
 * Vedi mr.h per le firme pubbliche e la specifica §A per il contratto.
 */

#include "mr.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ----- Default ragionevoli -----
 * Il minimo richiesto dalla specifica e' 1 thread mapper e 1 thread
 * reducer. Scegliamo 4 di default per esercitare effettivamente la
 * concorrenza nei test e negli esempi (allineato a quanto mostrato
 * nell'esempio di uso §B della specifica). */
#define MR_DEFAULT_MAPPER_THREADS   ((size_t)4)
#define MR_DEFAULT_REDUCER_THREADS  ((size_t)4)
#define MR_DEFAULT_QUEUE_SIZE       ((size_t)64)
/* log_file di default = NULL: il framework usera' "mr.log" o quanto
 * documentato in src/log.c. */

int mr_attr_init(mr_attr_t *attr) {
    if (attr == NULL) {
        errno = EINVAL;
        return -1;
    }
    attr->mapper_threads  = MR_DEFAULT_MAPPER_THREADS;
    attr->reducer_threads = MR_DEFAULT_REDUCER_THREADS;
    attr->queue_size      = MR_DEFAULT_QUEUE_SIZE;
    attr->log_file        = NULL;
    return 0;
}

int mr_attr_destroy(mr_attr_t *attr) {
    if (attr == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* free() su NULL e' un no-op, quindi e' safe anche se non e' mai
     * stato chiamato set_log_file().
     *
     * Il cast (void *) rimuove il const dichiarato nella struct
     * pubblica: e' corretto perche' la memoria e' effettivamente di
     * proprieta' di mr_attr_t (allocata da set_log_file con strdup).
     * Il const nel typedef serve a impedire modifiche dall'ESTERNO,
     * non a negarne la proprieta'. */
    free((void *)attr->log_file);

    /* Reset dei campi per invalidare un eventuale uso accidentale
     * dopo destroy (defensive programming). */
    attr->log_file        = NULL;
    attr->mapper_threads  = 0;
    attr->reducer_threads = 0;
    attr->queue_size      = 0;
    return 0;
}

int mr_attr_set_mapper_threads(mr_attr_t *attr, size_t n) {
    if (attr == NULL || n == 0) {
        errno = EINVAL;
        return -1;
    }
    attr->mapper_threads = n;
    return 0;
}

int mr_attr_set_reducer_threads(mr_attr_t *attr, size_t n) {
    if (attr == NULL || n == 0) {
        errno = EINVAL;
        return -1;
    }
    attr->reducer_threads = n;
    return 0;
}

int mr_attr_set_queue_size(mr_attr_t *attr, size_t n) {
    if (attr == NULL || n == 0) {
        errno = EINVAL;
        return -1;
    }
    attr->queue_size = n;
    return 0;
}

int mr_attr_set_log_file(mr_attr_t *attr, const char *path) {
    if (attr == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Libera l'eventuale path precedente: gestisce correttamente
     * chiamate ripetute di set_log_file() senza leak. */
    free((void *)attr->log_file);
    attr->log_file = NULL;

    if (path == NULL) {
        /* NULL = reset al default: il framework decidera' il nome. */
        return 0;
    }

    char *copy = strdup(path);
    if (copy == NULL) {
        /* errno e' gia' impostato da strdup (tipicamente ENOMEM). */
        return -1;
    }
    attr->log_file = copy;
    return 0;
}
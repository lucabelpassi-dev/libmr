/* tests/test_attr.c */
#include "mr.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    mr_attr_t attr;

    /* init imposta default validi */
    assert(mr_attr_init(&attr) == 0);
    assert(attr.mapper_threads >= 1);
    assert(attr.reducer_threads >= 1);
    assert(attr.queue_size >= 1);
    assert(attr.log_file == NULL);

    /* setter validi */
    assert(mr_attr_set_mapper_threads(&attr, 8) == 0);
    assert(attr.mapper_threads == 8);
    assert(mr_attr_set_queue_size(&attr, 128) == 0);
    assert(attr.queue_size == 128);

    /* set_log_file copia internamente */
    assert(mr_attr_set_log_file(&attr, "/tmp/mr.log") == 0);
    assert(attr.log_file != NULL);
    assert(strcmp(attr.log_file, "/tmp/mr.log") == 0);

    /* set ripetuto: non deve leakare (valgrind lo conferma) */
    assert(mr_attr_set_log_file(&attr, "/var/log/mr.log") == 0);
    assert(strcmp(attr.log_file, "/var/log/mr.log") == 0);

    /* set_log_file(NULL) = reset */
    assert(mr_attr_set_log_file(&attr, NULL) == 0);
    assert(attr.log_file == NULL);

    /* setter invalidi: zero thread / zero queue / attr NULL */
    errno = 0;
    assert(mr_attr_set_mapper_threads(&attr, 0) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(mr_attr_set_queue_size(&attr, 0) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(mr_attr_set_reducer_threads(NULL, 4) == -1);
    assert(errno == EINVAL);

    /* destroy e' safe; chiamarlo due volte non rompe niente perche'
     * dopo il primo destroy log_file e' NULL e free(NULL) e' no-op. */
    assert(mr_attr_destroy(&attr) == 0);

    printf("test_attr: OK\n");
    return 0;
}
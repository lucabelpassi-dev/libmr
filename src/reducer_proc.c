/* reducer_proc.c */
#include "reducer_proc.h"
#include <unistd.h>

int reducer_process_main(struct reducer_ctx *ctx) {
    (void)ctx;

    /* Stub: drena stdin (= mapper_to_reducer). Quando il mapper esce
     * e quindi chiude la sua estremita' di scrittura -> EOF -> usciamo.
     * Uscendo, _exit() chiude stdout (= reducer_to_main[1]),
     * propagando EOF al padre. */
    char buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof buf)) > 0) {
        /* drain */
    }
    return (n == -1) ? 1 : 0;
}
#include "mapper_proc.h"
#include <unistd.h>

int mapper_process_main(struct mapper_ctx *ctx) {
    (void)ctx;  /* TODO passata 2: creare i thread con ctx->num_threads */

    /* Drena stdin per non far bloccare il padre sulle write().
     * Quando il padre chiude main_to_mapper[1] -> EOF qui -> ritorniamo.
     * Uscendo, _exit() chiudera' stdout (= mapper_to_reducer[1])
     * automaticamente, propagando EOF al reducer. */
    char buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof buf)) > 0) {
        /* drain */
    }
    return (n == -1) ? 1 : 0;
}
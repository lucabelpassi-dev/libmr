#include "mr.h"
#include <stdio.h>

/* Callback dummy: non verranno invocate dagli stub, ma servono per
 * soddisfare l'API di mr_create. */
static int dummy_mapper(const mr_file_line_t *l, mr_emit_pair_t e,
                        void *ea, void *ua) {
    (void)l; (void)e; (void)ea; (void)ua;
    return 0;
}
static int dummy_reducer(const char *t, const mr_value_t *v, size_t n,
                         mr_emit_result_t e, void *ea, void *ua) {
    (void)t; (void)v; (void)n; (void)e; (void)ea; (void)ua;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input_path> <output_path>\n", argv[0]);
        return 1;
    }
    mr_attr_t attr;
    mr_attr_init(&attr);
    mr_t mr;
    if (mr_create(&mr, &attr, dummy_mapper, dummy_reducer, NULL) == -1) {
        perror("mr_create"); return 1;
    }
    if (mr_start(mr, argv[1], argv[2]) == -1) {
        perror("mr_start"); mr_destroy(mr); mr_attr_destroy(&attr); return 1;
    }
    mr_destroy(mr);
    mr_attr_destroy(&attr);
    printf("smoke: OK\n");
    return 0;
}
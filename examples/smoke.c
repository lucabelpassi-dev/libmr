#include "mr.h"
#include <stdio.h>
#include <string.h>

/* Mapper demo per testare la pipeline: per ogni riga emette UNA coppia
 * <"lineN", contenuto_della_riga> dove N e' il numero di riga.
 *
 * NB: il token "lineN" e' alfanumerico ASCII, come richiesto dal
 * contratto (§3 della specifica). Il valore e' la riga grezza,
 * cioe' byte arbitrari di lunghezza nota — opaco per il framework. */
static int demo_mapper(const mr_file_line_t *line,
                       mr_emit_pair_t emit, void *emit_arg, void *user_arg) {
    (void)user_arg;
    char token[64];
    snprintf(token, sizeof(token), "line%lu", line->line_number);
    return emit(token, line->line, line->line_len, emit_arg);
}

static int passthrough_reducer(const char *token, const mr_value_t *values,
                               size_t values_count, mr_emit_result_t emit,
                               void *emit_arg, void *user_arg) {
    (void)user_arg;
    if (values_count > 0) {
        return emit(token, values[0].data, values[0].size, emit_arg);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input> <output>\n", argv[0]);
        return 1;
    }
    mr_attr_t attr;
    mr_attr_init(&attr);
    mr_t mr;
    if (mr_create(&mr, &attr, demo_mapper, passthrough_reducer, NULL) == -1) {
        perror("mr_create"); return 1;
    }
    if (mr_start(mr, argv[1], argv[2]) == -1) {
        perror("mr_start");
        mr_destroy(mr); mr_attr_destroy(&attr);
        return 1;
    }
    mr_destroy(mr);
    mr_attr_destroy(&attr);
    return 0;
}
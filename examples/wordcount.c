/* examples/wordcount.c
 *
 * Esempio canonico: conta le occorrenze di ogni parola alfanumerica
 * trovata nel testo di input. La spec §9 lo nomina espressamente.
 *
 * Definizione di "parola": sequenza massimale di caratteri alfanumerici
 * ASCII [A-Za-z0-9]. Tutto il resto (spazi, punteggiatura, byte non-ASCII)
 * separa le parole.
 */

#include "mr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// static limita al file, inline suggerisce al compilatore di farla inlining perché è chiamata in un loop stretto.
static inline int is_word_char(unsigned char c) { 
    return (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9');
}

/* MAPPER: per ogni parola della riga, emetti (parola, 1). */
static int wc_mapper(const mr_file_line_t *line,
                     mr_emit_pair_t emit, void *emit_arg, void *user_arg) {
    (void)user_arg;
    int one = 1;
    char token[256];

    const char *p   = line->line;
    const char *end = line->line + line->line_len;

    while (p < end) {
        /* Salta separatori (tutto cio' che non e' alfanumerico). */
        while (p < end && !is_word_char((unsigned char)*p)) p++;
        if (p >= end) break;

        /* Estrai una parola alfanumerica. */
        const char *start = p;
        while (p < end && is_word_char((unsigned char)*p)) p++;
        size_t len = (size_t)(p - start);

        /* Salta parole troppo lunghe (oltre il buffer locale). */
        if (len >= sizeof token) continue;

        /* Copia + terminazione nulla: emit vuole una stringa C. */
        memcpy(token, start, len);
        token[len] = '\0';

        /* Il valore "1" e' un int serializzato come byte raw —
         * il framework lo tratta come opaco; sara' il reducer a
         * reinterpretarlo. */
        if (emit(token, &one, sizeof one, emit_arg) == -1) return -1;
    }
    return 0;
}

/* REDUCER: somma tutti gli interi per quel token. */
static int wc_reducer(const char *token,
                      const mr_value_t *values, size_t values_count,
                      mr_emit_result_t emit, void *emit_arg, void *user_arg) {
    (void)user_arg;
    int total = 0;

    for (size_t i = 0; i < values_count; i++) {
        /* Sanity check: ci aspettiamo solo int da 4 byte; saltiamo il resto. */
        if (values[i].size != sizeof(int)) continue;
        int v;
        memcpy(&v, values[i].data, sizeof(int));
        total += v;
    }

    /* Emetti il risultato come int raw. */
    return emit(token, &total, sizeof total, emit_arg);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input_path> <output_path>\n", argv[0]);
        return 1;
    }

    mr_attr_t attr;
    if (mr_attr_init(&attr) == -1) {
        perror("mr_attr_init");
        return 1;
    }

    mr_t mr;
    if (mr_create(&mr, &attr, wc_mapper, wc_reducer, NULL) == -1) {
        perror("mr_create");
        mr_attr_destroy(&attr);
        return 1;
    }

    if (mr_start(mr, argv[1], argv[2]) == -1) {
        perror("mr_start");
        mr_destroy(mr);
        mr_attr_destroy(&attr);
        return 1;
    }

    mr_destroy(mr);
    mr_attr_destroy(&attr);
    return 0;
}
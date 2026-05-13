#include "group_table.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    /* ===== Caso 1: inserimenti misti, verifica ordine lessicografico ===== */
    {
        group_table_t *t = group_table_create(16);
        assert(t != NULL);

        int v1 = 1, v2 = 2, v3 = 3;

        /* Inserimenti in ordine sparso. "alpha" appare due volte. */
        assert(group_table_insert(t, "gamma", 5, &v1, sizeof v1) == 0);
        assert(group_table_insert(t, "alpha", 5, &v2, sizeof v2) == 0);
        assert(group_table_insert(t, "beta",  4, &v3, sizeof v3) == 0);
        assert(group_table_insert(t, "alpha", 5, &v1, sizeof v1) == 0);

        group_entry_t **entries = NULL;
        size_t count = 0;
        assert(group_table_finalize(t, &entries, &count) == 0);
        assert(count == 3);

        /* Verifica ordinamento lessicografico: alpha, beta, gamma */
        assert(strcmp(entries[0]->token, "alpha") == 0);
        assert(strcmp(entries[1]->token, "beta")  == 0);
        assert(strcmp(entries[2]->token, "gamma") == 0);

        /* "alpha" deve avere 2 valori in ordine di inserimento: v2, v1 */
        assert(entries[0]->values_count == 2);
        assert(*(const int *)entries[0]->values[0].data == 2);
        assert(*(const int *)entries[0]->values[1].data == 1);

        assert(entries[1]->values_count == 1);
        assert(*(const int *)entries[1]->values[0].data == 3);

        assert(entries[2]->values_count == 1);
        assert(*(const int *)entries[2]->values[0].data == 1);

        free(entries);
        group_table_destroy(t);
    }

    /* ===== Caso 2: tabella vuota ===== */
    {
        group_table_t *t = group_table_create(8);
        group_entry_t **entries = (group_entry_t **)0xDEADBEEF;  /* sentinella */
        size_t count = 999;
        assert(group_table_finalize(t, &entries, &count) == 0);
        assert(count == 0);
        assert(entries == NULL);
        group_table_destroy(t);
    }

    /* ===== Caso 3: valori OPACHI con byte arbitrari, incluso \0 ===== */
    {
        group_table_t *t = group_table_create(8);
        unsigned char bin[] = { 0x00, 0xFF, 0x00, 0xAB, 0x7E };
        assert(group_table_insert(t, "k", 1, bin, sizeof bin) == 0);

        group_entry_t **entries; size_t count;
        assert(group_table_finalize(t, &entries, &count) == 0);
        assert(count == 1);
        assert(entries[0]->values_count == 1);
        assert(entries[0]->values[0].size == sizeof bin);
        assert(memcmp(entries[0]->values[0].data, bin, sizeof bin) == 0);

        free(entries);
        group_table_destroy(t);
    }

    /* ===== Caso 4: stress con molti token uguali (catene lunghe) ===== */
    {
        group_table_t *t = group_table_create(4);  /* pochi bucket = molte collisioni */
        for (int i = 0; i < 100; i++) {
            char tok[16];
            int n = snprintf(tok, sizeof tok, "tok%d", i);
            assert(group_table_insert(t, tok, (size_t)n, &i, sizeof i) == 0);
        }
        /* Inserisci 10 valori extra sotto lo stesso token */
        for (int i = 0; i < 10; i++) {
            assert(group_table_insert(t, "tok42", 5, &i, sizeof i) == 0);
        }

        group_entry_t **entries; size_t count;
        assert(group_table_finalize(t, &entries, &count) == 0);
        assert(count == 100);  /* tok0..tok99 distinti */

        /* tok42 deve avere 11 valori (1 originale + 10 aggiunti) */
        int found = 0;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(entries[i]->token, "tok42") == 0) {
                assert(entries[i]->values_count == 11);
                found = 1;
                break;
            }
        }
        assert(found);

        free(entries);
        group_table_destroy(t);
    }

    printf("test_group_table: OK\n");
    return 0;
}
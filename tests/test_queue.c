/* tests/test_queue.c
 *
 * Test unitario della coda thread-safe.
 * Compilato direttamente con ../src/queue.c (non passa per libmr.a)
 * per testare il modulo in isolamento. */

#include "queue.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

/* === Test 1: push e pop in un solo thread === */
static void test_basic_push_pop(void) {
    thread_queue_t q;
    queue_init(&q);

    int *v1 = malloc(sizeof(int)); *v1 = 42;
    int *v2 = malloc(sizeof(int)); *v2 = 99;

    queue_push(&q, (mr_value_t){ .data = v1, .size = sizeof(int) });
    queue_push(&q, (mr_value_t){ .data = v2, .size = sizeof(int) });

    mr_value_t out;
    assert(queue_pop(&q, &out) == true);
    assert(*(const int *)out.data == 42);
    free((void *)out.data);

    assert(queue_pop(&q, &out) == true);
    assert(*(const int *)out.data == 99);
    free((void *)out.data);

    queue_close(&q);
    assert(queue_pop(&q, &out) == false);  /* coda vuota e chiusa */

    queue_destroy(&q);
}

/* === Test 2: produttore-consumatore concorrente === */
struct prod_arg { thread_queue_t *q; int n; };

static int producer_thread(void *arg) {
    struct prod_arg *a = arg;
    for (int i = 0; i < a->n; i++) {
        int *v = malloc(sizeof(int));
        *v = i;
        queue_push(a->q, (mr_value_t){ .data = v, .size = sizeof(int) });
    }
    queue_close(a->q);
    return 0;
}

static int consumer_thread(void *arg) {
    thread_queue_t *q = arg;
    int sum = 0, count = 0;
    mr_value_t out;
    while (queue_pop(q, &out)) {
        sum += *(const int *)out.data;
        count++;
        free((void *)out.data);
    }
    /* Codifica sum e count nel return: assumiamo n=100 -> sum=4950, count=100 */
    return sum * 10000 + count;
}

static void test_concurrent(void) {
    thread_queue_t q;
    queue_init(&q);

    struct prod_arg parg = { .q = &q, .n = 100 };
    thrd_t prod, cons;
    thrd_create(&prod, producer_thread, &parg);
    thrd_create(&cons, consumer_thread, &q);

    int prod_ret, cons_ret;
    thrd_join(prod, &prod_ret);
    thrd_join(cons, &cons_ret);

    /* sum(0..99) = 4950, count = 100 */
    assert(cons_ret == 4950 * 10000 + 100);

    queue_destroy(&q);
}

/* === Test 3: distruzione di coda non vuota (drain difensivo) === */
static void test_destroy_non_empty(void) {
    thread_queue_t q;
    queue_init(&q);

    int *v = malloc(sizeof(int)); *v = 7;
    queue_push(&q, (mr_value_t){ .data = v, .size = sizeof(int) });

    /* Non chiamiamo pop: lasciamo il nodo in coda.
     * queue_destroy deve liberare sia il nodo sia il buffer. */
    queue_destroy(&q);
    /* Se non ci fosse il drain in queue_destroy, valgrind segnalerebbe leak. */
}

int main(void) {
    test_basic_push_pop();
    test_concurrent();
    test_destroy_non_empty();
    printf("test_queue: OK\n");
    return 0;
}
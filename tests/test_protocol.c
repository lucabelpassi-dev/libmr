/* tests/test_protocol.c
 *
 * Test unitario di readn/writen e di tutti i tipi di messaggio
 * del protocollo. Usa pipe locali per simulare la comunicazione. */

#include "protocol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* === Test 1: readn / writen di base === */
static void test_readn_writen(void) {
    int fd[2];
    assert(pipe(fd) == 0);

    const char msg[] = "Hello, world! Questa e' una stringa di test.";
    size_t n = sizeof msg;
    assert(writen(fd[1], msg, n) == (ssize_t)n);

    char buf[sizeof msg];
    assert(readn(fd[0], buf, n) == (ssize_t)n);
    assert(memcmp(buf, msg, n) == 0);

    close(fd[0]); close(fd[1]);
}

/* === Test 2: roundtrip messaggio "Riga" === */
static void test_line_roundtrip(void) {
    int fd[2];
    assert(pipe(fd) == 0);

    mr_file_line_t in = {
        .file_name     = "test.txt",
        .file_name_len = 8,
        .line_number   = 42,
        .line          = "ciao mondo",
        .line_len      = 10,
    };
    assert(protocol_write_line(fd[1], &in) == 0);

    mr_line_msg_t out;
    assert(protocol_read_line(fd[0], &out) == 1);
    assert(out.file_name_len == 8);
    assert(memcmp(out.file_name, "test.txt", 8) == 0);
    assert(out.line_number == 42);
    assert(out.line_len == 10);
    assert(memcmp(out.line, "ciao mondo", 10) == 0);
    protocol_line_msg_free(&out);

    /* Chiudi write end e verifica EOF pulito */
    close(fd[1]);
    mr_line_msg_t out2;
    assert(protocol_read_line(fd[0], &out2) == 0);
    close(fd[0]);
}

/* === Test 3: riga VUOTA (line_len = 0) === */
static void test_empty_line(void) {
    int fd[2];
    assert(pipe(fd) == 0);

    mr_file_line_t in = {
        .file_name     = "f.txt",
        .file_name_len = 5,
        .line_number   = 3,
        .line          = "",
        .line_len      = 0,
    };
    assert(protocol_write_line(fd[1], &in) == 0);

    mr_line_msg_t out;
    assert(protocol_read_line(fd[0], &out) == 1);
    assert(out.line_len == 0);
    assert(out.file_name_len == 5);
    protocol_line_msg_free(&out);

    close(fd[0]); close(fd[1]);
}

/* === Test 4: coppia con valore opaco (byte arbitrari, incluso 0x00) ===
 * QUESTO E' IL TEST CRITICO: dimostra che il protocollo gestisce dati
 * binari, non solo stringhe C. */
static void test_pair_roundtrip_binary(void) {
    int fd[2];
    assert(pipe(fd) == 0);

    unsigned char bin[] = { 0x00, 0xFF, 0x42, 0x00, 0xAB, 0x7E };
    assert(protocol_write_pair(fd[1], "mytoken", 7, bin, sizeof bin) == 0);

    mr_pair_msg_t out;
    assert(protocol_read_pair(fd[0], &out) == 1);
    assert(out.token_len == 7);
    assert(strcmp(out.token, "mytoken") == 0);  /* token e' stringa C valida */
    assert(out.value_size == sizeof bin);
    assert(memcmp(out.value, bin, sizeof bin) == 0);  /* byte per byte */
    protocol_pair_msg_free(&out);

    close(fd[0]); close(fd[1]);
}

/* === Test 5: roundtrip risultato (struttura identica alla coppia) === */
static void test_result_roundtrip(void) {
    int fd[2];
    assert(pipe(fd) == 0);

    int answer = 42;
    assert(protocol_write_result(fd[1], "ultimate", 8, &answer, sizeof answer) == 0);

    mr_result_msg_t out;
    assert(protocol_read_result(fd[0], &out) == 1);
    assert(out.token_len == 8);
    assert(strcmp(out.token, "ultimate") == 0);
    assert(out.result_size == sizeof(int));
    assert(*(const int *)out.result == 42);
    protocol_result_msg_free(&out);

    close(fd[0]); close(fd[1]);
}

int main(void) {
    test_readn_writen();
    test_line_roundtrip();
    test_empty_line();
    test_pair_roundtrip_binary();
    test_result_roundtrip();
    printf("test_protocol: OK\n");
    return 0;
}
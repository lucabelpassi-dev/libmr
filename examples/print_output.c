/* examples/print_output.c
 *
 * Utility standalone (non linka libmr): legge un file di output del
 * framework e ne stampa il contenuto in forma leggibile. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int token_len;
    int result_len;
} record_header_t;

static ssize_t readn_fd(int fd, void *buf, size_t n) {
    size_t left = n; char *p = buf;
    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r == 0) break;
        if (r == -1) { if (errno == EINTR) continue; return -1; }
        left -= r; p += r;
    }
    return n - left;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <output_file>\n", argv[0]); return 1; }

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) { perror("open"); return 1; }

    record_header_t hdr;
    while (1) {
        ssize_t n = readn_fd(fd, &hdr, sizeof hdr);
        if (n == 0) break;
        if (n != (ssize_t)sizeof hdr || hdr.token_len < 0 || hdr.result_len < 0) {
            fprintf(stderr, "errore: file malformato\n");
            close(fd); return 1;
        }

        char *token  = malloc((size_t)hdr.token_len + 1);
        char *result = malloc((size_t)hdr.result_len + 1);
        readn_fd(fd, token,  (size_t)hdr.token_len);
        readn_fd(fd, result, (size_t)hdr.result_len);
        token[hdr.token_len]   = '\0';

        /* Heuristic: se il risultato e' lungo come un int, stampalo come int.
         * Altrimenti stampalo come stringa (con length-cap per sicurezza). */
        if (hdr.result_len == (int)sizeof(int)) {
            int v; memcpy(&v, result, sizeof v);
            printf("%s\t%d\n", token, v);
        } else {
            printf("%s\t%.*s\n", token, hdr.result_len, result);
        }

        free(token); free(result);
    }
    close(fd);
    return 0;
}
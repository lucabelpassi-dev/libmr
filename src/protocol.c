#include "protocol.h"
#include <errno.h>

ssize_t readn(int fd, void *ptr, size_t n) {
    size_t nleft = n;
    ssize_t nread;
    char *bufptr = ptr;

    while (nleft > 0) {
        if ((nread = read(fd, bufptr, nleft)) < 0) {
            if (errno == EINTR) nread = 0; // Interrotto da segnale, riprova
            else return -1;                // Errore reale
        } else if (nread == 0) break;      // EOF (Pipe chiusa dall'altra parte)

        nleft -= nread;
        bufptr += nread;
    }
    return (n - nleft);
}

ssize_t writen(int fd, const void *ptr, size_t n) {
    size_t nleft = n;
    ssize_t nwritten;
    const char *bufptr = ptr;

    while (nleft > 0) {
        if ((nwritten = write(fd, bufptr, nleft)) <= 0) {
            if (nwritten < 0 && errno == EINTR) nwritten = 0; // Riprova
            else return -1;                                   // Errore
        }
        nleft -= nwritten;
        bufptr += nwritten;
    }
    return n;
}
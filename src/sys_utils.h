#ifndef SYS_UTILS_H
#define SYS_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

// costanti
#define SYSCALL_ERROR -1

// macro 
#define SYSCALL(r, c, e) do { \
    if((r=c) == SYSCALL_ERROR){ \
        perror(e); \
        exit(EXIT_FAILURE); \
    } \
} while(0) 

#define PARENT_OR_CHILD(pid, f_parent, f_child) do{\
    if(pid == 0){ \
        f_child; \
    } \
    else{ \
        f_parent; \
    } \
}while(0)

#endif

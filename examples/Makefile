CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -I../include -pthread
LIB_DIR = ../lib
LDFLAGS = -L$(LIB_DIR) -pthread
LDLIBS = -lmr

EXAMPLES = wordcount print_output

.PHONY: all clean

all: $(EXAMPLES)

# wordcount linka libmr.a — è un consumatore della libreria
wordcount: wordcount.c $(LIB_DIR)/libmr.a
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

# print_output è standalone (legge solo il formato di output con length-prefix)
print_output: print_output.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(EXAMPLES) *.o
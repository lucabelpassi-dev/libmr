CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -I../include -pthread
LIB_DIR = ../lib
LDFLAGS = -L$(LIB_DIR) -pthread
LDLIBS = -lmr

EXAMPLES = wordcount print_output smoke

.PHONY: all clean

all: $(EXAMPLES)

# Esempi che consumano la libreria
wordcount: wordcount.c $(LIB_DIR)/libmr.a
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

smoke: smoke.c $(LIB_DIR)/libmr.a
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

# Standalone (legge solo il formato di output)
print_output: print_output.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(EXAMPLES) *.o mr.log
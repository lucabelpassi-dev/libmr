CC = gcc
# Qui sì che vediamo ../src — i test unitari accedono agli header interni
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -I../include -I../src -pthread
LIB_DIR = ../lib
LDFLAGS = -L$(LIB_DIR) -pthread
LDLIBS = -lmr

UNIT_TESTS = test_queue test_protocol test_attr test_group_table

.PHONY: all clean run

all: $(UNIT_TESTS)

# Test unitari: compilano direttamente i .c di src/ per isolare il modulo
test_queue: test_queue.c ../src/queue.c
	$(CC) $(CFLAGS) $^ -o $@

test_protocol: test_protocol.c ../src/protocol.c
	$(CC) $(CFLAGS) $^ -o $@

test_attr: test_attr.c $(LIB_DIR)/libmr.a
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

test_group_table: test_group_table.c ../src/group_table.c
	$(CC) $(CFLAGS) $^ -o $@

run: all
	@echo "── Unit tests ──"
	@./test_queue
	@./test_protocol
	@./test_attr
	@./test_group_table
	@echo "── End-to-end ──"
	@bash test_e2e.sh

clean:
	rm -f $(UNIT_TESTS) *.o
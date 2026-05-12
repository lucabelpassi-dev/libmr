CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -I./include -pthread

# Strumento per creare la libreria (Archiver)
AR = ar
# rcs = replace (aggiorna i file), create (crea l'archivio), sort (crea un indice veloce)
ARFLAGS = rcs

# Cartelle
SRC_DIR = src
BUILD_DIR = build
LIB_DIR = lib
TEST_DIR = tests

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# Il nostro obiettivo finale
TARGET = $(LIB_DIR)/libmr.a

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^
	@echo "✅ Libreria $@ creata con successo!"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Pulizia
clean:
	rm -rf $(BUILD_DIR)/* $(LIB_DIR)/*
	@echo "🧹 Pulizia completata."

# Test
test: $(TARGET)
	@echo "🧪 Esecuzione test..."
	@if [ -d $(TEST_DIR) ]; then \
		$(MAKE) -C $(TEST_DIR) run; \
	else \
		echo "⚠️  Cartella $(TEST_DIR)/ non esiste ancora — placeholder"; \
	fi


# Compilatore e flag
CC = gcc
# Aggiungiamo -pthread fondamentale per i thread C11 e POSIX!
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -I./include -pthread

# Strumento per creare la libreria (Archiver)
AR = ar
# rcs = replace (aggiorna i file), create (crea l'archivio), sort (crea un indice veloce)
ARFLAGS = rcs

# Cartelle
SRC_DIR = src
BUILD_DIR = build
LIB_DIR = lib

# 1. Trova tutti i file .c nella cartella src/
SRCS = $(wildcard $(SRC_DIR)/*.c)
# 2. Genera i nomi dei file .o sostituendo "src/" con "build/" e ".c" con ".o"
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# Il nostro obiettivo finale
TARGET = $(LIB_DIR)/libmr.a

# Regola di default (quando scrivi solo 'make')
all: $(TARGET)

# Regola per creare la libreria .a
$(TARGET): $(OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^
	@echo "✅ Libreria $@ creata con successo!"

# Regola per compilare ogni singolo file .c in .o
# L'opzione -c dice a gcc: "compila ma non cercare il main()"
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Pulizia
clean:
	rm -rf $(BUILD_DIR)/* $(LIB_DIR)/*
	@echo "🧹 Pulizia completata."
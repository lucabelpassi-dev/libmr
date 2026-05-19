CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -I./include -pthread
AR = ar
ARFLAGS = rcs

SRC_DIR = src
BUILD_DIR = build
LIB_DIR = lib
EXAMPLES_DIR = examples
TESTS_DIR = tests

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
TARGET = $(LIB_DIR)/libmr.a

.PHONY: all lib examples test clean relazione zip domande

# Default: libreria + esempi (così `make` da solo produce qualcosa di eseguibile)
all: lib examples

lib: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^
	@echo "✅ Libreria $@ creata"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

examples: lib
	@$(MAKE) -C $(EXAMPLES_DIR)

test: lib
	@$(MAKE) -C $(TESTS_DIR) run

clean:
	rm -rf $(BUILD_DIR)/* $(LIB_DIR)/*
	@$(MAKE) -C $(EXAMPLES_DIR) clean 2>/dev/null || true
	@$(MAKE) -C $(TESTS_DIR) clean 2>/dev/null || true
	@echo "🧹 Pulizia completata"

# Rigenerare relazione.pdf dal markdown
relazione: doc/relazione.md
	pandoc doc/relazione.md -o doc/relazione.pdf \
		--pdf-engine=xelatex -V geometry:margin=2.5cm --toc

zip: clean
	@echo "Creazione zip pulito..."
	@cd .. && zip -r belpassi_libmr.zip $(notdir $(CURDIR)) \
		-x '$(notdir $(CURDIR))/.git/*' \
		-x '$(notdir $(CURDIR))/.git' \
		-x '$(notdir $(CURDIR))/**/.DS_Store' \
		-x '$(notdir $(CURDIR))/mr.log' \
		-x '$(notdir $(CURDIR))/**/mr.log' \
		-x '$(notdir $(CURDIR))/**/*.o' \
		-x '$(notdir $(CURDIR))/**/*.a'
	@echo "Zip creato: ../belpassi_libmr.zip"
	@ls -lh ../belpassi_libmr.zip

# DA RIMUOVERE
domande: ../schemi/domande-orale.md
	pandoc ../schemi/domande-orale.md -o ../schemi/domande-orale.pdf \
		--pdf-engine=xelatex -V geometry:margin=2.5cm --toc

#!/bin/bash
# tests/test_e2e.sh
#
# Test end-to-end del framework libmr usando l'esempio wordcount.
# Per ogni caso di prova:
#   1) prepara input + output atteso
#   2) esegue wordcount
#   3) decodifica il file di output con print_output
#   4) confronta con diff
#
# Esce con codice 0 se tutti i test passano, 1 altrimenti.

set -u

WORDCOUNT=../examples/wordcount
PRINT_OUT=../examples/print_output

if [[ ! -x "$WORDCOUNT" ]]; then
    echo "ERRORE: $WORDCOUNT non trovato o non eseguibile (hai fatto 'make'?)"
    exit 2
fi
if [[ ! -x "$PRINT_OUT" ]]; then
    echo "ERRORE: $PRINT_OUT non trovato o non eseguibile"
    exit 2
fi

TEST_DIR=$(mktemp -d)
trap "rm -rf $TEST_DIR" EXIT

PASS=0
FAIL=0

# ============================================================
# Helper: lancia un test e confronta output
#   $1 = nome del test
#   $2 = file di input (o directory)
#   $3 = file con l'output atteso
# ============================================================
run_test() {
    local name="$1"
    local input="$2"
    local expected="$3"

    local output="$TEST_DIR/${name}.bin"
    local actual="$TEST_DIR/${name}.actual"

    if ! "$WORDCOUNT" "$input" "$output" 2>/dev/null; then
        echo "  [$name] FAIL — wordcount ha terminato con errore"
        FAIL=$((FAIL+1)); return
    fi

    "$PRINT_OUT" "$output" > "$actual"

    if diff -q "$expected" "$actual" > /dev/null; then
        echo "  [$name] OK"
        PASS=$((PASS+1))
    else
        echo "  [$name] FAIL"
        echo "    atteso:"
        sed 's/^/      /' "$expected"
        echo "    ottenuto:"
        sed 's/^/      /' "$actual"
        FAIL=$((FAIL+1))
    fi
}

# ============================================================
# Test 1: caso base
# ============================================================
IN="$TEST_DIR/t1_in"; EXP="$TEST_DIR/t1_exp"
cat > "$IN" <<EOF
ciao mondo
hello world
ciao
EOF
cat > "$EXP" <<EOF
ciao	2
hello	1
mondo	1
world	1
EOF
run_test "base" "$IN" "$EXP"

# ============================================================
# Test 2: file vuoto -> output vuoto (spec §4)
# ============================================================
IN="$TEST_DIR/t2_in"; EXP="$TEST_DIR/t2_exp"
> "$IN"
> "$EXP"
run_test "empty_file" "$IN" "$EXP"

# ============================================================
# Test 3: ultima riga senza \n (spec §4)
# ============================================================
IN="$TEST_DIR/t3_in"; EXP="$TEST_DIR/t3_exp"
printf "alpha beta alpha" > "$IN"   # nessun newline finale
cat > "$EXP" <<EOF
alpha	2
beta	1
EOF
run_test "no_trailing_newline" "$IN" "$EXP"

# ============================================================
# Test 4: punteggiatura come separatore
# ============================================================
IN="$TEST_DIR/t4_in"; EXP="$TEST_DIR/t4_exp"
cat > "$IN" <<EOF
hello, world! hello? "world"
EOF
cat > "$EXP" <<EOF
hello	2
world	2
EOF
run_test "punctuation_separator" "$IN" "$EXP"

# ============================================================
# Test 5: directory come input (spec §4)
# Tre file in ordine lessicografico devono essere processati tutti.
# ============================================================
DIR="$TEST_DIR/t5_dir"; mkdir -p "$DIR"
echo "alpha" > "$DIR/a.txt"
echo "beta"  > "$DIR/b.txt"
echo "alpha gamma" > "$DIR/c.txt"
EXP="$TEST_DIR/t5_exp"
cat > "$EXP" <<EOF
alpha	2
beta	1
gamma	1
EOF
run_test "directory_input" "$DIR" "$EXP"

# ============================================================
# Test 6: determinismo (spec §8)
# Eseguito due volte sullo stesso input deve produrre file IDENTICI.
# ============================================================
IN="$TEST_DIR/t6_in"
seq 1 200 | awk '{print "word" ($1 % 17), "word" ($1 % 23)}' > "$IN"
OUT1="$TEST_DIR/t6_run1.bin"
OUT2="$TEST_DIR/t6_run2.bin"

"$WORDCOUNT" "$IN" "$OUT1" 2>/dev/null
"$WORDCOUNT" "$IN" "$OUT2" 2>/dev/null

if cmp -s "$OUT1" "$OUT2"; then
    echo "  [determinism] OK"
    PASS=$((PASS+1))
else
    echo "  [determinism] FAIL — output differente tra due esecuzioni"
    FAIL=$((FAIL+1))
fi

# ============================================================
# Riepilogo
# ============================================================
echo
echo "test_e2e: $PASS passati, $FAIL falliti"

if [[ $FAIL -gt 0 ]]; then exit 1; fi
exit 0
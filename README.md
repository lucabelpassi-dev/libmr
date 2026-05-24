# libmr

Framework MapReduce a singolo nodo in C, sviluppato come progetto del corso
**Laboratorio II A**, A.A. 2025/2026.

`libmr` è una libreria statica (`libmr.a`) che permette al programma cliente di
eseguire una computazione MapReduce specificando due funzioni di callback
(_mapper_ e _reducer_) e i percorsi del file di input e di quello di output.
Internamente la libreria orchestra tre processi cooperanti (`main`, `mapper`,
`reducer`) comunicanti tramite pipe POSIX, ciascuno multithreaded con thread
C11.

## Struttura della directory

```
progetto/
├── include/mr.h          Header pubblico della libreria
├── src/                  Sorgenti della libreria
├── examples/             Esempi di utilizzo (smoke, wordcount, print_output)
├── tests/                Test unitari e end-to-end
├── doc/relazione.pdf      Relazione del progetto
├── Makefile              Build orchestrator
└── README.md             Questo file
```

## Compilazione

Dalla radice del progetto:

```sh
make            # compila la libreria e tutti gli eseguibili
make test       # esegue i test unitari e end-to-end
make clean      # pulisce gli artefatti di build
```

I sorgenti vengono compilati con:
- standard `C11` (`-std=c11`)
- warning rigorosi (`-Wall -Wextra -Wpedantic`)
- ottimizzazione `-O2`
- features POSIX (`-D_POSIX_C_SOURCE=200809L`)

L'output del build è:
- `lib/libmr.a` — la libreria statica
- `examples/wordcount`, `examples/smoke`, `examples/print_output` — eseguibili di esempio

## Utilizzo: esecuzione manuale degli esempi

Il framework include due esempi che usano `libmr.a` come libreria
statica: `wordcount` (conta le occorrenze di ogni parola alfanumerica
nel file di input) e `smoke` (test minimale di sanità).
Lo strumento `print_output` permette di decodificare in chiaro il file
di output binario.

### Wordcount: il caso d'uso canonico

```sh
make
echo "il gatto sul tetto. il gatto miagola." > /tmp/test.txt
./examples/wordcount /tmp/test.txt /tmp/output.bin
./examples/print_output /tmp/output.bin
# Atteso:
#   gatto    2
#   il       2
#   miagola  1
#   sul      1
#   tetto    1
```

I due argomenti di `wordcount` sono il percorso del file di input
(o di una directory contenente piu' file regolari) e il percorso del
file di output. L'output viene prodotto in formato binario
length-prefixed e va decodificato con `print_output`.

### Sull'estensione del file di output

Il framework scrive byte sul file, indipendentemente dall'estensione.
Convenzionalmente si usa `.bin` per rendere evidente che il file e'
binario. Aprire il file con un editor di testo mostrera' parti
leggibili (i token alfanumerici) e parti non stampabili (le lunghezze
e i valori binari): e' atteso. Per ispezionare il contenuto byte
per byte usare `xxd` o `hexdump`.

### Input come directory

Se il primo argomento e' una directory, il framework legge in ordine
lessicografico tutti i file regolari contenuti, processandoli come
se fossero concatenati:

```sh
mkdir -p /tmp/inputs
echo "alpha beta"   > /tmp/inputs/a.txt
echo "alpha gamma"  > /tmp/inputs/b.txt
./examples/wordcount /tmp/inputs /tmp/output.bin
./examples/print_output /tmp/output.bin
# alpha    2
# beta     1
# gamma    1
```

### Smoke test minimale

Lo stesso pattern vale per il programma `smoke`, utile per verificare
rapidamente la pipeline su un input piccolo:

```sh
echo "ciao mondo" > /tmp/smoke.txt
./examples/smoke /tmp/smoke.txt /tmp/smoke_out.bin
./examples/print_output /tmp/smoke_out.bin
```

### Ispezione del log

Ogni esecuzione produce un file `mr.log` nella directory di lavoro
corrente (cwd). Le righe sono ordinabili cronologicamente:

```sh
sort mr.log
```

## Logging

A ogni esecuzione viene prodotto un file `mr.log` nella directory di lavoro
contenente le tappe principali dell'esecuzione (creazione processi, avvio dei
thread, conteggi degli eventi, terminazione). Il file e' scritto in modo
serializzato fra tutti i processi tramite un semaforo POSIX named ed e' un
formato di testo ASCII a colonne fisse. Per ordinarlo cronologicamente:

```sh
sort mr.log
```

## Test

I test unitari verificano i singoli moduli (coda thread-safe, protocollo di
serializzazione, attributi, hash table per il raggruppamento). Il test
end-to-end esegue `wordcount` su input noti e confronta byte a byte il file di
output decodificato con un output atteso scritto a mano, coprendo file vuoti,
righe senza newline finale, punteggiatura come separatore, directory come
input e determinismo su esecuzioni ripetute.

```sh
make test
```

## Documentazione

La relazione completa (architettura, scelte progettuali, formati, test) si
trova in `doc/relazione.pdf`
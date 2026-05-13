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
├── doc/relazione.md      Relazione del progetto
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

## Utilizzo: l'esempio canonico

Il programma `wordcount` legge un file di testo (o tutti i file regolari in una
directory) e produce in output il conteggio delle parole alfanumeriche
incontrate.

```sh
make
echo "hello world hello" > /tmp/in.txt
./examples/wordcount /tmp/in.txt /tmp/out.bin
./examples/print_output /tmp/out.bin
# hello   2
# world   1
```

Il file di output è in formato binario length-prefixed. Lo strumento
`print_output` lo decodifica e stampa una rappresentazione testuale
`token<TAB>valore` per riga.

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
trova in `doc/relazione.md`. Per generare la versione PDF:

```sh
pandoc doc/relazione.md -o doc/relazione.pdf \
    --pdf-engine=xelatex \
    -V geometry:margin=2.5cm \
    --toc
```
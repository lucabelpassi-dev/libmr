# Domande e risposte — preparazione orale

Documento di ripasso per il progetto **libmr** (Laboratorio II A).
Raccoglie domande emerse rileggendo il codice, con risposte pronte da
ripercorrere prima dell'orale.

--- 

## 1. Sulle scelte di linguaggio e stile

### 1.1 `_POSIX_C_SOURCE=200809L`

**Domanda**: a lezione non l'abbiamo mai usato. È necessario?

**Risposta**: Sì, è **tecnicamente necessario**, non un vezzo stilistico.
È una *feature-test macro* standard del C: quando definita, dice agli
header (in particolare `<unistd.h>`) di esporre anche le funzioni
POSIX.1-2008 oltre a quelle ANSI C. Senza questa definizione, funzioni
come `fork()`, `pipe()`, `dup2()`, `getline()`, `scandir()`,
`localtime_r()` non sono dichiarate e il codice non compila in modalità
strict (`-std=c11`).

**Risposta-30-secondi all'orale**:
> "È una feature-test macro standard del C: dice all'header `<unistd.h>`
> di esporre le funzioni POSIX.1-2008. Senza, `fork`, `pipe`, `getline`
> non sarebbero dichiarate e con `-std=c11` rigoroso il codice non
> compila."

**Verifica concreta**: se rimuoviamo `-D_POSIX_C_SOURCE=200809L` da
`CFLAGS` (mantenendo `-std=c11`), il compilatore segnala
*"implicit declaration of getline"*, *"fork undeclared"*, ecc. Quindi
serve davvero.

---

### 1.2 `#include <dirent.h>`

**Domanda**: a lezione non l'abbiamo mai usato. Serve?

**Risposta**: Sì, è l'header POSIX standard per leggere il contenuto
delle directory: contiene la dichiarazione di `opendir`, `readdir`,
`scandir` e della `struct dirent`. Lo usiamo in `file_input.c` perché la
specifica §4 dice che `input_path` può essere una directory, e dobbiamo
elencarne il contenuto.

**Risposta-30-secondi all'orale**:
> "Header POSIX standard per l'enumerazione delle directory. Lo uso per
> `scandir` con `alphasort`, che combina enumerazione, filtro e
> ordinamento lessicografico in una chiamata. La specifica §4 ammette
> input directory, quindi serve."

---

### 1.3 `#include <limits.h>`

**Domanda**: a lezione non l'abbiamo mai usato.

**Risposta**: È header ANSI C standardissimo (esiste da prima ancora del
C89). Dà costanti come `INT_MAX`, `INT_MIN`, `SIZE_MAX`. Lo uso in
`protocol.c` per controllare se una lunghezza in `size_t` può andare in
overflow quando la castiamo a `int` (come richiesto dal formato del
nostro header di messaggio).

**Risposta-30-secondi all'orale**:
> "Header standard del C, mi dà `INT_MAX`. Lo uso in `protocol.c` per
> evitare overflow quando casto una lunghezza `size_t` a `int`, perché
> l'header del protocollo dichiara le lunghezze come `int`."

---

### 1.4 `EINVAL` e `errno`

**Domanda**: mai usato.

**Risposta**: `EINVAL`, `ENOMEM`, `EIO` ecc. sono **codici errno standard
POSIX**, dichiarati in `<errno.h>`. Sono lo strumento canonico per
riportare errori di funzioni di libreria. La specifica §A è esplicita:
le funzioni del framework "devono restituire `-1` in caso di errore,
impostando `errno`". Quindi non solo li usiamo, **siamo obbligati**.

**Risposta-30-secondi all'orale**:
> "`EINVAL` è il codice `errno` POSIX standard per 'argomento non
> valido'. Lo uso per rispettare la convenzione `return -1` + `errno`
> imposta dalla specifica §A."

---

### 1.5 I `goto cleanup`

**Domanda**: i `goto` sono "considerati dannosi", giusto?

**Risposta**: Sì in generale, **no per questo pattern specifico**. I
`goto cleanup` in `mr.c` e `file_input.c` sono:
- **sempre in avanti** (mai indietro),
- **sempre verso un'unica label** alla fine della funzione,
- **per raccogliere errori da più punti** e fare una pulizia idempotente
  delle risorse aperte.

Questo è il pattern "**goto cleanup**" (anche "single-exit error
handling"), usato sistematicamente nel kernel Linux e in tutti i progetti
C seri.

**Risposta-30-secondi all'orale**:
> "È il pattern 'goto cleanup' del kernel Linux. In C, senza eccezioni
> di C++ o try/finally di Java, è l'unico modo pulito per gestire
> risorse multiple in caso di errore senza nidificare cinque livelli di
> if. Salto sempre in avanti, sempre verso un'unica label di cleanup
> idempotente. Dijkstra parlava dei goto come salti arbitrari, non di
> questo schema disciplinato."

---

### 1.6 `static inline`

**Domanda**: mai usato.

**Risposta**: `static` limita la visibilità al file (è equivalente al
`private` di altri linguaggi); `inline` è un *suggerimento* al
compilatore di "incorporare" il corpo della funzione nel chiamante
invece di chiamarla. È un'ottimizzazione minore, utile per funzioni
piccole chiamate in loop stretti.

**Importante**: `inline` è un *hint*, non un ordine. Con `-O2`, il
compilatore decide comunque autonomamente se fare l'inlining; quindi
si può togliere senza impatto pratico.

**Risposta-30-secondi all'orale**:
> "`static` la rende visibile solo nel file. `inline` è un hint al
> compilatore di farne l'inlining perché è chiamata in un loop stretto.
> A `-O2` il compilatore decide comunque autonomamente, quindi è
> opzionale. L'ho messa per chiarezza di intento."

---

### 1.7 `assert.h`

**Domanda**: mai usato.

**Risposta**: `assert()` è una macro standard `<assert.h>` (C89) che
termina il programma se la condizione è falsa, stampando file e riga.
È **lo strumento canonico per i test unitari in C senza dipendenze
esterne**. Lo usiamo solo nei test (`tests/test_*.c`), MAI nella
libreria, perché in libreria gli errori si devono propagare al
chiamante, non terminare il processo.

**Risposta-30-secondi all'orale**:
> "`assert()` è il macro standard `<assert.h>`: se la condizione è
> falsa, stampa file e riga e termina con `abort()`. Lo uso nei test
> unitari per verificare invarianti senza dipendere da framework
> esterni. È compilato via `-DNDEBUG`."

---

### 1.8 La funzione hash FNV-1a

**Domanda**: cosa è questa hash function strana?

**Risposta**: FNV-1a (*Fowler-Noll-Vo* variante 1a) è una hash function
classica e semplice: scorre i byte uno per uno, fa XOR col byte corrente
e poi moltiplica per un primo "magico". 8 righe di codice. Per il
progetto va benissimo: distribuisce uniformemente token alfanumerici
sui bucket.

**Alternativa più semplice da spiegare**: hash con moltiplicatore 31
(quello di Java):
```c
size_t h = 0;
for (size_t i = 0; i < len; i++)
    h = h * 31 + (unsigned char)s[i];
return h % n_buckets;
```

**Risposta-30-secondi all'orale**:
> "FNV-1a è una hash function classica, otto righe di codice. Per ogni
> byte fa XOR col byte corrente e poi moltiplica per un numero primo,
> ottenendo buona distribuzione. Per i nostri carichi va benissimo;
> se diventasse un collo di bottiglia, si passerebbe a hash più
> sofisticati come xxHash o MurmurHash, ma non serve."

---

### 1.9 `_Thread_local`

**Domanda**: mai usato.

**Risposta**: `_Thread_local` è una **keyword standard C11** (insieme a
`mtx_t` che usiamo ovunque): dichiara una variabile con una **copia
indipendente per ciascun thread**. Diversa dalle variabili globali
"normali", che sono condivise fra tutti i thread del processo.

La usiamo in `log.c` per `g_thread_name`: ogni thread ha la sua copia,
quindi può chiamare `log_set_thread_name("worker-0")` senza interferire
con gli altri thread. Senza TLS dovremmo passare il nome come argomento
esplicito a ogni `log_event`.

**Risposta-30-secondi all'orale**:
> "Keyword standard C11 — esiste nello stesso standard di `mtx_t` e
> `thrd_create` che usiamo. Dichiara una variabile con una copia per
> thread, garantita dal runtime. Senza, dovrei passare il nome del
> thread come parametro esplicito a ogni `log_event`, e con decine di
> punti di chiamata sarebbe ridondante."

---

### 1.10 `bool` vs `int`

**Domanda**: mai usato `bool`.

**Risposta**: `bool` è dichiarato in `<stdbool.h>` (C99) come sinonimo
type-safe di `int 0/1`. È più leggibile ma non aggiunge niente di
tecnico rispetto a `int`. Nel nostro codice **abbiamo scelto `int`**
per stare aderenti al C K&R/C89 visto a lezione: `0` = falso, valore
diverso da 0 = vero, esattamente come `if` e `while` fanno
implicitamente.

**Esempio dal nostro `queue.c`**:
```c
int queue_pop(thread_queue_t *q, mr_value_t *out_value) {
    /* ... */
    return 0;   /* niente da pescare, coda chiusa */
    /* ... */
    return 1;   /* pesca riuscita */
}

/* Chiamante: */
while (queue_pop(&q, &item)) { ... }   /* funziona come bool, ma è int */
```

---

## 2. Sulla struttura del framework

### 2.1 Lo schema completo dei tre processi

Tre processi separati, ciascuno col suo spazio di memoria, comunicanti
solo via pipe POSIX unidirezionali:

```
   main  --[pipe1: righe]-->  mapper  --[pipe2: coppie]-->  reducer  --[pipe3: risultati]-->  main
```

Il main fa due `fork()` consecutive. **Dopo le fork**, i tre processi
sono indipendenti e non condividono memoria. Si parlano esclusivamente
attraverso le pipe.

---

### 2.2 Cosa fa il processo MAIN

Il main è **single-threaded** (un solo thread per tutta l'esecuzione).
In ordine:

1. Apre il file di output (mantiene il fd aperto).
2. Crea le 3 pipe.
3. `fork()` → crea il processo mapper.
4. `fork()` → crea il processo reducer.
5. Chiude i lati di pipe che non userà mai (**critico**: il padre deve
   chiudere ENTRAMBI i lati della pipe `mapper_to_reducer`, altrimenti
   il reducer non vede mai EOF).
6. Apre il file di input con `file_input_open`.
7. **Loop**: legge una riga, la serializza con `protocol_write_line`
   sulla pipe1 verso il mapper. Ripete finché ci sono righe.
8. Chiude il lato di scrittura della pipe1 (**questo segnala "fine
   input" al mapper via EOF**).
9. **Loop**: legge byte dalla pipe3 (risultati del reducer) e li
   scrive sul file di output. È un passthrough byte-per-byte: il
   formato sulla pipe e quello del file coincidono per design.
10. `waitpid` su entrambi i figli per evitare zombie e raccogliere
    lo status.
11. Cleanup di tutto.

---

### 2.3 Cosa fa il processo MAPPER

Il mapper ha **due tipi di thread** + una coda interna:

```
PROCESSO MAPPER

  pipe1 --> [reader thread]
                |
                | push
                v
          [   QUEUE    ]   <-- thread-safe, mtx + cnd
                |
                | pop
                v
     [worker-0] [worker-1] [worker-2] [worker-3]
                |
                v
        chiamano emit() --> protocol_write_pair --> pipe2 --> reducer
```

- **1 reader thread**: deserializza i messaggi-riga dalla pipe1
  (`protocol_read_line`) e fa `queue_push` nella coda interna.
  Quando vede EOF, fa `queue_close` e termina.
- **N worker thread** (configurabili via `mr_attr_set_mapper_threads`):
  fanno `queue_pop` (bloccante se la coda è vuota), costruiscono un
  `mr_file_line_t` con i puntatori "prestati" dal messaggio, e
  invocano la funzione mapper dell'utente. Quando l'utente chiama
  `emit`, una coppia viene serializzata e scritta sulla pipe2.

**Perché separare reader e worker**: l'I/O da pipe è bloccante e
sequenziale, ma il calcolo del mapper utente può essere parallelizzato.
Tenere un solo reader e N worker mette il "collo di bottiglia" sulla
lettura (intrinsecamente seriale) e parallelizza il resto.

---

### 2.4 Cosa fa il processo REDUCER

Il reducer ha anch'esso due tipi di thread, ma organizzati in
**tre fasi sequenziali** (non si sovrappongono nel tempo):

```
PROCESSO REDUCER

  FASE 1 - solo reader thread lavora:
    pipe2 --> [reader] --> group_table.insert()
                  |
            (EOF della pipe2)
                  v
            group_table.finalize()   <-- ordina i token lex

  FASE 2 - solo i worker lavorano (in parallelo):
            entries[] ordinato
                  |
       --------------------
       |        |         |
       v        v         v
   [worker-0][worker-1][worker-N]
       |        |         |
       --> chiamano reduce() --> scrivono in buffer locali (uno per entry)

  FASE 3 - solo il main thread del reducer lavora:
       scorre i buffer in ordine lex --> pipe3 --> main
```

**Perché tre fasi e non pipeline streaming**: per chiamare
`reduce("ciao", values)`, il framework deve aver raccolto **tutti** i
valori associati al token "ciao". Ma un valore di "ciao" può apparire
nell'ultima coppia letta dalla pipe2. Quindi non si può iniziare la fase
di reducing prima di aver letto **tutto** dalla pipe2.

**Determinismo dell'output (§8 della specifica)**: i worker scrivono
in buffer per-entry, non sulla pipe. Il flush finale è
single-threaded e in ordine lessicografico. Così l'output è
deterministico anche con worker paralleli.

---

### 2.5 In quali processi vive ciascun modulo

| Modulo | Vive in | Funzione |
|--------|---------|----------|
| `file_input.c` | solo MAIN | legge il file/directory di input riga per riga |
| `protocol.c` | TUTTI | serializza/deserializza i messaggi sulle pipe |
| `queue.c` | solo MAPPER | coda thread-safe fra reader e worker |
| `group_table.c` | solo REDUCER | hash table per raggruppare valori per token |
| `log.c` | TUTTI | scrittura concorrente del log via semaforo POSIX |
| `mr.c`, `mr_attr.c` | solo MAIN | orchestrazione: fork, pipe, lifecycle |
| `mapper_proc.c` | solo MAPPER | reader + worker + emit |
| `reducer_proc.c` | solo REDUCER | reader + grouping + worker + flush |

Nota importante: **`queue.c` è usata solo nel mapper**. Nel reducer non
c'è una coda: il reader inserisce direttamente nella `group_table`, e i
worker leggono direttamente dall'array già ordinato tramite un contatore
atomico (work stealing).

---

### 2.6 Quando viene scritto il log

**Continuamente, durante tutta l'esecuzione, da tutti e tre i processi
e da tutti i thread di ciascun processo.** Non c'è un momento specifico:
ogni evento significativo (creazione di una pipe, fork, avvio di un
thread, riga letta, coppia emessa, EOF ricevuto, terminazione di un
thread, terminazione di un processo) genera una riga di log nel momento
in cui accade.

La sincronizzazione cross-process è garantita dal **semaforo POSIX
named** (`/mr_log_<pid>`): tutti i thread di tutti i processi fanno
`sem_wait` prima di scrivere e `sem_post` dopo. Le righe non si
mescolano mai.

---

### 2.7 Ordine corretto: fork PRIMA dell'apertura del file di input

Sequenza giusta in `mr_start`:

1. Crea le pipe.
2. **Fa le due fork** (i tre processi sono ancora single-threaded).
3. Chiude i descrittori non usati nel padre.
4. **Solo ora** apre il file di input.
5. Inizia il loop di lettura+invio sulla pipe1.

**Perché in questo ordine**:
- I thread C11 si creano DOPO la fork (dentro `mapper_process_main`
  e `reducer_process_main`), per evitare lo stato inconsistente dei
  mutex dopo fork.
- Se il main aprisse il file di input prima delle fork, anche i
  figli erediterebbero quel file descriptor inutilmente; chiusurarlo
  in ciascun figlio è un fastidio evitabile.

---

## 3. Sull'API pubblica

### 3.1 `mr_start`: puntatori vs "stringhe"

**Domanda**: nel codice passiamo `argv[1]` e `argv[2]`, mentre il prof
in slide scrive `mr_start(mr, "input", "output.mro")`. Sono cose
diverse?

**Risposta**: No, sono **identiche**. In C non esiste un tipo "stringa":
una stringa è sempre un `char *` (o `const char *`) che punta a una
sequenza di byte terminata da `\0`.

Quando si scrive `"output.mro"` in mezzo al codice, il compilatore
mette quei byte in una zona di memoria read-only del binario, e
restituisce un `const char *` che punta a quella zona. Quindi le tre
forme che seguono sono **identiche al livello di firma**:

```c
mr_start(mr, "input.txt", "output.mro");   /* stringhe letterali */

const char *in = "input.txt";
const char *out = "output.mro";
mr_start(mr, in, out);                      /* puntatori a stringhe */

mr_start(mr, argv[1], argv[2]);             /* puntatori da argv */
```

Tutte e tre passano `const char *` a `mr_start`.

**Sull'estensione `.mro`**: è solo un nome di file inventato dal prof
nei suoi esempi (probabilmente "**M**ap**R**educe **O**utput"). Il
framework non impone nessuna estensione specifica. Nel nostro `smoke.c`
lo chiamiamo `output.bin`. Sarebbe indifferente chiamarlo
`output.mro`, `output.dat`, o `pippo`.

**Risposta-30-secondi all'orale**:
> "In C una stringa è un `const char *`. Quando scrivo `\"output.mro\"`
> in codice, il compilatore mette quei byte in memoria read-only e
> mi dà il puntatore. Quindi `mr_start(mr, \"a\", \"b\")` e
> `mr_start(mr, argv[1], argv[2])` hanno la stessa firma: due
> `const char *`. Sono notazioni diverse per la stessa cosa."

---

### 3.2 Il meccanismo `emit` con `emit_arg` opaco

**Il problema risolto**: l'utente che scrive una funzione mapper deve
poter "emettere" coppie verso il reducer, ma **non deve sapere** che
internamente esistono pipe, file descriptor, mutex di sincronizzazione.
Quei dettagli sono privati del framework.

**Soluzione**: il framework presta all'utente, al momento della
chiamata della mapper, due cose:

1. **Una funzione `emit`** (puntatore a funzione) che l'utente può
   chiamare per emettere una coppia.
2. **Un argomento opaco `emit_arg`** (un `void *`) che l'utente non
   deve interpretare, e che passerà pari pari a `emit` quando la
   chiama.

**Firma della mapper** (da `mr.h`):
```c
typedef int (*mr_mapper_t)(const mr_file_line_t *line,
                           mr_emit_pair_t emit,    /* funzione "emit" */
                           void *emit_arg,         /* argomento opaco */
                           void *user_arg);
```

**Esempio di mapper utente**:
```c
int my_mapper(const mr_file_line_t *line,
              mr_emit_pair_t emit, void *emit_arg, void *user_arg) {
    int one = 1;
    char token[64];

    /* L'utente estrae una parola in token... */

    /* Emette: chiama emit passandogli token, valore, e l'emit_arg
       opaco che il framework gli ha dato. */
    emit(token, &one, sizeof(one), emit_arg);
    return 0;
}
```

**Cosa fa il framework dietro le quinte** (in `mapper_proc.c`):
```c
static int mapper_emit(const char *token,
                       const void *value, size_t value_size,
                       void *emit_arg) {
    /* emit_arg, dietro il velo opaco, è il puntatore al nostro
       mapper_state. Lo castiamo indietro al tipo vero. */
    struct mapper_state *st = (struct mapper_state *)emit_arg;

    /* Ora abbiamo accesso a tutto: fd verso il reducer, mutex, ecc. */
    mtx_lock(&st->out_mutex);
    int rc = protocol_write_pair(st->reducer_fd, token, /*...*/,
                                 value, value_size);
    mtx_unlock(&st->out_mutex);
    return rc;
}

/* Quando il worker invoca la mapper utente: */
st->mapper_fn(&line,           /* la riga da elaborare */
              mapper_emit,     /* prestiamo NOI STESSI come funzione */
              st,              /* prestiamo IL NOSTRO STATO come emit_arg opaco */
              st->user_arg);   /* l'argomento utente */
```

**L'analogia del francobollo**: stai scrivendo a un servizio clienti.
Loro ti danno un modulo da compilare *e una busta affrancata pre-stampata
col loro indirizzo* (che tu non conosci). Tu compili il modulo, lo metti
in busta, imbuchi. La busta sa dove andare anche se tu non lo sai.

Nel nostro caso:
- **Tu** = utente (la funzione mapper)
- **Modulo** = token + valore
- **Busta affrancata** = la funzione `emit` + il puntatore `emit_arg`
- **Indirizzo nascosto sulla busta** = il fatto che `emit_arg` punti
  al `mapper_state` con dentro il file descriptor verso il reducer

**Perché tutto questo casino**: disaccoppiamento. L'utente scrive codice
indipendente dai dettagli interni del framework. Se domani cambiassimo
la pipe con un socket TCP, la firma di `emit` rimane identica, l'utente
non si accorge di niente. Stesso principio dei syscall del sistema
operativo.

**Risposta-30-secondi all'orale**:
> "`emit` è la funzione che il framework presta alla callback utente
> per emettere coppie chiave-valore. È fatta così perché l'utente non
> deve conoscere i dettagli interni — pipe, file descriptor,
> sincronizzazione. Il framework gli dà una funzione + un puntatore
> opaco `emit_arg`; l'utente la chiama, e dentro la nostra
> implementazione di `emit` castiamo l'argomento opaco al tipo vero
> (un `struct mapper_state *`), recuperando l'accesso al file
> descriptor della pipe verso il reducer e al mutex di sincronizzazione.
> È il pattern di iniezione di contesto in una callback in C,
> concettualmente equivalente al `this` implicito degli oggetti in C++
> o Java."

**Lo stesso identico meccanismo vale per il reducer**: c'è una
`reducer_emit` che fa l'analogo per i risultati.

---

### 3.3 `emit` rispiegato dal problema

Versione alternativa alla 3.2, costruita partendo dal *problema* anziché
dalla *soluzione*. Utile se la spiegazione "francobollo" non scatta.

#### Il problema concreto

L'utente che scrive una mapper deve produrre coppie chiave-valore. Come
fa, concretamente, dentro la funzione?

```c
int my_mapper(const mr_file_line_t *line, ...) {
    // ho la riga in line->line
    // voglio produrre la coppia ("parola", 1)
    // ...come?
}
```

Cosa significa "produrre"? Dove va a finire? Chi la riceve?

#### Le tre alternative possibili

**Alternativa A — `return` di un array**:
```c
mr_pair_t *my_mapper(const mr_file_line_t *line);
```
Problema: una mapper può produrre 0, 1, 100 coppie. Il chiamante non sa
quante. Servirebbe ritornare anche la lunghezza, gestire l'allocazione,
liberare la memoria. Scomodo.

**Alternativa B — variabile globale**:
```c
mr_pair_t global_output[1000000];
int global_count = 0;
int my_mapper(...) { global_output[global_count++] = ...; }
```
Disastroso: dimensione fissa, non thread-safe, contraria a ogni buon
senso.

**Alternativa C — una funzione che l'utente chiama "ad ogni coppia"**:
```c
int my_mapper(...) {
    produci_coppia("parola1", &uno, sizeof(int));
    produci_coppia("parola2", &uno, sizeof(int));
}
```
Vincente. L'utente non torna niente, non riempie array, non scrive su
globali. Chiama una funzione per ogni coppia. Questa funzione si
chiama `emit`.

L'utente non sa, e non gli interessa, cosa fa `emit`: potrebbe scrivere
su pipe, rete, file, memoria. Sa solo che `emit(...)` produce una
coppia.

#### Sotto-problema: `emit` deve sapere dove scrivere

Dentro `emit` vogliamo scrivere sulla pipe verso il reducer. Come fa
`emit` a sapere il file descriptor?

```c
int emit(const char *token, const void *value, size_t value_size) {
    write(???, ...);   // qual è il fd?
}
```

`emit` è una funzione C: ha solo i suoi parametri. Non ha "stato suo".
Non sa "in che contesto" viene chiamata.

#### Soluzione: passare il contesto come quarto parametro

Aggiungiamo a `emit` un puntatore generico che contiene "tutto quello
che serve":

```c
int emit(const char *token, const void *value, size_t value_size,
         void *emit_arg) {
    struct mapper_state *st = (struct mapper_state *)emit_arg;
    write(st->reducer_fd, ...);
}
```

`emit_arg` è un `void *`: puntatore opaco. Dietro c'è il nostro
`mapper_state`, che lo casta dentro `emit`.

**Chi passa `emit_arg` a `emit`? L'utente.**
**Come fa l'utente a sapere cosa metterci? Non lo sa.**

Il framework gli dà sia la funzione `emit` sia il valore di `emit_arg`,
e gli dice: *"quando chiami emit, passagli questo `emit_arg`, non farti
domande"*.

#### Mapper riscritta con `emit_arg`

```c
int my_mapper(const mr_file_line_t *line,
              mr_emit_pair_t emit,    // funzione prestata dal framework
              void *emit_arg,         // argomento opaco prestato dal framework
              void *user_arg) {
    int one = 1;
    emit("parola1", &one, sizeof(one), emit_arg);  // pari pari
    emit("parola2", &one, sizeof(one), emit_arg);
}
```

L'utente **non guarda dentro `emit_arg`**, **non lo modifica**,
**non lo crea**. Lo riceve dal framework e lo ripassa a `emit`.
Trasparente per lui.

#### Lato framework, quando invochiamo la mapper

```c
struct mapper_state *st = ...;  // il nostro stato condiviso

st->mapper_fn(&line,        // riga
              mapper_emit,  // ← prestiamo NOI STESSI come emit
              st,           // ← prestiamo il nostro stato come emit_arg
              st->user_arg);
```

E `mapper_emit`:
```c
static int mapper_emit(const char *token, const void *value, size_t value_size,
                       void *emit_arg) {
    struct mapper_state *st = (struct mapper_state *)emit_arg;
    // accesso a: st->reducer_fd, st->out_mutex, ecc.
    mtx_lock(&st->out_mutex);
    protocol_write_pair(st->reducer_fd, ...);
    mtx_unlock(&st->out_mutex);
}
```

#### Perché ci ostiniamo con questo schema

Senza `emit`, l'utente dovrebbe scrivere così:
```c
int my_mapper(int reducer_fd, mtx_t *mutex, ...) {
    mtx_lock(mutex);
    write(reducer_fd, ...);  // formato length-prefixed
    mtx_unlock(mutex);
}
```

Dovrebbe conoscere: il file descriptor del reducer, il mutex, il
formato del protocollo, come gestire EINTR, come fare writen. Disastro
di astrazione: il giorno che cambiassimo internamente le pipe con i
socket, ogni mapper utente esistente andrebbe riscritta.

Con `emit`: l'utente sa solo *"per emettere una coppia, chiama questa
funzione"*. Tutto il resto è nascosto. Possiamo cambiare l'implementazione
interna domani senza toccare il codice utente. Stesso principio dei
syscall del sistema operativo verso le applicazioni.

#### Riassunto in 4 punti

1. **Problema**: l'utente nella mapper deve produrre coppie ma non
   deve sapere dove finiscono.
2. **Soluzione**: il framework presta all'utente una funzione `emit`
   che lui chiama per ogni coppia.
3. **Sotto-problema**: `emit` deve sapere il file descriptor della pipe
   verso il reducer, ma è solo una funzione, non ha stato proprio.
4. **Soluzione del sotto-problema**: il framework presta all'utente
   un secondo oggetto, un puntatore opaco `emit_arg`, da passare a
   `emit` ogni volta. Dietro al velo del `void *`, `emit_arg` è il
   `mapper_state` con dentro il fd. `emit` lo casta e ritrova il contesto.

#### Risposta-30-secondi all'orale (versione semplificata)

> "`emit` è la funzione che l'utente chiama nella sua mapper per
> produrre coppie chiave-valore destinate al reducer. È fatta così
> perché l'utente non deve conoscere i dettagli interni del framework
> (pipe, file descriptor, mutex). Quindi il framework gli presta due
> cose: la funzione `emit` stessa e un puntatore opaco `emit_arg`.
> L'utente chiama `emit(token, valore, sizeof, emit_arg)` senza
> interpretare l'emit_arg. Dentro la nostra implementazione di emit
> (`mapper_emit`), castiamo `emit_arg` al tipo vero — un
> `struct mapper_state *` — e così ritroviamo l'accesso al file
> descriptor e al mutex per scrivere la coppia sulla pipe verso
> il reducer."

---

### 3.4 Il `64` di `mr_attr_set_queue_size`

**Domanda**: nel default `attr->queue_size = 64`, perché 64?

**Risposta**: `queue_size` rappresenta il **numero massimo di righe**
che la coda interna del mapper *dovrebbe* contenere prima di bloccare
il reader. È un buffer di smorzamento fra le due velocità (reader della
pipe vs worker che processano).

Il valore `64` è scelto come **default ragionevole**:
- abbastanza grande da assorbire piccoli burst di righe lette in rapida
  sequenza,
- abbastanza piccolo da non sprecare memoria (64 puntatori = qualche
  centinaio di byte).

`64` è una potenza di 2 per convenzione informatica, non per ragioni
tecniche: la nostra coda è una lista linkata, non un array circolare,
quindi le potenze di 2 non danno vantaggi di indirizzamento.

Avrebbe potuto essere `32`, `100`, `128`: nessuna differenza pratica per
i carichi del progetto.

**ONESTÀ BRUTALE — DA SAPERE PER L'ORALE**:

Nella nostra implementazione attuale, **`queue_size` è dichiarato negli
attributi ma non viene effettivamente usato per limitare la coda**. Se
guardi `queue.c`, la coda è una lista linkata che cresce indefinitamente
con `queue_push`; non c'è un controllo di soglia.

Quindi:
- il campo esiste nella `mr_attr_t` (richiesto dalla specifica),
- l'utente può chiamare `mr_attr_set_queue_size` (l'API c'è),
- ma il limite non viene effettivamente rispettato in pratica.

È una delle **"inefficienze volute"** che il prof ha annunciato. Se il
prof lo nota, è onesto riconoscerlo e descrivere come si correggerebbe.

**Risposta-30-secondi all'orale**:
> "`queue_size` è il valore di default della dimensione massima
> della coda interna del mapper, scelto come compromesso pratico fra
> capacità di smorzamento dei burst di lettura e occupazione di
> memoria. Onestamente però la nostra coda è implementata come lista
> linkata illimitata, quindi questo valore non viene rispettato in
> pratica — è un campo esposto dall'API per coerenza con la
> specifica. Una versione completa userebbe un buffer circolare
> bounded e bloccherebbe `queue_push` quando il limite è raggiunto."

---

## 4. Sul processo reducer in dettaglio

### 4.1 Fase 2: distribuzione del lavoro fra i worker

**Scenario di partenza** (dopo la Fase 1). La `group_table` è stata
finalizzata: tutti i gruppi sono in un array `entries[]` ordinato
lessicograficamente per token.

```
entries[] = [
    { "alpha",  values=[...], count=3 },   // indice 0
    { "beta",   values=[...], count=1 },   // indice 1
    { "ciao",   values=[...], count=5 },   // indice 2
    { "delta",  values=[...], count=2 },   // indice 3
    { "zeta",   values=[...], count=1 },   // indice 4
]
entries_count = 5
```

I gruppi sono **indipendenti**: per produrre i risultati di "alpha" non
serve sapere niente di "beta". Idealmente quindi vorremmo elaborarli
in parallelo con i worker.

**Domanda**: come distribuiamo 5 gruppi fra 4 worker?

#### Soluzione: work stealing con contatore atomico

Un singolo contatore globale `next_entry_idx` (parte da 0). Ogni
worker, quando libero, fa una **fetch-and-increment atomica**:

```c
size_t i = atomic_fetch_add(&st->next_entry_idx, 1);
if (i >= st->entries_count) break;   // niente più gruppi disponibili
// processa entries[i]
```

L'operazione "leggi-e-incrementa" è **atomica**: il processore garantisce
che se due thread la fanno contemporaneamente, ognuno ottiene un valore
diverso e il contatore avanza di 2. Nessun thread vede mai lo stesso
indice di un altro.

**Esempio di run con 4 worker e 5 gruppi**:
```
T0: worker-0 → i=0 (processa "alpha")
T0: worker-1 → i=1 (processa "beta")
T0: worker-2 → i=2 (processa "ciao")
T0: worker-3 → i=3 (processa "delta")
T1: worker-1 finisce per primo ("beta" era veloce, 1 valore)
T1: worker-1 → i=4 (processa "zeta")
T2: worker-3 finisce → i=5, ma 5 >= 5 → termina
T2: worker-0 finisce → i=6 → termina
T2: worker-2 finisce → i=7 → termina
T2: worker-1 finisce → i=8 → termina
```

**Vantaggio del work stealing dinamico**: nessun worker aspetta
inutilmente. Se "ciao" ha 1000 valori da processare, worker-2 ci lavora
a lungo, ma gli altri continuano a prendere gruppi rapidi. Carico
bilanciato dinamicamente senza prepartizionare nulla.

**Risposta-30-secondi all'orale**:
> "Distribuzione dinamica con un singolo contatore atomico. Ogni
> worker fa `atomic_fetch_add(&next_idx, 1)` per prenotare il prossimo
> gruppo. L'atomicità è del processore, non un mutex applicativo:
> molto più leggera. Schema noto come *work stealing dinamico*:
> bilancia il carico anche se i gruppi hanno costi di elaborazione
> eterogenei."

---

### 4.2 I buffer per-entry: cosa sono, dove vivono, cosa contengono

**Domande tipiche che ci si fa rileggendo il codice**:
- *"Un buffer per thread o per gruppo?"*
- *"Cosa contiene il buffer?"*
- *"Come è organizzato?"*

#### Un buffer **per entry** (cioè per gruppo, cioè per token distinto)

Se `entries[]` ha 5 gruppi, ci sono **5 buffer**. Vivono in tre array
paralleli dentro `reducer_state`, ciascuno di dimensione `entries_count`:

```c
struct reducer_state {
    /* ... */
    char  **out_buffers;       // out_buffers[i] = buffer del gruppo i
    size_t *out_buffer_lens;   // out_buffer_lens[i] = byte usati
    size_t *out_buffer_caps;   // out_buffer_caps[i] = capienza attuale
};
```

Mappa concettuale:
```
out_buffers[0]    → buffer dei risultati per "alpha"
out_buffer_lens[0] = N byte usati
out_buffer_caps[0] = M byte di capienza (M >= N)

out_buffers[1]    → buffer dei risultati per "beta"
... ecc.
```

**Inizialmente tutti i buffer sono NULL e di lunghezza 0**. Crescono
solo quando il worker che processa quel gruppo chiama `emit` e ci scrive.

#### Cosa contiene il buffer

I **byte già serializzati nel formato del protocollo dei risultati**
(header `mr_result_header_t` + token + result), pronti per essere
scritti sulla pipe3 senza ulteriori trasformazioni.

Esempio: `wc_reducer` per "alpha" emette un solo risultato (total=3 come
`int`). Dentro `reducer_emit`:

```c
mr_result_header_t hdr = {
    .token_len  = 5,     // strlen("alpha")
    .result_len = 4,     // sizeof(int)
};

size_t total = sizeof(hdr) + token_len + result_size;
            //  8         + 5          + 4 = 17 byte

// Cresce il buffer se necessario (realloc raddoppiante)
if (*ec->len_p + total > *ec->cap_p) {
    *ec->cap_p = (*ec->cap_p == 0) ? 256 : *ec->cap_p * 2;
    *ec->buf_p = realloc(*ec->buf_p, *ec->cap_p);
}

// Append: header, token, result
char *dst = *ec->buf_p + *ec->len_p;
memcpy(dst, &hdr, sizeof hdr);         dst += sizeof hdr;
memcpy(dst, "alpha", 5);                dst += 5;
memcpy(dst, &total, 4);
*ec->len_p += total;
```

Risultato in `out_buffers[0]`:
```
byte:  0  1  2  3  4  5  6  7   8  9 10 11 12  13 14 15 16
      [ token_len=5  ][result_len=4 ][a][l][p][h][a][3] [0][0][0]
       \---- header (8 byte) ----/   \--token--/  \-int 3 LE-/
```

17 byte di payload "wire-format", esattamente nello stesso formato con
cui finiranno sul file di output. **Già pronti per la trasmissione.**

#### Perché un buffer per entry e non per worker

Per garantire l'ordine deterministico nell'output. Se ogni worker
avesse un suo buffer, alla fine dovremmo riordinare i risultati prima
del flush, perché un worker può aver lavorato su gruppi in ordine
sparso (es. worker-0 ha fatto gli indici 0, 4, 7). Con un buffer per
entry, l'ordine è implicito nell'indice: scorrere `i` da 0 a
`entries_count-1` è già scorrere in ordine lex.

---

### 4.3 Sincronizzazione fra i worker reducer (spoiler: quasi nulla)

**I worker reducer NON usano mutex** fra di loro per le scritture.
Perché?

- `entries[]` è in **sola lettura**: il worker legge `entries[i]->token`
  e `entries[i]->values` ma non li modifica.
- `out_buffers[i]`, `out_buffer_lens[i]`, `out_buffer_caps[i]` sono
  accessi a **indici diversi** per ogni worker: due worker che lavorano
  sui gruppi 3 e 7 non si toccano mai nello stesso indirizzo di memoria.
- L'unica primitiva condivisa è il contatore atomico `next_entry_idx`,
  che usa l'atomicità del processore — niente mutex applicativo.

**L'unica vera sincronizzazione fra i worker** è il `thrd_join` che il
main thread del reducer fa al termine: aspetta che TUTTI i worker
abbiano finito prima di procedere alla Fase 3. Questo è un punto di
sincronizzazione "happens-before" garantito dal C11.

**Differenza col mapper**: nel mapper i worker condividono la pipe
verso il reducer (un'unica risorsa), quindi serve `out_mutex`. Nel
reducer i worker scrivono ognuno nel proprio buffer, **risorse
disgiunte**, niente mutex.

---

### 4.4 Fase 3: il flush ordinato

Dopo `thrd_join` di tutti i worker, il main thread del reducer ha:
```
out_buffers[0] → [bytes per "alpha"]   (17 byte)
out_buffers[1] → [bytes per "beta"]    (12 byte)
out_buffers[2] → [bytes per "ciao"]    (15 byte)
out_buffers[3] → [bytes per "delta"]   (14 byte)
out_buffers[4] → [bytes per "zeta"]    (13 byte)
```

Tutti in formato wire, indicizzati dalla posizione lex del token. Il
flush è banale:
```c
for (size_t i = 0; i < state.entries_count; i++) {
    if (state.out_buffer_lens[i] > 0) {
        writen(STDOUT_FILENO,                // = pipe3 verso il main
               state.out_buffers[i],
               state.out_buffer_lens[i]);
    }
}
```

In ordine: scrive prima il blob di "alpha", poi "beta", poi "ciao",
"delta", "zeta". Sulla pipe3 escono i record nello stesso ordine
lessicografico, **deterministico**.

Il main thread del reducer è single-threaded in questa fase. Il main
process dall'altro lato della pipe3 fa il passthrough byte-per-byte
sul file di output. **Il file finisce contenente i record in ordine
lex, deterministico, garantito.**

#### Domande tipiche da orale su questa fase

**Q**: *"Cosa succederebbe se chiudessimo i worker prima di flushare?"*
**A**: I buffer sarebbero leak: liberati nel cleanup finale ma mai
trasmessi al main, file di output vuoto. Il `thrd_join` di TUTTI i
worker prima del flush è obbligatorio.

**Q**: *"E se l'utente non chiamasse mai emit?"*
**A**: Il buffer del gruppo rimarrebbe vuoto (`out_buffer_lens[i] == 0`).
Nel flush, l'`if (lens[i] > 0)` lo salta. Quel token non appare
nell'output. Comportamento previsto.

**Q**: *"Quanto consuma in memoria?"*
**A**: Per ogni entry, il buffer cresce in proporzione ai risultati
emessi (con realloc raddoppiante). Per wordcount tipico, ogni buffer
è ~20 byte. Totale: `entries_count * ~20 byte`, niente.

**Q**: *"Perché i risultati di emit successive sullo stesso token sono
preservati nell'ordine?"*
**A**: Il buffer dell'entry è append-only: ogni emit appende in coda,
quindi l'ordine è quello delle chiamate dell'utente.

---

## 5. Sullo stato condiviso fra i thread

### 5.1 Cos'è `reducer_state` e dove vive

`reducer_state` è una **struct C che contiene tutto quello che i thread
del processo reducer hanno bisogno di vedere e condividere**: la
`group_table`, l'array delle entry finalizzate, i buffer per-entry, il
contatore atomico, la callback dell'utente.

```c
struct reducer_state {
    /* Fase 1 */
    group_table_t *table;

    /* Fase 2 */
    group_entry_t **entries;
    size_t          entries_count;

    char  **out_buffers;
    size_t *out_buffer_lens;
    size_t *out_buffer_caps;

    atomic_size_t next_entry_idx;

    /* Sempre */
    mr_reducer_t reducer_fn;
    void        *user_arg;
};
```

**Vive sullo stack del main thread del reducer**, dentro
`reducer_process_main`:

```c
int reducer_process_main(struct reducer_ctx *ctx) {
    struct reducer_state state = {     // ← stack
        .reducer_fn = ctx->reducer,
        .user_arg   = ctx->user_arg,
    };
    atomic_init(&state.next_entry_idx, 0);
    state.table = group_table_create(1024);

    /* ... lancia thread, gli passa &state ... */
    /* ... thrd_join ... */
    /* ... cleanup ... */
    return ret;
}
```

`state` è una variabile locale: vive sullo stack della funzione, viene
distrutta quando la funzione ritorna. Nel frattempo, tutti i thread
sono stati joinati prima del return, quindi nessuno tiene puntatori a
`state` dopo la sua morte.

---

### 5.2 Come la condividono i thread

Quando `reducer_process_main` crea un thread, gli passa **l'indirizzo
di `state`** come argomento:

```c
thrd_t reader;
thrd_create(&reader, reducer_reader_main, &state);
//                                         ^^^^^^
```

E `reducer_reader_main`, dentro, lo recupera con un cast:
```c
static int reducer_reader_main(void *arg) {
    struct reducer_state *st = (struct reducer_state *)arg;
    /* ... usa st->table, st->reducer_fn, ecc. */
}
```

Stessa cosa per i worker:
```c
for (size_t i = 0; i < ctx->num_threads; i++) {
    thrd_create(&workers[i], reducer_worker_main, &state);
}
```

**Tutti i thread di quel processo condividono lo stesso `state`**: tutti
hanno un puntatore allo stesso indirizzo di memoria. Modifiche fatte
da un thread sono visibili agli altri, con le dovute precauzioni di
sincronizzazione (come per il contatore atomico).

---

### 5.3 Perché non usiamo variabili globali

Tre motivi.

**1. Vivacità garantita**: `state` vive sullo stack della funzione che
ha creato i thread. Quella funzione fa `thrd_join` su tutti i thread
prima di tornare. `state` esiste **per tutto il tempo in cui esistono i
thread che la usano**. Zero possibilità di dangling pointer.

**2. Iniezione di contesto**: i thread C11 hanno firma fissa
`int (*)(void *)`. Un solo puntatore. Se vogliamo passare 7 cose
diverse, le mettiamo in una struct e passiamo il puntatore. Pattern
universale in C.

**3. Disaccoppiamento**: i "dati condivisi" sono in un solo posto
identificabile (`state`), invece che sparsi in globali. Se domani
volessi due reducer concorrenti nello stesso processo, basterebbe avere
due `state` diversi. Con le globali, no.

---

### 5.4 La relazione fra `state` e `emit_arg`

Quando il framework chiama il reducer dell'utente, gli passa un
`emit_arg`. Cosa gli passa?

**Non `&state` direttamente, ma una struct ausiliaria più specifica
dell'entry corrente:**

```c
struct reducer_emit_ctx {
    char   **buf_p;     /* indirizzo del puntatore al buffer */
    size_t  *len_p;     /* indirizzo della lunghezza corrente */
    size_t  *cap_p;     /* indirizzo della capacità corrente */
};
```

E dentro `reducer_worker_main`:
```c
group_entry_t *entry = st->entries[i];

struct reducer_emit_ctx ec = {
    .buf_p = &st->out_buffers[i],          // ← buffer del gruppo i
    .len_p = &st->out_buffer_lens[i],
    .cap_p = &st->out_buffer_caps[i],
};

int rc = st->reducer_fn(entry->token,
                        entry->values, entry->values_count,
                        reducer_emit, &ec,       // ← emit_arg è &ec
                        st->user_arg);
```

**Perché due strutture invece di passare direttamente `&state`?** Perché
`state` è del processo, ma il buffer da modificare è quello dell'entry
specifica su cui questo worker sta lavorando *ora*. Se passassimo
`&state` all'utente, dovremmo dirgli anche "e l'indice corrente è i",
complicando l'interfaccia. Invece pre-calcoliamo i tre puntatori
giusti (al buffer di `entries[i]`) in `ec`, e l'utente vede solo
"qualcosa per emettere", senza sapere di indici o array.

Riepilogo:
- **`state`** = stato del **processo** (vive per tutto
  `reducer_process_main`)
- **`ec`** = stato dell'**emit**, specifico per una singola chiamata
  al reducer dell'utente; punta dentro `state` al buffer giusto

---

### 5.5 Simmetria col mapper: `mapper_state`

Esattamente la stessa storia vale nel mapper. La struct si chiama
`mapper_state`:

```c
struct mapper_state {
    thread_queue_t *queue;
    mr_mapper_t     mapper_fn;
    void           *user_arg;
    int             reducer_fd;     // fd verso il reducer (= STDOUT_FILENO)
    mtx_t           out_mutex;      // mutex per scritture concorrenti
    atomic_size_t   pairs_count;    // contatore per il log
};
```

Allocata sullo stack di `mapper_process_main`, condivisa fra reader e
worker via puntatore.

**Differenza importante**: nel mapper, **`emit_arg` passato all'utente
è direttamente `&state`** (non c'è bisogno di una struct ausiliaria
come `reducer_emit_ctx`). Perché? Perché tutti i worker mapper scrivono
sulla stessa pipe attraverso lo stesso mutex: niente è specifico per
la riga corrente. Lo stato del processo basta.

```c
int rc = st->mapper_fn(&line,
                       mapper_emit,
                       st,              // ← emit_arg = puntatore a state
                       st->user_arg);
```

Dentro `mapper_emit`:
```c
static int mapper_emit(const char *token, const void *value, size_t value_size,
                       void *emit_arg) {
    struct mapper_state *st = (struct mapper_state *)emit_arg;
    mtx_lock(&st->out_mutex);
    int rc = protocol_write_pair(st->reducer_fd, /* ... */);
    mtx_unlock(&st->out_mutex);
}
```

**Risposta-30-secondi all'orale**:
> "`reducer_state` (e analogamente `mapper_state`) è la struct
> allocata sullo stack del main thread del processo, che contiene
> tutto lo stato condiviso fra i thread di quel processo: callback,
> file descriptor, mutex, contatori, array delle entry. I thread
> ricevono il suo indirizzo come argomento `void*` di `thrd_create`.
> È il pattern classico in C per condividere dati fra thread: i
> thread hanno firma con un solo argomento, quindi ci mettiamo
> tutto in una struct. Vive sullo stack del main thread del
> processo, garantendo che esista per tutta la durata dei thread
> che la usano grazie al `thrd_join` prima del return."

---

## Spazio per nuove domande

Aggiungere qui sotto man mano che emergono altri dubbi durante il
ripasso.

###  

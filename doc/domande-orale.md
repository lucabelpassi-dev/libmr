# Domande e risposte — preparazione orale

Documento di ripasso per il progetto **libmr** (Laboratorio II A).
Raccoglie domande emerse rileggendo il codice, con risposte pronte da
ripercorrere prima dell'orale.


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

### 3.3 Il `64` di `mr_attr_set_queue_size`

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

## Spazio per nuove domande

Aggiungere qui sotto man mano che emergono altri dubbi durante il
ripasso.

###  

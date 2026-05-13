---
title: "libmr — un framework MapReduce a singolo nodo in C"
subtitle: "Relazione del progetto, corso Laboratorio II A — A.A. 2025/2026"
date: "Maggio 2026"
documentclass: article
fontsize: 11pt
geometry: margin=2.5cm
toc: true
toc-depth: 2
numbersections: true
---

\newpage

# Introduzione

Il progetto realizza `libmr`, una libreria statica in C che implementa il
paradigma di calcolo *MapReduce* confinato a un singolo nodo, secondo la
specifica del corso. La libreria espone al programma cliente un'interfaccia
minimale — la struttura `mr_attr_t` per la configurazione, il tipo opaco
`mr_t` per la sessione, e quattro funzioni: `mr_attr_init`, `mr_create`,
`mr_start`, `mr_destroy` — e nasconde dietro questa interfaccia
un'architettura interna a tre processi cooperanti, comunicanti attraverso
*pipe* POSIX, ciascuno multithreaded mediante i *thread* del C11.

La filosofia di design adottata è di **scatola nera trasparente**: il
chiamante fornisce due funzioni di callback (la *map function* e la *reduce
function*) e i percorsi del file di input e di quello di output; il framework
si occupa di tutto il resto — `fork`, creazione delle pipe, distribuzione del
lavoro fra i thread, raggruppamento dei valori per chiave, serializzazione
del file di output, logging. Il chiamante riceve il controllo soltanto a
esecuzione conclusa. Non vede mai i processi figli, non maneggia mai i
descrittori delle pipe, non sceglie un thread sul quale girare: scrive
funzioni pure di trasformazione dati e lascia che il framework le orchestri.

Questo schema di responsabilità è quanto la specifica richiede al §1 e al §3,
e ha un'implicazione importante sulla forma delle *callback*: queste vedono
solo dati, non flussi, non descrittori, non strutture di sincronizzazione. Il
loro contratto è specificato in modo preciso nell'header pubblico
(`include/mr.h`), inclusa in particolare la regola **`emit` deve copiare il
contenuto del messaggio prima di restituire il controllo al chiamante**: una
clausola apparentemente sottile ma decisiva, sulla quale è costruito tutto il
modello di proprietà dei buffer.

Nelle pagine che seguono descriviamo le scelte di progettazione adottate per
ognuno dei livelli del framework, partendo dall'architettura generale (§2) e
scendendo poi nei processi e nei loro descrittori (§3, §4), nei thread e
nelle loro sincronizzazioni (§5, §6), nei formati di scambio fra processi e
verso il file di output (§7, §8), nel sistema di logging (§9) e nel
collaudo (§10). La sezione conclusiva ricapitola le decisioni più rilevanti
ai fini della discussione orale.

\newpage

# Architettura generale

`libmr` esegue una computazione MapReduce facendo cooperare tre processi
distinti, ciascuno con un ruolo ben preciso:

- il **processo principale** (*main*) legge il file di input riga per riga e
  spedisce le righe al processo mapper; al termine raccoglie i record
  prodotti dal processo reducer e li scrive sul file di output;
- il **processo mapper** riceve righe dal main, le distribuisce ai propri
  *worker thread* che invocano la *map function* fornita dall'utente; le
  coppie chiave–valore generate vengono spedite al processo reducer;
- il **processo reducer** riceve le coppie dal mapper, le raggruppa per
  chiave, e per ciascun gruppo invoca la *reduce function* fornita
  dall'utente; i risultati vengono spediti al processo main.

I tre processi sono connessi da tre pipe unidirezionali:

```
   main             mapper             reducer
    |                |                   |
    | --- righe ---> | --- coppie -----> | --- risultati ---> main (file output)
    |                |                   |
```

Questa disposizione è quella prescritta dalla specifica al §5 e ha tre
proprietà strutturali importanti.

**Isolamento di errore.** Un crash del mapper non corrompe lo stato del main
o del reducer: la rispettiva pipe in lettura segnala EOF e il processo che la
sta leggendo termina ordinatamente. La specifica al §5.1 chiede esattamente
questo comportamento.

**Segnalazione di terminazione attraverso EOF.** La fine dell'input non viene
notificata con un messaggio speciale di "fine flusso", ma con la chiusura
*ordinata* del lato di scrittura della pipe corrispondente. Quando il processo
main ha finito di leggere l'input, chiude il lato di scrittura della pipe
`main_to_mapper`; il reader del mapper riceve `read(...) == 0` e propaga il
segnale verso i suoi worker, che a loro volta terminano; il mapper chiude il
lato di scrittura della pipe `mapper_to_reducer`; e così via fino al main, che
rileva la chiusura dell'ultima pipe e fa `waitpid` sui figli.

**Determinismo del file di output.** Pur essendo il calcolo *parallelo* a più
livelli (più worker nel mapper, più worker nel reducer), il file di output è
**deterministico**: esecuzioni successive sullo stesso input producono
file byte-per-byte identici, come richiesto dalla specifica §8. Le scelte
architetturali che garantiscono questa proprietà sono descritte al §8.

Una scelta operativa critica, che presentiamo qui perché impatta
l'architettura, è l'**ordine di creazione**: i tre processi nascono via
`fork()` **prima** che vengano avviati i thread. Detto altrimenti, ogni
processo è *single-threaded* nel momento in cui viene biforcato. Il motivo è
che `fork()` clona soltanto il thread che lo chiama, mentre eventuali altri
thread vengono persi nel figlio; mutex eventualmente posseduti da quei thread
restano "occupati" nel figlio senza nessuno che possa liberarli. Forkare a
single-thread elimina alla radice il rischio di stato residuo da liberare.

# Interfaccia pubblica e contratti

L'header `include/mr.h` espone esattamente quanto la specifica del progetto
richiede (Appendice A): una *struct* `mr_attr_t` per la configurazione, un
tipo opaco `mr_t` per la sessione, e quattro funzioni. La separazione fra i
due tipi non è casuale.

**`mr_attr_t` è esposta.** Contiene 12 campi pubblicamente accessibili: i
numeri di thread, le dimensioni delle code, i percorsi di input e output, e
così via. È esposta perché il chiamante deve poterla allocare nello stack,
inizializzarla, modificarne i campi con i setter, e infine distruggerla.
Esporre la struttura permette l'uso "C-idiomatico" di una configurazione
fatta inizializzando la struct sullo stack.

**`mr_t` è opaca.** Il tipo è un *pointer alias* a una struttura definita
solo in `mr.c`. Il chiamante non sa, e non deve sapere, cosa c'è dentro: i
*pid* dei figli, i *file descriptor* delle pipe, i puntatori alle callback,
sono dettagli implementativi della libreria. Esporre questi campi
significherebbe vincolarci a non poterli mai più cambiare. Tenerli opachi
permette di riorganizzarli in futuro senza rompere il chiamante.

Le **callback** `mr_mapper_func_t` e `mr_reducer_func_t` ricevono dati e
una funzione di *emissione* (`emit`) come parametro; non hanno accesso ai
descrittori delle pipe, non vedono i thread su cui girano, non maneggiano
sincronizzazioni. È un'astrazione di *funzione pura* (modulo gli effetti
collaterali del log o di stato esterno scelto dall'utente attraverso il
puntatore `user_arg`).

**Il contratto di `emit`** è la parte di interfaccia più sottile. La
specifica (§A.1 di Appendice A) impone che `emit` **copi il contenuto del
buffer ricevuto prima di restituire il controllo al chiamante**. Questo
significa che il chiamante può tranquillamente passare un buffer allocato
sullo stack, riusarlo per altre emissioni successive, o liberarlo subito
dopo; la sua memoria non viene "tenuta in vita" attraverso il framework. Nel
nostro caso questa copia è realizzata facendo sì che `emit` esegua una
`write` *sincrona* sulla pipe: il kernel copia i byte nel buffer della pipe
prima del ritorno, e questa è precisamente la "copia" richiesta dal
contratto. Nessun buffering applicativo intermedio.

**`mr_start` è bloccante.** Non esiste una `mr_join`: `mr_start` ritorna solo
quando i due processi figli sono stati raccolti via `waitpid` e il file di
output è chiuso. Questo modello sincrono semplifica enormemente la vita
dell'utente, che non deve preoccuparsi di stati intermedi.

# Gestione dei processi: fork, pipe, dup2, waitpid

Il processo `main` crea le tre pipe con `pipe(2)`, poi esegue due `fork(2)`
in successione. Per ciascun figlio, prima di restituire il controllo
all'utente, si compie la seguente *disciplina dei descrittori*, che è
**critica** e deve essere svolta meticolosamente.

Il padre, dopo i due fork, **chiude i lati delle pipe che non userà**:

```
nel main (padre):
    close(main_to_mapper[READ])      // legge solo il mapper
    close(mapper_to_reducer[READ])   // legge solo il reducer
    close(mapper_to_reducer[WRITE])  // scrive solo il mapper
    close(reducer_to_main[WRITE])    // scrive solo il reducer
```

Ciascun figlio fa il simmetrico: chiude i lati che non userà e collega
`stdin`/`stdout` ai lati che terrà, usando `dup2(2)`. Nel mapper, ad esempio:

```
nel figlio mapper:
    dup2(main_to_mapper[READ],     STDIN_FILENO)
    dup2(mapper_to_reducer[WRITE], STDOUT_FILENO)
    chiusura di tutti gli altri descrittori delle pipe
```

Questa disciplina è importante per tre motivi.

Primo, **affinché EOF si propaghi correttamente**. Quando il padre chiude il
lato di scrittura di `main_to_mapper`, il mapper riceve EOF *solo se* nessun
altro processo nel sistema sta tenendo aperto quel lato. Se il padre avesse
dimenticato di chiudere il suo riferimento al `READ` end, il mapper non lo
saprebbe (sarebbe innocuo), ma se avesse dimenticato di chiudere il suo
riferimento al `WRITE` end, il mapper *non vedrebbe mai EOF* e resterebbe in
attesa indefinita. Questo è un classico errore difficile da diagnosticare.

Secondo, **per liberare risorse**: i descrittori sono una risorsa di
processo, lasciarli aperti senza necessità è uno spreco.

Terzo, **per dare alle callback dell'utente un ambiente C-idiomatico**: il
mapper legge dallo `stdin` e scrive sullo `stdout`. Anche se le callback non
usano direttamente questi descrittori, il framework può scrivere il proprio
codice come se fosse un filtro Unix.

Al termine dell'esecuzione, `mr_start` chiama **`waitpid(2)` su entrambi i
figli** per evitare processi *zombie* e per propagare lo stato di
terminazione all'utente attraverso il log (sezione §9).

# Thread C11: lifecycle e sincronizzazione

Ognuno dei tre processi è single-threaded al momento del `fork`; i thread
vengono creati subito dopo, dentro le funzioni di entry-point dei processi
figli (`mapper_process_main`, `reducer_process_main`), usando le primitive
del C11 dichiarate in `<threads.h>`: `thrd_create`, `thrd_join`, `mtx_t`,
`cnd_t`, `_Thread_local`.

Il **processo mapper** crea due *gruppi* di thread:

- **un singolo *reader thread*** che legge dalla pipe verso il main,
  deserializza i messaggi nel protocollo descritto al §7, e accoda gli
  oggetti su una coda interna;
- **N *worker thread*** (default 4, configurabile via `mr_attr_t`) che
  estraggono righe dalla coda e invocano la *map function* dell'utente, che
  a sua volta chiama `emit` per ogni coppia chiave–valore prodotta.

Questa scelta — un *reader* unico e più worker — separa la responsabilità
di parsing del protocollo dalla responsabilità di calcolo. Avere più reader
sulla stessa pipe sarebbe possibile ma non utile: la `read(2)` è
sincronizzata dal kernel, e parallelizzare la deserializzazione di un flusso
sequenziale non porta velocità apprezzabile.

Il **processo reducer** adotta uno schema diverso: una **pipeline a tre fasi
sequenziali**, non un modello producer–consumer.

- **Fase 1**: un reader thread legge *tutte* le coppie dalla pipe verso il
  mapper, le inserisce in una *hash table* che raggruppa i valori per
  *token* (sezione §7), e termina al ricevere EOF.
- **Fase 2**: la hash table viene *finalizzata*: ogni *bucket* viene
  trasformato in un array, l'insieme dei token viene ordinato
  lessicograficamente. A questo punto vengono creati N *worker thread* che
  invocano la *reduce function* sui gruppi assegnatigli, usando un
  *contatore atomico* condiviso per il *work stealing* (ogni worker fa
  `atomic_fetch_add(&next, 1)` per ricevere un nuovo indice).
- **Fase 3**: dopo che tutti i worker hanno terminato, il main thread del
  reducer percorre l'array dei token in ordine lessicografico e scrive sulla
  pipe verso il main i risultati prodotti dai worker, già pre-serializzati
  in buffer per-entry.

La scelta di **tre fasi sequenziali** invece di una pipeline streaming è una
decisione di progetto motivata dalle richieste di determinismo (§8): se
volessimo iniziare a *ridurre* mentre il *grouping* è ancora in corso,
dovremmo affrontare la complicazione di decidere "questo gruppo è completo
oppure arriveranno ancora valori?". Tre fasi sequenziali rendono il problema
banale: la fase di reduce inizia solo quando la fase di grouping è
completamente terminata.

Tutte le primitive di sincronizzazione tra thread (mutex, variabili di
condizione, code) sono **per-processo**: vivono nello stesso spazio di
indirizzamento e non attraversano i confini di processo. Quando serve
serializzazione cross-process (per il log) si usa un semaforo POSIX named
(§9).

# Coda thread-safe e protocollo IPC

La struttura `mr_queue_t` (in `src/queue.c`) è una coda *bounded* di puntatori
opachi, thread-safe, con semantica di chiusura:

- un *producer* esegue `queue_push(q, item)`; se la coda è piena, attende su
  una variabile di condizione;
- un *consumer* esegue `queue_pop(q, &item)`; se la coda è vuota e non
  chiusa, attende; se vuota e chiusa, riceve un codice di terminazione;
- `queue_close(q)` segnala "non arriveranno più elementi", e fa
  `cnd_broadcast` per svegliare *tutti* i consumer in attesa, che vedranno
  la coda vuota+chiusa e termineranno.

Tutto è protetto da un singolo `mtx_t` + due `cnd_t` (uno per "non più
piena", uno per "non più vuota").

Il *protocollo* di scambio fra i processi (file `src/protocol.c`) è
**length-prefixed**: ogni messaggio è preceduto da un *header* a intero che
indica la lunghezza dei dati che seguono. Esistono tre tipi di messaggio:

1. una **riga** spedita dal main al mapper: `[int len][bytes len]`;
2. una **coppia chiave–valore** spedita dal mapper al reducer: `[int klen][bytes klen][int vlen][bytes vlen]`;
3. un **risultato** spedito dal reducer al main, stesso formato di una
   coppia.

La scelta del length-prefix risolve il problema fondamentale della
*framing*: come distinguere dove finisce un messaggio e dove ne inizia un
altro su un flusso di byte. Alternative come delimitatori di fine
(`\0`, `\n`, `\r\n`) hanno il difetto che il delimitatore può apparire
all'interno dei dati binari (un valore arbitrario può contenere uno zero).
Il length-prefix è binario, sicuro per ogni payload, autodescrittivo.

Tutte le `read` e le `write` sulle pipe sono incapsulate in due funzioni
`readn` e `writen` che gestiscono le **letture e scritture parziali**: il
kernel non garantisce di soddisfare in una singola syscall la richiesta di
trasferire N byte; può ritornare un numero inferiore. Le funzioni iterano
finché non hanno trasferito tutto, oppure ritornano errore. Validano inoltre
le lunghezze ricevute (non negative, sotto un tetto di 16 MB) per rifiutare
messaggi corrotti.

# Raggruppamento per token

Il raggruppamento dei valori per chiave nel reducer è realizzato in
`src/group_table.c` con una **hash table a *bucket***: 1024 *bucket*
implementati come liste linkate, indirizzati da una funzione hash FNV-1a sul
token. La struttura ha due fasi di vita:

- **fase di inserimento** (durante la lettura dalla pipe): ogni coppia
  `(token, valore)` viene cercata nei bucket; se il token è nuovo, viene
  creata una nuova entry; se esiste, il valore viene appeso a una lista
  linkata interna alla entry. Il framework copia sia il token che il valore
  in memoria propria (la callback `emit` aveva già copiato i byte nel buffer
  della pipe; qui copiamo nuovamente nella tabella).
- **fase di finalizzazione**: a EOF, la tabella viene "consolidata" — per
  ogni entry, la lista linkata di valori viene riversata in un array
  contiguo; l'insieme di tutti i token viene poi raccolto in un secondo
  array, ordinato lessicograficamente con `qsort`. È questo array ordinato
  che la *reduce phase* attraversa.

La scelta della *hash table* con 1024 bucket e *chaining* è un compromesso
ragionevole fra semplicità di implementazione e prestazioni nel caso medio.
Per un numero di token estremamente grande il fattore di carico crescerebbe
e le liste si allungherebbero, ma il dimensionamento è coerente con
l'ipotesi di lavoro su file di testo di taglia tipica del progetto. Il
**modello di proprietà** della memoria è chiaro: la tabella possiede tutte
le proprie chiavi e valori, e li libera nella sua funzione di distruzione.

# Formato di output e determinismo

Il file di output ha lo **stesso formato length-prefixed** dei messaggi sulla
pipe `reducer_to_main`. Il main, dopo l'avvio dei figli, esegue un *pass-through*
byte-per-byte dalla pipe del reducer al file di output: legge un *header* di
lunghezza, scrive l'header e il payload sul file, ripete fino a EOF. Il main
non interpreta nulla; trasporta soltanto.

Il **determinismo** del file di output è una proprietà obbligatoria
(specifica §8) e non automatica: il framework esegue calcoli su più thread,
e l'ordine di esecuzione dei worker è non deterministico. Tre meccanismi
combinati garantiscono comunque l'identità byte-per-byte fra esecuzioni
ripetute:

- **i token sono ordinati lessicograficamente** alla fine della fase di
  *grouping* (sezione §7);
- **i worker del reducer accumulano i propri risultati in *buffer per
  entry*** invece di scriverli direttamente sulla pipe; nessun worker
  esegue scritture sulla pipe;
- **il main thread del reducer scorre l'array ordinato dei token**, prende
  il buffer pre-serializzato di ciascuna entry e lo scrive sulla pipe in
  *ordine lessicografico stretto*. Anche se i worker hanno terminato in un
  ordine arbitrario, il flush finale è sequenziale e deterministico.

Una conseguenza piacevole è che il file di output è anche *testualmente*
diff-abile fra esecuzioni con un semplice `cmp` o `diff` — proprietà che il
test end-to-end sfrutta direttamente.

# Sistema di logging

A ogni esecuzione il framework produce un file `mr.log` nella directory di
lavoro, scritto da **tutti e tre i processi** e da **tutti i thread di
ciascun processo**. Per garantire che le righe non si intreccino fra di loro
mentre vengono scritte, l'accesso al file è protetto da un **semaforo POSIX
*named*** (`/mr_log_<pid_del_padre>`, dove `<pid_del_padre>` è il pid del
processo main al momento di `mr_start`).

La scelta del semaforo POSIX named è dettata dalla necessità di
sincronizzazione *cross-process*: i mutex C11 vivono nello spazio di
indirizzamento di un singolo processo e non sono utilizzabili attraverso una
`fork`. I semafori POSIX named, al contrario, sono identificati da un nome
nel filesystem (`/dev/shm` su Linux) e sono accessibili da qualsiasi processo
che conosca il nome. Il padre li crea con `sem_open(O_CREAT | O_EXCL)` e li
distrugge con `sem_unlink` al termine; i figli li ereditano via `fork` (il
nome è disponibile attraverso lo stato del padre clonato).

All'interno di ciascun processo, una variabile a livello di file (`log_st`)
mantiene lo stato del logging (descrittore aperto, semaforo, identificativo
del processo); una variabile `_Thread_local` mantiene il nome del thread
corrente, in modo che ogni thread possa loggare con la propria identità
senza dover passare il proprio nome ad ogni chiamata.

Il **formato del log** è ASCII a colonne fisse:

```
[YYYY-MM-DD HH:MM:SS.mmm] [proc   ] [thread    ] [EVENT             ] msg
```

con `proc` di 7 caratteri (per accomodare "reducer"), `thread` di 10
caratteri (per accomodare "worker-NN"), e `EVENT` di 18 caratteri. Questa
scelta rende il file facilmente *parseabile* con strumenti testuali standard
(`grep`, `awk`, `cut`), e il formato del timestamp ISO-8601 con zero-padding
ha la proprietà di essere lessicograficamente ordinabile: un semplice `sort
mr.log` produce le righe in ordine cronologico anche quando il `sem_wait` ha
riordinato l'arrivo sul file.

Il logging è *best-effort*: le funzioni di log non propagano mai un errore
al chiamante. Se il file non si apre o il semaforo non si crea, il log
viene disabilitato silenziosamente e il programma continua. Un fallimento
del log non è mai un buon motivo per fallire un MapReduce.

# Compilazione, uso, test

## Compilazione

Il `Makefile` di livello superiore è organizzato in modo *ricorsivo*: ha le
proprie regole per compilare i sorgenti della libreria in `src/` e produrre
`lib/libmr.a`, e poi delega ai *sub-Makefile* di `examples/` e `tests/`. I
target principali sono:

- `make` o `make all` — compila tutto;
- `make test` — esegue i test unitari e quelli end-to-end;
- `make clean` — rimuove tutti gli artefatti di build (`.o`, `.a`,
  eseguibili, file `mr.log`, PDF della relazione).

I flag di compilazione sono `-std=c11 -Wall -Wextra -Wpedantic -O2
-D_POSIX_C_SOURCE=200809L`. Il warning level rigoroso è una scelta di
qualità: il codice compila senza warning con queste opzioni.

## Test unitari

I test unitari (`tests/test_*.c`) sono programmi `main()` autonomi che
chiamano le API interne dei moduli e usano `assert` per le verifiche.
Coprono:

- **`test_queue`** — push/pop con producer e consumer concorrenti, chiusura
  con drain corretto, comportamento con coda piena;
- **`test_protocol`** — round-trip di serializzazione/deserializzazione dei
  tre tipi di messaggio, rifiuto di lunghezze invalide;
- **`test_attr`** — initialize/destroy ripetuti, setter di tutti i campi,
  comportamento con puntatori `NULL`;
- **`test_group_table`** — inserimento, lookup, finalizzazione, ordinamento,
  pulizia.

## Test end-to-end

Il test end-to-end (`tests/test_e2e.sh`) esegue il binario `wordcount`
linkato contro `libmr.a` su sei casi di input distinti (caso base, file
vuoto, riga finale senza newline, punteggiatura come separatore, directory
con più file, esecuzione ripetuta per il determinismo), decodifica il file
binario di output con `print_output`, e confronta byte a byte con un output
atteso scritto a mano. Ognuno dei sei casi è motivato da un requisito
puntualmente nominato dalla specifica (§4 per i casi limite di input, §8 per
il determinismo).

# Conclusioni

Il progetto realizza interamente le funzionalità richieste dalla specifica:
l'interfaccia pubblica corrisponde all'Appendice A, l'architettura a tre
processi è quella prescritta al §5, il sistema di logging usa il semaforo
POSIX named come richiesto al §11, l'output è deterministico come richiesto
al §8, e i test (unitari e end-to-end) coprono i casi limite enumerati nella
specifica.

I due aspetti progettuali più sottili — e quelli su cui si è dovuta
investire più attenzione — sono stati la **disciplina dei descrittori dopo
`fork`** (la propagazione corretta dell'EOF lungo la cascata di pipe
dipende dalla chiusura *meticolosa* dei lati non utilizzati) e la
**garanzia di output deterministico in presenza di thread paralleli nel
reducer** (risolta con il pattern *per-entry buffering + flush ordinato
single-threaded*). Sono questi i due punti che caratterizzano il progetto
come *concorrente reale*, e che si è scelto di descrivere in dettaglio
nelle sezioni §4 e §8.

Il framework, nella sua forma attuale, è completo e funzionante per il caso
d'uso canonico del `wordcount`. Le naturali estensioni — gestione di
*file di input molto grandi* (oggi tutto il *grouping* tiene memoria nella
hash table del reducer), un *combiner* opzionale eseguito dal mapper per
ridurre il traffico sulla pipe `mapper_to_reducer`, *shuffling* fra più
reducer — sono fuori dallo scopo del progetto del corso e non sono state
implementate.

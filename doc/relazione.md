# Framework libmr — Relazione di progetto
**Autore:** Luca Belpassi — **Corso:** Laboratorio II A — **Data:** 26/05/26

## 1. Introduzione
<!-- Cosa fa libmr in 1 paragrafo. Riferimento concettuale a MapReduce.
     Cosa NON fa: no distribuzione, no rete, no exec, no shared memory. -->

## 2. Architettura generale
<!-- Diagramma a tre processi (main → mapper → reducer → main).
     Pipeline locale, pipe come unico canale IPC del flusso dati.
     Concetto del "corriere cieco": valori opachi length-prefixed. -->
### Il framework come "corriere cieco" [Specifica §3, §10]
Il framework libmr adotta un modello di trasporto opaco dei dati: una volta che il programma utente fornisce un valore associato a un token, il framework non interpreta mai i byte trasportati. Questa scelta riflette il principio fondamentale di MapReduce, in cui la logica applicativa è interamente delegata alle funzioni mapper e reducer fornite dal programma utente. Concretamente, il framework non assume che i valori siano stringhe C terminate da \0, non li confronta, non ne calcola dimensioni con strlen, non li stampa. Ogni valore è una sequenza di byte di lunghezza nota, comunicata sempre insieme alla sua lunghezza. L'unica eccezione è il token di raggruppamento, che per contratto è una stringa C valida composta da soli caratteri alfanumerici ASCII e viene usato come chiave di partizionamento.

## 3. Interfaccia pubblica
<!-- Tour rapido di mr.h.
     Asimmetria mr_attr_t (struct value, esposta) vs mr_t (opaco):
       espone configurazione, nascondi macchinario.
     Regole di ownership di §A.1: emit deve copiare prima di ritornare,
       puntatori validi solo durante la callback.
     Convenzione 0/-1 + errno. -->
### mr_attr_t esposto vs mr_t opaco [Specifica §A]
L'interfaccia pubblica adotta un'asimmetria deliberata fra i due tipi principali. La struttura mr_attr_t è completamente esposta nell'header: è una semplice aggregazione di parametri di configurazione (numero di thread mapper e reducer, dimensione delle code, nome del file di log) che l'utente può dichiarare come variabile locale sullo stack. Al contrario, mr_t è opaco: l'header dichiara solo typedef struct mr *mr_t; mentre la definizione della struct mr vive esclusivamente in src/mr.c. La regola implicita è "espone la configurazione, nascondi il macchinario": la configurazione è composta da dati passivi che l'utente legittimamente legge e scrive, mentre la sessione contiene puntatori a callback, descrittori di file, PID dei processi figli — stato che deve restare invisibile e protetto da modifiche accidentali.

### emit deve copiare prima di ritornare [Specifica §A.1]
Il contratto specificato in §A.1 della specifica impone un vincolo preciso alla funzione emit: i dati passati dall'utente (token e valore) devono essere copiati dal framework prima che emit ritorni. Questo permette all'utente di passare a emit buffer temporanei (anche sullo stack) senza preoccuparsi della loro persistenza. Il framework adotta la strategia di scrittura sincrona: dentro emit la coppia viene serializzata e scritta direttamente sulla pipe verso il reducer tramite writen. Al ritorno della write il kernel ha già copiato i byte nel buffer interno della pipe, quindi la copia richiesta dal contratto è di fatto garantita dal sistema operativo. Il costo è che emit può bloccare se la pipe è piena, ma questo non viola la specifica e mantiene la complessità del framework bassa. L'alternativa era introdurre un buffer applicativo intermedio, scelta scartata per inutile complicazione.

### mr_start bloccante, niente mr_join [Specifica §A]
La specifica definisce mr_start() come chiamata bloccante: restituisce il controllo al chiamante solo quando l'intera elaborazione è terminata e il file di output è stato prodotto, oppure quando si verifica un errore. Non esiste una funzione separata mr_join(): il waitpid() sui processi figli mapper e reducer avviene internamente a mr_start(), dopo aver chiuso la pipe verso il mapper e raccolto tutti i risultati dalla pipe del reducer. Il programma utente ha quindi un modello mentale lineare e sincrono: alla riga successiva alla mr_start() può assumere che il file di output esista e sia completo (oppure leggere errno per scoprire cosa è andato storto).
Le funzioni mr_attr_set_* implementano la validazione degli input al perimetro dell'API: rifiutano valori non validi (zero per i conteggi di thread o per la dimensione delle code) ritornando -1 con errno = EINVAL. Questo principio — controllare gli argomenti il prima possibile, all'ingresso del framework — semplifica il codice interno, che può assumere invarianti (es. "il numero di thread mapper è sempre >= 1") senza ulteriori controlli. La funzione mr_attr_set_log_file adotta inoltre la convenzione di deep copy al confine: duplica internamente la stringa passata dall'utente con strdup, così l'utente può liberare o riusare il proprio buffer subito dopo la chiamata. Simmetricamente, mr_attr_destroy libera questa copia. La proprietà della stringa è dell'oggetto mr_attr_t, e il const dichiarato nel typedef serve a impedire modifiche dall'esterno, non a negarne la proprietà interna.

## 4. Organizzazione dei processi
<!-- fork() + pipe() + dup2() + waitpid().
     Schema delle tre pipe e dei loro lati read/write.
     Ordine critico: pipe → fork(mapper) → fork(reducer) → dup2 → close.
     Perché chiudere i fd "fantasma" nei processi sbagliati = blocco su EOF.
     EOF come segnale di fine input (no messaggi speciali). -->
###  EOF come segnale di fine input  [Specifica §5.1]
Il framework non utilizza alcun messaggio speciale per segnalare la fine dei dati attraverso la pipeline. La terminazione di una fase viene comunicata sempre tramite la chiusura del lato di scrittura della pipe corrispondente, che provoca la ricezione di EOF (lettura che ritorna 0 byte) sul lato di lettura. La sequenza è la seguente: il processo principale, dopo aver inviato tutte le righe, chiude la sua estremità di scrittura verso il mapper; il thread lettore del mapper riceve EOF e chiude la coda interna; i worker mapper svuotano la coda e terminano; solo dopo la terminazione di tutti i worker il coordinatore del mapper chiude la pipe verso il reducer; il reducer riceve EOF e procede al raggruppamento finale; infine il reducer chiude la pipe verso il processo principale. La correttezza di questo schema dipende crucialmente dal fatto che ogni descrittore di scrittura della pipe mapper→reducer sia chiuso in tutti i processi che non lo usano: anche un solo lato di scrittura lasciato aperto impedirebbe al reducer di vedere EOF e bloccherebbe la pipeline indefinitamente.

### Disciplina dei descrittori dopo fork()
La correttezza della pipeline dipende in modo critico dalla disciplina con cui ogni processo chiude i descrittori che non utilizza. Prima della prima fork() esistono sei descrittori (due per ciascuna delle tre pipe), tutti aperti nel processo principale. Dopo le due fork(), ciascuno dei tre processi ne possiede una copia, per un totale di diciotto riferimenti. La regola operativa è: in ogni processo, deve restare aperto solo il sottoinsieme di descrittori effettivamente usato; tutti gli altri devono essere chiusi immediatamente.

- Processo principale: mantiene main_to_mapper[1] (scrittura verso il mapper) e reducer_to_main[0] (lettura dai risultati del reducer). Chiude tutto il resto, inclusi entrambi i lati della pipe mapper_to_reducer.
- Processo mapper: dopo aver duplicato i propri estremi su STDIN_FILENO e STDOUT_FILENO tramite dup2(), chiude tutti i sei descrittori originali delle pipe più il fd del file di output ereditato dal padre.
- Processo reducer: analogo al mapper, con i due lati appropriati duplicati su stdin e stdout.

Il caso più insidioso è la pipe mapper_to_reducer nel processo principale: il padre non la usa mai, ma la pipe è stata creata da lui e i suoi due descrittori sono inizialmente aperti nel suo address space. Se anche uno solo dei due venisse lasciato aperto nel padre, il reducer non riceverebbe EOF al termine del lavoro del mapper, perché la pipe avrebbe ancora un riferimento di scrittura "fantasma" nel padre. Il processo reducer resterebbe bloccato in read() indefinitamente. Questa è la forma più comune di deadlock nelle pipeline a tre processi, e nel nostro codice è prevenuta dalla chiusura esplicita di mapper_to_reducer[0] e mapper_to_reducer[1] subito dopo le fork(), accompagnata da un commento di avvertimento.

## 5. Thread C11 nel mapper e nel reducer
<!-- Mapper: 1 reader thread + N worker thread.
     Reducer: 1 reader thread + N worker thread + fase di grouping.
     Perché fork() PRIMA dei thread (stato dei mutex non riproducibile).
     Uso di mtx_t, cnd_t, thrd_create, thrd_join. -->
### Ordine fork() → thread (mai il contrario) [Specifica §5]
Un vincolo critico della specifica è che fork() non deve mai essere chiamato da un processo che abbia già creato thread C11 attivi. Il motivo è che fork() duplica solo il thread chiamante: nei processi figli gli altri thread "scompaiono", lasciando lo stato delle primitive di sincronizzazione (mutex, condition variable) in una configurazione inconsistente. Un mutex bloccato da un thread ora invisibile resterebbe bloccato per sempre nel figlio; una condition variable potrebbe avere worker in attesa di un signal che non arriverà mai. Per questo il framework realizza l'intera struttura dei processi prima di creare qualunque thread: il processo principale crea le pipe, esegue le due fork(), e solo a quel punto ciascun processo (principale, mapper, reducer) crea i propri thread C11 nello spazio di indirizzamento separato che gli compete.

### La funzione emit e il suo emit_arg "magico" [Specifica §A]
La firma di mr_mapper_t definita dalla specifica include un parametro void *emit_arg che il framework passa alla callback utente insieme al puntatore alla funzione emit. Il parametro è opaco per l'utente: non deve interpretarlo né modificarlo, lo passa pari pari quando richiama emit. È invece il framework che, dentro l'implementazione di emit, sa che emit_arg è un puntatore alla propria struct mapper_state, e lo cast'a per accedere al contesto di esecuzione (file descriptor della pipe verso il reducer, mutex di scrittura, eventuale stato per il logging). Il pattern è una forma classica di iniezione di contesto in una callback in C, equivalente concettualmente alla this-pointer dei linguaggi orientati agli oggetti: la callback non sa nulla del framework, ma porta con sé un riferimento opaco al proprio "ambiente" che, ritornato indietro al framework, gli permette di riprendere il controllo coerente del proprio stato.

### Work stealing dinamico con contatore atomico
La distribuzione del lavoro fra i worker non è statica (es. blocchi contigui di entry per worker) ma dinamica: tutte le entry sono in un unico array condiviso, e ciascun worker, quando è libero, esegue atomic_fetch_add(&next_idx, 1) per prenotare la prossima entry disponibile. Questo schema, noto come work stealing dinamico, garantisce un bilanciamento del carico anche quando il costo di elaborazione delle entry è fortemente eterogeneo: se un'entry richiede molto più lavoro di un'altra (es. valori per quel token particolarmente numerosi), i worker liberi continuano a prelevare entry rapide senza attendere il worker "lento". Il costo è una singola operazione atomica per prelievo — molto inferiore a quello di un mutex — e nessuna logica di partizionamento a priori.

## 6. Code interne e sincronizzazione
<!-- Pattern producer-consumer con mtx + cnd.
     is_closed + cnd_broadcast: come si sveglia tutti i worker dormienti.
     Ownership dei buffer attraverso la coda (chi alloca, chi libera).
     Fail-safe nel destroy se la coda non è vuota. -->
### is_closed + cnd_broadcast [Specifica §6, §6.1]
Le code interne (src/queue.c) implementano un classico schema produttore-consumatore sincronizzato con mtx_t per la mutua esclusione e cnd_t per le condizioni di risveglio. Per gestire la terminazione naturale, ogni coda espone un flag is_closed e una funzione queue_close(). Quando il produttore (es. il thread lettore della pipe) ha finito, chiama queue_close() che imposta il flag a true e invoca cnd_broadcast per svegliare tutti i worker eventualmente in attesa. I worker, al risveglio, verificano nel ciclo while (head == NULL && !is_closed) cnd_wait(...); se trovano la coda vuota e chiusa, ritornano "no more work" al chiamante e terminano. L'uso di cnd_broadcast invece di cnd_signal in questo punto è essenziale: con signal solo un worker verrebbe svegliato, gli altri resterebbero dormienti in eterno.

### Ownership dei buffer attraverso la coda [Specifica §6]
Per i buffer trasportati dalla coda vale la regola "chi produce alloca, chi consuma libera": il produttore alloca dinamicamente il buffer del dato, lo "regala" alla coda tramite queue_push, e il consumatore lo riceve da queue_pop diventando responsabile della sua deallocazione. La coda stessa, in condizioni normali, è solo un trasportatore e non possiede mai i buffer in modo duraturo. Tuttavia, in caso di terminazione anomala, la coda potrebbe essere distrutta non vuota; per evitare leak silenziosi, queue_destroy() scansiona la lista residua e libera sia i nodi sia i buffer in essi contenuti. Questa è una rete di sicurezza difensiva: in flusso normale non viene mai esercitata.

## 7. Formato dei messaggi sulle pipe [Specifica §6]
<!-- Tre tipi di messaggio:
       main→mapper: header file_line + bytes (file_name, line)
       mapper→reducer: header pair + bytes (token, value)
       reducer→main: header result + bytes (token, result)
     readn/writen per letture/scritture parziali.
     Validazione delle lunghezze (no negativi, no oltre i limiti). -->
### Length-prefix obbligatorio [Specifica §3, §7, §10]
Il protocollo binario sulle pipe è length-prefixed: ogni messaggio è preceduto da un header a campi di lunghezza fissa che dichiara le dimensioni dei payload variabili successivi. Questa scelta è imposta dalla natura opaca dei valori intermedi: come stabilito in §3 della specifica, un processed_token può contenere qualunque sequenza di byte, inclusi byte nulli. Un protocollo basato su delimitatori testuali sarebbe ambiguo: un byte zero nel payload verrebbe scambiato per un separatore. Il length-prefix elimina alla radice ogni ambiguità: si legge l'header, da esso si ricava esattamente quanti byte costituiscono il payload, e si leggono tutti e soli quei byte. Poiché le pipe possono effettuare letture e scritture parziali, sia le ricezioni sia le trasmissioni passano per le funzioni ausiliarie readn e writen, che iterano finché tutti i byte richiesti non sono stati trasferiti (gestendo correttamente EINTR). Le lunghezze contenute negli header sono valori int validati prima della conversione a size_t: non sono accettati valori negativi né valori superiori a un limite documentato, a tutela di messaggi malformati.

### Sincronizzazione delle scritture concorrenti sulla pipe [Specifica §6.1]
Più thread worker possono produrre coppie contemporaneamente, ma la pipe verso il reducer è una risorsa condivisa singola. Senza sincronizzazione, due thread che scrivono insieme rischiano di interlacciare i propri byte sulla pipe: l'intestazione di un messaggio potrebbe essere seguita dal payload di un altro, corrompendo il protocollo. POSIX garantisce l'atomicità di una singola write() solo per dimensioni inferiori a PIPE_BUF (4096 byte su Linux); ma poiché la funzione emit esegue tre write() separate (intestazione, token, valore), l'atomicità del kernel non basta. Il framework usa quindi un mtx_t C11 (out_mutex in struct mapper_state) che ogni worker acquisisce prima di scrivere e rilascia subito dopo, rendendo l'intera operazione di emit una sezione critica indivisibile rispetto agli altri worker.

## 8. Raggruppamento per token nel reducer
<!-- Struttura dati: [hash table? sorted array? — DA DECIDERE]
     Costo asintotico per insert e iter.
     Come si costruisce il vettore di mr_value_t per il reducer.
     Ordine lessicografico finale per determinismo dell'output. -->
### Struttura dati di raggruppamento: scelta e tradeoff [Specifica §6.2, §8]
La struttura dati che raggruppa i valori per token nel processo reducer è una tabella di hash con catene lineari (linked-list chaining). L'inserimento richiede tempo costante ammortizzato O(1) — calcolo dell'hash + ricerca lineare nella catena della casella, che in pratica è cortissima se il numero di caselle è ben dimensionato rispetto al numero di token distinti. Un'alternativa considerata era una struttura ordinata (es. albero AVL o array ordinato), che avrebbe avuto inserimenti in O(log N) e iterazione naturalmente ordinata; è stata scartata perché l'inserimento è l'operazione dominante (chiamata una volta per ogni coppia in arrivo, potenzialmente milioni) mentre l'ordinamento dei token è richiesto solo una volta a fine input. La tabella di hash + ordinamento finale O(N log N) ha quindi un costo totale equivalente nelle costanti più favorevoli del caso comune.
Il modulo implementa due fasi distinte, con un passaggio esplicito di "chiusura" fra le due. Durante l'inserimento i valori associati a un token vengono accumulati in una lista concatenata, perché aggiungerne uno in coda costa O(1) e non richiede di pre-conoscere quanti valori arriveranno; al momento del finalize la lista viene "appiattita" in un array contiguo mr_value_t[] — esattamente la forma richiesta dalla firma di mr_reducer_t. Il finalize esegue inoltre un singolo qsort di tutti i puntatori alle entry, ordinandoli lessicograficamente per token: questa è la sorgente del determinismo dell'output, requisito esplicito della specifica §8.
Il numero di caselle della tabella è fissato alla creazione (non c'è auto-resize). Per i carichi tipici del progetto questo è sufficiente; un'estensione futura potrebbe introdurre un raddoppio automatico quando il fattore di carico supera una soglia.

###  Pipeline a fasi sequenziali nel reducer [Specifica §6.2]
Il processo reducer adotta un'architettura a fasi sequenziali anziché un puro modello produttore-consumatore. La ragione è strutturale: per invocare la mr_reducer_t su un dato token, il framework deve avere raccolto tutti i valori associati a quel token, ma un valore può apparire in qualsiasi punto del flusso di coppie in arrivo dal mapper. Non è quindi possibile iniziare l'elaborazione di un token finché l'input non è interamente consumato. Il reducer implementa tre fasi non sovrapposte:

1. Fase di lettura: un singolo thread legge le coppie da stdin e le inserisce nella group_table. A EOF, esegue la finalize che appiattisce le liste di valori in array contigui e ordina le entry lessicograficamente per token.
2. Fase di reducing parallelo: N thread worker prelevano entry dall'array sortato tramite un contatore atomico (work stealing) e invocano la callback utente, accumulando i risultati serializzati in buffer per-entry.
3. Fase di flush: il main thread del processo scorre i buffer in ordine lessicografico e li scrive sulla pipe verso il processo principale.

La sincronizzazione fra fasi è ottenuta semplicemente tramite thrd_join: prima di lanciare i worker, il main attende il termine del reader; prima di flushare, attende il termine di tutti i worker. Non servono né condition variable né flag espliciti — il join è già un punto di sincronizzazione "happens-before" della memoria.

## 9. Formato del file di output 
<!-- Record length-prefixed: token_len, token, result_len, result.
     Ordinamento lessicografico per token = determinismo.
     Programma ausiliario print_output per leggere il formato in chiaro. -->
### Output deterministico con worker paralleli [Specifica §8]
La specifica richiede che il file di output sia deterministico: a parità di input e configurazione, esecuzioni diverse devono produrre file identici. Tuttavia, i worker reducer lavorano in parallelo e terminano in ordine imprevedibile. Per riconciliare parallelismo e determinismo, il framework adotta uno schema a buffer per-entry più flush ordinato: ogni worker, anziché scrivere direttamente sulla pipe verso il processo principale, serializza i propri risultati (intestazione + token + result, formato length-prefixed identico a quello del file di output) in un buffer in memoria indicizzato dalla posizione dell'entry nell'array sortato. Quando tutti i worker hanno terminato, il main thread del processo reducer scorre i buffer in ordine di indice — cioè in ordine lessicografico per token — e li scrive sulla pipe in un colpo unico. L'ordine sulla pipe (e quindi nel file di output) è quindi fisso e indipendente dall'ordine di terminazione dei worker. Se il reducer utente emette più risultati per uno stesso token, l'ordine relativo fra essi è preservato perché il buffer è append-only: i risultati appaiono nel file nello stesso ordine in cui sono stati prodotti dalle chiamate a emit dentro la callback.

## 10. Formato del file di log
<!-- Schema riga: [timestamp] [processo] [thread] [evento] messaggio.
     Sincronizzazione interprocesso via semaforo POSIX named.
     Lista degli eventi loggati (creazione pipe, fork, EOF, conteggi...). -->
### Semaforo POSIX per il log [Specifica §11]
Il file di log è scritto da tutti e tre i processi (principale, mapper, reducer), quindi richiede una primitiva di sincronizzazione inter-processo, e non solo inter-thread. La scelta adottata è un semaforo POSIX named, creato dal processo principale tramite sem_open() prima delle fork() e quindi ereditato dai processi figli. Ogni evento da loggare segue lo schema: sem_wait() per acquisire il lock, formattazione della riga in un buffer locale, writen() sul descrittore del file di log aperto con O_APPEND, sem_post() per rilasciare. La specifica al §11 indica esplicitamente i semafori POSIX come una soluzione ammissibile. Alternative considerate erano i lock su file (flock()/fcntl()) e un processo logger dedicato: la prima richiede attenzione alla semantica di O_APPEND sotto fork, la seconda introduce un quarto processo e un canale di comunicazione aggiuntivo, entrambe non giustificate per gli obiettivi del progetto.

## 11. Test realizzati
<!-- Unit test: queue (push/pop/close), protocol (readn/writen).
     End-to-end: wordcount su input noti.
     Edge case: file vuoti, righe vuote, no \n finale, file con 1 riga. -->

## 12. Conclusioni
<!-- Cosa funziona. Limiti noti. Possibili estensioni (più mapper/reducer process). -->

## Scratchpad (rimuovere prima della consegna)
<!-- da rimuovere -->
### Scelte di design da tenere a mente per l'orale
API stile iteratore (open / next / close) e non callback-based: il chiamante (mr.c) ha già il suo loop principale e vuole controllare lui quando chiedere la prossima riga, soprattutto perché tra una next e l'altra deve serializzare e scrivere sulla pipe (operazione bloccante).
Puntatori "borrowed": file_input_next riempie un mr_file_line_t con puntatori validi solo fino alla chiamata successiva. Il chiamante li usa subito (li scrive sulla pipe) e li dimentica. Vantaggio: zero malloc/free per riga letta — il buffer interno viene riusato.
getline() per la lettura: è POSIX 2008 (abbiamo già _POSIX_C_SOURCE=200809L), gestisce tutti i casi limite della specifica gratuitamente — ultima riga senza \\n, righe vuote, righe arbitrariamente lunghe (rialloca il buffer da sola). Strappiamo solo il \\n finale se presente, come richiesto dalla specifica ("Il carattere di fine riga non fa parte del contenuto logico della riga passato al mapper").
scandir() con alphasort per le directory: combina enumerazione, filtro (saltiamo ., .. e non-file regolari) e ordinamento lessicografico in una sola chiamata. Determinismo garantito dalla libreria.
Non è thread-safe: usa stato mutabile interno. Va benissimo perché ha un solo utilizzatore (il main thread del processo principale).
### Per file_input
Casi limite gestiti (da menzionare nella relazione)
Direttamente coperti dalla specifica §4:

File vuoto: prima getline() ritorna -1 con feof, passiamo al file successivo. Se è l'unico file → file_input_next ritorna 0 senza mai produrre una riga.
Riga vuota: getline() ritorna n = 1 (solo \\n), strip → line_len = 0. La specifica richiede che la riga vuota venga passata al mapper.
File con una sola riga, senza \\n finale: getline() ritorna n = strlen(riga), nessuno strip, viene passata; chiamata successiva → EOF → file successivo o fine input.
File con \\n finale: nessuna "riga fantasma" dopo il \\n perché getline() ritorna -1 alla chiamata successiva.
Directory vuota o con soli non-file-regolari: paths_count resta 0, file_input_next ritorna 0 alla prima chiamata.
### mr.c: prima passata
Concettualmente la prima passata di mr.c risponde a tre domande operative:

1 - Come si trasforma la struttura di processi prevista in chiamate di sistema concrete? → pipe() x 3, fork() x 2, dup2() per riconnettere stdin/stdout, close() chirurgica di tutto il resto.
2 - Come si garantisce la propagazione corretta dell'EOF? → chiudendo aggressivamente ogni descrittore non usato in ogni processo. Lasciare aperto un solo lato di una pipe in un processo sbagliato vuol dire deadlock.
3 - Come si raccoglie l'esito? → waitpid() esplicito sui due PID dei figli, controllo di WIFEXITED/WEXITSTATUS.
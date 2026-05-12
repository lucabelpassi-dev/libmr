# Framework libmr — Relazione di progetto
**Autore:** Luca Belpassi — **Corso:** Laboratorio II A — **Data:** 26/05/26

## 1. Introduzione
<!-- Cosa fa libmr in 1 paragrafo. Riferimento concettuale a MapReduce.
     Cosa NON fa: no distribuzione, no rete, no exec, no shared memory. -->

## 2. Architettura generale
<!-- Diagramma a tre processi (main → mapper → reducer → main).
     Pipeline locale, pipe come unico canale IPC del flusso dati.
     Concetto del "corriere cieco": valori opachi length-prefixed. -->

## 3. Interfaccia pubblica
<!-- Tour rapido di mr.h.
     Asimmetria mr_attr_t (struct value, esposta) vs mr_t (opaco):
       espone configurazione, nascondi macchinario.
     Regole di ownership di §A.1: emit deve copiare prima di ritornare,
       puntatori validi solo durante la callback.
     Convenzione 0/-1 + errno. -->

## 4. Organizzazione dei processi
<!-- fork() + pipe() + dup2() + waitpid().
     Schema delle tre pipe e dei loro lati read/write.
     Ordine critico: pipe → fork(mapper) → fork(reducer) → dup2 → close.
     Perché chiudere i fd "fantasma" nei processi sbagliati = blocco su EOF.
     EOF come segnale di fine input (no messaggi speciali). -->

## 5. Thread C11 nel mapper e nel reducer
<!-- Mapper: 1 reader thread + N worker thread.
     Reducer: 1 reader thread + N worker thread + fase di grouping.
     Perché fork() PRIMA dei thread (stato dei mutex non riproducibile).
     Uso di mtx_t, cnd_t, thrd_create, thrd_join. -->

## 6. Code interne e sincronizzazione
<!-- Pattern producer-consumer con mtx + cnd.
     is_closed + cnd_broadcast: come si sveglia tutti i worker dormienti.
     Ownership dei buffer attraverso la coda (chi alloca, chi libera).
     Fail-safe nel destroy se la coda non è vuota. -->

## 7. Formato dei messaggi sulle pipe
<!-- Tre tipi di messaggio:
       main→mapper: header file_line + bytes (file_name, line)
       mapper→reducer: header pair + bytes (token, value)
       reducer→main: header result + bytes (token, result)
     readn/writen per letture/scritture parziali.
     Validazione delle lunghezze (no negativi, no oltre i limiti). -->

## 8. Raggruppamento per token nel reducer
<!-- Struttura dati: [hash table? sorted array? — DA DECIDERE]
     Costo asintotico per insert e iter.
     Come si costruisce il vettore di mr_value_t per il reducer.
     Ordine lessicografico finale per determinismo dell'output. -->

## 9. Formato del file di output
<!-- Record length-prefixed: token_len, token, result_len, result.
     Ordinamento lessicografico per token = determinismo.
     Programma ausiliario print_output per leggere il formato in chiaro. -->

## 10. Formato del file di log
<!-- Schema riga: [timestamp] [processo] [thread] [evento] messaggio.
     Sincronizzazione interprocesso via semaforo POSIX named.
     Lista degli eventi loggati (creazione pipe, fork, EOF, conteggi...). -->

## 11. Test realizzati
<!-- Unit test: queue (push/pop/close), protocol (readn/writen).
     End-to-end: wordcount su input noti.
     Edge case: file vuoti, righe vuote, no \n finale, file con 1 riga. -->

## 12. Conclusioni
<!-- Cosa funziona. Limiti noti. Possibili estensioni (più mapper/reducer process). -->

## Scratchpad (rimuovere prima della consegna)
<!-- "Corriere cieco" / valori opachi → §2 e §3
mr_attr_t esposto vs mr_t opaco → §3
emit deve copiare prima di ritornare (Strategia A: scrittura sincrona) → §3 e §4
mr_start bloccante, no mr_join → §3
Pipe come segnale di EOF, non messaggi speciali → §4
Ordine fork → thread (mai il contrario) → §5
is_closed + broadcast → §6
Ownership dei buffer attraverso la coda → §6
Length-prefix obbligatorio perché i valori contengono byte arbitrari → §7
Semaforo POSIX per il log → §10 -->
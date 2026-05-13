#ifndef MR_LOG_H
#define MR_LOG_H

/* Inizializza il logging nel processo CHIAMANTE.
 *
 *   log_path:  percorso del file di log; se NULL usa "mr.log".
 *   sem_name:  nome del semaforo POSIX named (formato "/qualcosa", deve
 *              essere univoco per esecuzione, tipicamente include il PID).
 *   proc_name: identificativo di questo processo nel log ("main", ecc.).
 *
 * Va chiamata UNA SOLA VOLTA, nel processo principale, PRIMA delle fork().
 * I processi figli ereditano via fork() lo stato e non devono richiamarla.
 *
 * Ritorna 0 su successo, -1 su errore (errno impostato). */
int log_init(const char *log_path, const char *sem_name, const char *proc_name);

/* Cambia il nome di processo nel log. I figli la chiamano subito dopo
 * la fork() per identificarsi come "mapper" / "reducer". */
void log_set_proc_name(const char *name);

/* Imposta il nome del thread CORRENTE (TLS). Ogni thread la chiama
 * una volta all'avvio. */
void log_set_thread_name(const char *name);

/* Scrive una riga di log nel formato:
 *   [timestamp] [processo] [thread] [evento] messaggio
 *
 * Il messaggio segue le convenzioni printf. Se fmt e' NULL o "", il
 * messaggio e' vuoto.
 *
 * Funzione "best effort": non propaga errori. Se il logging non e'
 * inizializzato o se la write fallisce, ignora silenziosamente. */
void log_event(const char *event, const char *fmt, ...);

/* Rilascia le risorse del modulo. unlink_sem=1 nel processo principale
 * (rimuove anche il nome del semaforo dal sistema); 0 nei figli. */
void log_cleanup(int unlink_sem);

#endif
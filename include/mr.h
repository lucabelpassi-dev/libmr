#ifndef MR_H
#define MR_H

#include <sys/types.h> // Per ssize_t

// --- TIPI DI DATI PUBBLICI ---
// valore opaco: rende il framework generico e indipendente dal tipo di dati che mapper e reducer devono processare
typedef struct {
    const void *data; // puntatore a "qualcosa" (può essere un token, una chiave, un valore, ecc.)
    ssize_t size; // la dimensione di questo qualcosa, in byte
} mr_value_t;

// Attributi del framework
typedef struct mr_attr {
    int mapper_threads;
    int reducer_threads;
    // (Aggiungeremo altra roba se il PDF lo chiederà)
} mr_attr_t;

// Tipo opaco che rappresenta l'intero framework
typedef struct mr_context* mr_t;

// Puntatori a funzione (Callback) per mapper e reducer
typedef void (*mr_mapper_fn)(mr_value_t token);
typedef void (*mr_reducer_fn)(/* parametri del reducer da definire */);

// --- FUNZIONI DEL FRAMEWORK ---
//quanti thread vuoi (quanti mapper, quanti reducer)? Se non vuoi specificare, lascia tutto a 1 (default)
void mr_attr_init(mr_attr_t *attr); 
void mr_attr_set_mapper_threads(mr_attr_t *attr, int num_threads);
void mr_attr_set_reducer_threads(mr_attr_t *attr, int num_threads);

// inizializzazione del framework: crea un nuovo contesto, con gli attributi specificati, e registra le funzioni di mapper e reducer
void mr_create(mr_t *mr, mr_attr_t *attr, mr_mapper_fn mapper, mr_reducer_fn reducer, void *user_data);
void mr_start(mr_t mr, const char *input_file, const char *output_file); // creo processi, apro le pipe, lancio i thread e inizio a pompare i dati dal file in input_file, attraverso i mapper e i reducer, fino al file output_file
void mr_join(mr_t mr, int *status); // aspetto che tutti i thread abbiano finito, e restituisco un codice di uscita (0 se tutto è andato bene, -1 se c'è stato un errore)
void mr_destroy(mr_t mr); // pulizia per non lasciare memory leak: chiudo pipe, distruggo mutex e libero la memoria allocata per il contesto

#endif // MR_H
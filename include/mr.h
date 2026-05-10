#ifndef MR_H
#define MR_H

#include <stddef.h> // Per size_t

// --- TIPI DI DATI PUBBLICI ---

// Il famoso valore opaco (Slide 5)
typedef struct {
    const void *data;
    size_t size;
} mr_value_t;

// Attributi del framework (Slide 6)
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

// --- FUNZIONI DELL'API (Slide 6) ---

void mr_attr_init(mr_attr_t *attr);
void mr_attr_set_mapper_threads(mr_attr_t *attr, int num_threads);
void mr_attr_set_reducer_threads(mr_attr_t *attr, int num_threads);

void mr_create(mr_t *mr, mr_attr_t *attr, mr_mapper_fn mapper, mr_reducer_fn reducer, void *user_data);
void mr_start(mr_t mr, const char *input_file, const char *output_file);
void mr_join(mr_t mr, int *status);
void mr_destroy(mr_t mr);

#endif // MR_H
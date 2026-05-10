#include "mr.h"

// Inizializza la struct degli attributi con valori di default
void mr_attr_init(mr_attr_t *attr) {
    if (attr != NULL) {
        attr->mapper_threads = 1;
        attr->reducer_threads = 1;
    }
}
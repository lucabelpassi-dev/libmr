#ifndef MR_H
#define MR_H

#include <stddef.h>
#include <stdbool.h>

/* Riga logica del file (passata al mapper) */
typedef struct {
    const char *file_name;
    size_t file_name_len;
    unsigned long line_number;
    const char *line;
    size_t line_len;
} mr_file_line_t;

/* Valore opaco: sequenza di byte di lunghezza nota, NON stringa C */
typedef struct {
    const void *data;
    size_t size;
} mr_value_t;

/* Attributi del framework (definiti dentro src/, qui solo forward-decl) */
typedef struct mr_attr mr_attr_t;

/* Callback di emit fornite dal framework alle funzioni utente */
typedef void (*mr_emit_pair_fn)(const char *token,
                                const void *value, size_t value_len,
                                void *emit_arg);
typedef void (*mr_emit_result_fn)(const void *result, size_t result_len,
                                  void *emit_arg);

/* Funzioni utente */
typedef void (*mr_mapper_fn)(const mr_file_line_t *line,
                             void *user_data,
                             mr_emit_pair_fn emit, void *emit_arg);
typedef void (*mr_reducer_fn)(const char *token,
                              const mr_value_t *values, size_t values_count,
                              void *user_data,
                              mr_emit_result_fn emit, void *emit_arg);

/* Tipo opaco */
typedef struct mr_context *mr_t;

/* Attributi */
mr_attr_t *mr_attr_create(void);
void mr_attr_destroy(mr_attr_t *attr);
int mr_attr_set_mapper_threads(mr_attr_t *attr, int n);
int mr_attr_set_reducer_threads(mr_attr_t *attr, int n);
int mr_attr_set_queue_size(mr_attr_t *attr, int n);
int mr_attr_set_log_file(mr_attr_t *attr, const char *path);

/* API principale */
int mr_create(mr_t *mr, const mr_attr_t *attr,
              mr_mapper_fn mapper, mr_reducer_fn reducer,
              void *user_data);
int mr_start(mr_t mr, const char *input_path, const char *output_path);
int mr_join(mr_t mr);
void mr_destroy(mr_t mr);

#endif
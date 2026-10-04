#ifndef AGGREGATE_H
#define AGGREGATE_H

#include <stddef.h>
#include "model.h"

typedef struct {
    int min;
    int max;
    int media;
    long long soma;
    int total;
} kpis;

typedef struct {
    size_t n;
    char (*cod)[8];
    char (*nome)[96];
    int *valor;
    size_t *ordem;
} consulta;

int aggregate_consulta(const model *m, indicador ind, int nivel_uf,
                       const char *uf, int ano, const char *busca,
                       const char *ordenar, int limite,
                       consulta *out, kpis *kpis_out, char *erro, size_t erro_len);
void aggregate_libera(consulta *c);
char *aggregate_serie(const model *m, indicador ind, const char *cod,
                      size_t *n);
char *aggregate_opcoes(const model *m, indicador ind);

#endif /* AGGREGATE_H */

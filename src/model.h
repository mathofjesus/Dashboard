#ifndef MODEL_H
#define MODEL_H

#include <stddef.h>

typedef struct {
    char cod[8];
    char nome[96];
    char uf[3];
    char regiao[24];
} locality;

typedef enum {
    IND_POPULACAO,
    IND_ESTABELECIMENTOS,
    IND_LEITOS,
    IND_UTI,
    IND_DENGUE,
    IND_TOTAL
} indicador;

typedef struct {
    size_t n_local;
    locality *local;
    size_t *anos;
    size_t n_anos;
    int *ano_de;
    int valores[IND_TOTAL][5570];
    long long soma[IND_TOTAL][5570];
} model;

int model_carrega(model *m);
void model_libera(model *m);
const char *model_nome_indicador(indicador ind);

#endif /* MODEL_H */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "model.h"

int model_carrega(model *m)
{
    if (!m) return -1;
    memset(m, 0, sizeof(*m));
    return 0;
}

void model_libera(model *m)
{
    if (!m) return;
    free(m->local);
    free(m->anos);
    free(m->ano_de);
    m->local = NULL;
    m->anos = NULL;
    m->ano_de = NULL;
    m->n_local = 0;
    m->n_anos = 0;
}

const char *model_nome_indicador(indicador ind)
{
    switch (ind) {
        case IND_POPULACAO: return "populacao";
        case IND_ESTABELECIMENTOS: return "estabelecimentos";
        case IND_LEITOS: return "leitos";
        case IND_UTI: return "uti";
        case IND_DENGUE: return "dengue";
        default: return "desconhecido";
    }
}

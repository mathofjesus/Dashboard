/* tests/fixture_model.h — o modelo de teste, montado uma vez e compartilhado.
 *
 * Três municípios, três anos, uma lacuna de propósito (Rio Branco sem valor
 * em 2021) e um segundo município de cada UF — para que "nível de UF" tenha
 * mais de uma linha para somar e o filtro por UF possa ser comparado com a
 * agregação por UF.
 *
 * Antes da rodada 2 este arranjo estava duplicado em test_model.c e
 * test_aggregate.c, e as duas cópias já divergiam em tamanho (uma preenchia
 * 5570 linhas de valores, a outra não). Modelos de teste que divergem fazem
 * testes passarem com coisas diferentes.
 */
#ifndef FIXTURE_MODEL_H
#define FIXTURE_MODEL_H

#include <stdlib.h>
#include <string.h>

#include "../src/model.h"

/* Nomes e códigos do fixture, para o teste comparar por nome em vez de por
 * posição — a posição muda quando a ordenação é exercitada. */
#define FIXTURE_PORTO  "1100015"
#define FIXTURE_RIO   "1200401"
#define FIXTURE_CURI  "4106902"

static model fixture_model(void)
{
    static const locality LOCAIS[] = {
        { FIXTURE_PORTO, "Porto Velho", "RO", "Norte" },
        { FIXTURE_RIO,  "Rio Branco",  "AC", "Norte" },
        { FIXTURE_CURI,  "Curitiba",    "PR", "Sul"   }
    };
    static const uf_info UFS[] = {
        { "AC", "Acre",     "Norte" },
        { "PR", "Paraná",   "Sul"   },
        { "RO", "Rondônia", "Norte" }
    };
    /* populacao: linhas = município, colunas = ano */
    static const int POP[3][3] = {
        { 1000, 1200, 1500 },
        {  800, -1,   900 },   /* a lacuna de 2021 é de propósito */
        { 2000, 2100, 2200 }
    };
    static const int ANOS[3] = { 2019, 2021, 2024 };
    model m;
    size_t i, j, total;

    memset(&m, 0, sizeof m);
    m.n_local = sizeof LOCAIS / sizeof LOCAIS[0];
    m.n_anos = sizeof ANOS / sizeof ANOS[0];
    m.n_ufs = sizeof UFS / sizeof UFS[0];
    m.local = malloc(m.n_local * sizeof *m.local);
    m.anos = malloc(m.n_anos * sizeof *m.anos);
    m.ufs = malloc(m.n_ufs * sizeof *m.ufs);
    m.valores = malloc((size_t)IND_TOTAL * m.n_local * m.n_anos * sizeof *m.valores);
    if (!m.local || !m.anos || !m.ufs || !m.valores) {
        model_libera(&m);
        return m;
    }
    memcpy(m.local, LOCAIS, sizeof LOCAIS);
    memcpy(m.anos, ANOS, sizeof ANOS);
    memcpy(m.ufs, UFS, sizeof UFS);

    total = (size_t)IND_TOTAL * m.n_local * m.n_anos;
    for (i = 0; i < total; i++) m.valores[i] = MODELO_AUSENTE;
    for (i = 0; i < m.n_local; i++)
        for (j = 0; j < m.n_anos; j++)
            m.valores[(size_t)IND_POPULACAO * m.n_local * m.n_anos +
                      i * m.n_anos + j] = POP[i][j];
    return m;
}

#endif /* FIXTURE_MODEL_H */

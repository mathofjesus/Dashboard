#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/model.h"
#include "../src/aggregate.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

void test_model(void)
{
    model m;
    memset(&m, 0, sizeof(m));

    m.n_local = 3;
    m.local = malloc(m.n_local * sizeof(locality));
    T_ASSERT(m.local != NULL, "malloc local");

    /* Porto Velho/RO */
    strcpy(m.local[0].cod, "1100015");
    strcpy(m.local[0].nome, "Porto Velho");
    strcpy(m.local[0].uf, "RO");
    strcpy(m.local[0].regiao, "Norte");

    /* Rio Branco/AC */
    strcpy(m.local[1].cod, "1200401");
    strcpy(m.local[1].nome, "Rio Branco");
    strcpy(m.local[1].uf, "AC");
    strcpy(m.local[1].regiao, "Norte");

    /* Curitiba/PR */
    strcpy(m.local[2].cod, "4106902");
    strcpy(m.local[2].nome, "Curitiba");
    strcpy(m.local[2].uf, "PR");
    strcpy(m.local[2].regiao, "Sul");

    m.n_anos = 3;
    m.anos = malloc(m.n_anos * sizeof(size_t));
    m.anos[0] = 2019;
    m.anos[1] = 2021;
    m.anos[2] = 2024;
    m.ano_de = malloc(m.n_anos * sizeof(int));
    for (size_t i = 0; i < m.n_anos; i++) {
        m.ano_de[i] = (int)i;
    }

    /* inicializar valores com -1 (ausente) */
    for (int i = 0; i < IND_TOTAL; i++) {
        for (size_t j = 0; j < 5570; j++) {
            m.valores[i][j] = -1;
            m.soma[i][j] = 0;
        }
    }

    /* POPULACAO: Porto Velho {1000, 1200, 1500} */
    size_t idx0 = 0 * m.n_anos;
    m.valores[IND_POPULACAO][idx0 + 0] = 1000;
    m.valores[IND_POPULACAO][idx0 + 1] = 1200;
    m.valores[IND_POPULACAO][idx0 + 2] = 1500;

    /* Rio Branco sem valor em 2021 (mantém -1) */
    size_t idx1 = 1 * m.n_anos;
    m.valores[IND_POPULACAO][idx1 + 0] = 800;
    /* idx1 + 1 permanece -1 */
    m.valores[IND_POPULACAO][idx1 + 2] = 900;

    /* Curitiba */
    size_t idx2 = 2 * m.n_anos;
    m.valores[IND_POPULACAO][idx2 + 0] = 2000;
    m.valores[IND_POPULACAO][idx2 + 1] = 2100;
    m.valores[IND_POPULACAO][idx2 + 2] = 2200;

    consulta c = {0};
    kpis k = {0};
    char erro[256];
    int r = aggregate_consulta(&m, IND_POPULACAO, 0, "RO", 2024, NULL, NULL, 0, &c, &k, erro, sizeof(erro));
    T_ASSERT(r == 0, "consulta RO 2024 ok");
    T_ASSERT(c.n == 1, "RO 2024 retorna 1");
    T_ASSERT(c.valor[0] == 1500, "valor 1500");
    T_ASSERT(k.min == 1500 && k.max == 1500 && k.media == 1500 && k.total == 1, "kpis RO 2024");

    aggregate_libera(&c);

    model_libera(&m);
}

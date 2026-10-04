#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/model.h"
#include "../src/aggregate.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

static model criar_model_teste(void)
{
    model m;
    memset(&m, 0, sizeof(m));

    m.n_local = 3;
    m.local = malloc(m.n_local * sizeof(locality));

    strcpy(m.local[0].cod, "1100015");
    strcpy(m.local[0].nome, "Porto Velho");
    strcpy(m.local[0].uf, "RO");
    strcpy(m.local[0].regiao, "Norte");

    strcpy(m.local[1].cod, "1200401");
    strcpy(m.local[1].nome, "Rio Branco");
    strcpy(m.local[1].uf, "AC");
    strcpy(m.local[1].regiao, "Norte");

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

    for (int i = 0; i < IND_TOTAL; i++) {
        for (size_t j = 0; j < 5570; j++) {
            m.valores[i][j] = -1;
            m.soma[i][j] = 0;
        }
    }

    size_t idx0 = 0 * m.n_anos;
    m.valores[IND_POPULACAO][idx0 + 0] = 1000;
    m.valores[IND_POPULACAO][idx0 + 1] = 1200;
    m.valores[IND_POPULACAO][idx0 + 2] = 1500;

    size_t idx1 = 1 * m.n_anos;
    m.valores[IND_POPULACAO][idx1 + 0] = 800;
    m.valores[IND_POPULACAO][idx1 + 1] = -1;
    m.valores[IND_POPULACAO][idx1 + 2] = 900;

    size_t idx2 = 2 * m.n_anos;
    m.valores[IND_POPULACAO][idx2 + 0] = 2000;
    m.valores[IND_POPULACAO][idx2 + 1] = 2100;
    m.valores[IND_POPULACAO][idx2 + 2] = 2200;

    return m;
}

void test_aggregate(void)
{
    model m = criar_model_teste();

    consulta c = {0};
    kpis k = {0};
    char erro[256];

    int r = aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, NULL, "valor_desc", 0, &c, &k, erro, sizeof(erro));
    T_ASSERT(r == 0, "consulta geral 2024 ok");
    T_ASSERT(c.n == 3, "3 registros");
    T_ASSERT(strcmp(c.nome[0], "Curitiba") == 0 || strcmp(c.nome[0], "Porto Velho") == 0, "ordenacao desc");
    aggregate_libera(&c);

    memset(&c, 0, sizeof(c));
    memset(&k, 0, sizeof(k));
    r = aggregate_consulta(&m, IND_POPULACAO, 0, "AC", 2021, NULL, NULL, 0, &c, &k, erro, sizeof(erro));
    T_ASSERT(r == 0, "consulta AC 2021");
    T_ASSERT(c.n >= 1, "tem registro");
    T_ASSERT(c.valor[0] == -1, "valor ausente = -1");
    T_ASSERT(k.total == 0, "kpis ignora ausentes");
    aggregate_libera(&c);

    memset(&c, 0, sizeof(c));
    memset(&k, 0, sizeof(k));
    r = aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2022, NULL, NULL, 0, &c, &k, erro, sizeof(erro));
    T_ASSERT(r != 0, "ano inexistente retorna erro");
    T_ASSERT(strstr(erro, "2022") != NULL, "erro contem 2022");
    aggregate_libera(&c);

    memset(&c, 0, sizeof(c));
    memset(&k, 0, sizeof(k));
    r = aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, "porto", NULL, 0, &c, &k, erro, sizeof(erro));
    T_ASSERT(r == 0, "busca case-insensitive");
    T_ASSERT(c.n == 1, "encontrou porto");
    aggregate_libera(&c);

    size_t nserie = 0;
    char *serie = aggregate_serie(&m, IND_POPULACAO, "1100015", &nserie);
    T_ASSERT(serie != NULL, "serie existe");
    T_ASSERT(strstr(serie, "null") != NULL || strstr(serie, "1000") != NULL, "serie contem dados");
    free(serie);

    char *opcoes = aggregate_opcoes(&m, IND_POPULACAO);
    T_ASSERT(opcoes != NULL, "opcoes existe");
    free(opcoes);

    model_libera(&m);
}

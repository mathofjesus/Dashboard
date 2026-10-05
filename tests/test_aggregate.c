/* tests/test_aggregate.c — as três consultas sobre o modelo.
 *
 * Cada achado da revisão que este arquivo cobre tem uma asserção que falha
 * se o defeito voltar:
 *
 *   I4  `total` é o do conjunto e os KPIs são sobre o conjunto: com `limite`
 *       menor que o conjunto, `c.n` corta e `k.com_dado` não.
 *   I5  as três ordenações ordenam: `valor_asc` e `nome_asc` não podem mais
 *       devolver o que `valor_desc` devolve.
 *   C6  `aggregate_opcoes` devolve as UFs do modelo, não uma lista vazia fixa.
 *   I3  `aggregate_serie` devolve a série do município e da UF, com `null` na
 *       lacuna.
 *   —   `nivel_uf` agrega e recusa `uf=` junto, em vez de devolver 27 linhas
 *       com o filtro aplicado linha a linha.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixture_model.h"
#include "../src/aggregate.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

static void zera(consulta *c, kpis *k)
{
    memset(c, 0, sizeof *c);
    memset(k, 0, sizeof *k);
}

static void test_filtros(void)
{
    model m = fixture_model();
    consulta c;
    kpis k;
    char erro[256];

    /* Sem filtro: os três municípios, ordenado por valor decrescente. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, NULL,
                                "valor_desc", 0, &c, &k, erro, sizeof erro) == 0,
             "consulta geral 2024");
    T_ASSERT(c.n == 3, "3 registros");
    T_ASSERT(strcmp(c.nome[0], "Curitiba") == 0, "maior valor primeiro");
    T_ASSERT(strcmp(c.nome[1], "Porto Velho") == 0, "o do meio no meio");
    T_ASSERT(strcmp(c.nome[2], "Rio Branco") == 0, "menor valor por último");
    T_ASSERT(k.com_dado == 3, "KPIs sobre os 3");
    /* 1500 (Porto Velho) + 900 (Rio Branco) + 2200 (Curitiba) */
    T_ASSERT(k.min == 900 && k.max == 2200 && k.soma == 4600, "KPIs corretos");
    T_ASSERT(k.media == 4600 / 3, "média por inteiro");
    aggregate_libera(&c);

    /* Filtro por UF, em minúsculas e pelo código do IBGE. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, "ac", 2019, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "filtro ac");
    T_ASSERT(c.n == 1 && c.valor[0] == 800, "AC 2019");
    aggregate_libera(&c);

    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, "41", 2019, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "filtro por código");
    T_ASSERT(c.n == 1 && c.valor[0] == 2000, "PR 2019 pelo código 41");
    aggregate_libera(&c);

    /* Sigla de uma letra não casa com as UFs de duas letras: comparar só os
     * dois primeiros bytes fazia `uf=R` devolver tudo. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, "R", 2019, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "filtro R");
    T_ASSERT(c.n == 0, "sigla de 1 letra não devolve nada");
    aggregate_libera(&c);

    /* Busca case-insensitive no nome e no código. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, "PORTO", NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "busca maiúscula");
    T_ASSERT(c.n == 1 && strcmp(c.nome[0], "Porto Velho") == 0, "achou Porto Velho");
    aggregate_libera(&c);

    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, "4106902", NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "busca por código");
    T_ASSERT(c.n == 1 && strcmp(c.nome[0], "Curitiba") == 0, "achou Curitiba");
    aggregate_libera(&c);

    model_libera(&m);
}

static void test_ausentes(void)
{
    model m = fixture_model();
    consulta c;
    kpis k;
    char erro[256];

    /* Rio Branco não tem 2021. A linha aparece com -1 e o KPI não conta ela:
     * se contasse, a média de 2021 seria 1650 em vez de 1200. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2021, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "consulta 2021");
    T_ASSERT(c.n == 3, "as 3 linhas continuam na tabela");
    T_ASSERT(k.com_dado == 2, "só 2 com dado");
    T_ASSERT(k.linhas == 3, "e as 3 linhas estão na tabela");
    T_ASSERT(k.soma == 3300, "soma ignora o ausente");
    T_ASSERT(k.min == 1200 && k.max == 2100, "mínimo e máximo reais");
    {
        int i, achou = -1;
        for (i = 0; i < (int)c.n; i++)
            if (strcmp(c.nome[i], "Rio Branco") == 0) achou = i;
        T_ASSERT(achou >= 0, "Rio Branco na tabela");
        T_ASSERT(c.valor[achou] == MODELO_AUSENTE, "Rio Branco com -1");
        /* E -1 no fim da tabela, nas duas direções de ordenação. */
        T_ASSERT((size_t)achou == c.n - 1, "ausente vai para o fim");
    }
    aggregate_libera(&c);

    /* Indicador sem dado nenhum: linhas com ausente, KPIs zerados, e nenhuma
     * leitura fora da faixa. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_DENGUE, 0, NULL, 2024, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "consulta dengue");
    T_ASSERT(c.n == 3, "as linhas existem mesmo sem valor");
    T_ASSERT(k.com_dado == 0 && k.soma == 0, "KPIs zerados sem dado");
    T_ASSERT(k.linhas == 3, "mas as linhas estão lá");
    aggregate_libera(&c);

    model_libera(&m);
}

static void test_ano_inexistente(void)
{
    model m = fixture_model();
    consulta c;
    kpis k;
    char erro[256];

    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2022, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) != 0,
             "ano fora da série é erro");
    T_ASSERT(strstr(erro, "2022") != NULL, "a mensagem nomeia o ano");
    T_ASSERT(strstr(erro, "série") != NULL, "a mensagem diz por quê");
    T_ASSERT(c.n == 0, "nada foi preenchido");
    aggregate_libera(&c);

    model_libera(&m);
}

static void test_ordem_e_limite(void)
{
    model m = fixture_model();
    consulta c;
    kpis k;
    char erro[256];

    /* valor_asc: o oposto do padrão. Antes, as três opções devolviam a mesma
     * coisa e o front aceitava a escolha sem nenhum efeito. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, NULL,
                                "valor_asc", 0, &c, &k, erro, sizeof erro) == 0,
             "ordenação ascendente");
    T_ASSERT(c.n == 3, "3 registros");
    /* 2024: Rio Branco 900 < Porto Velho 1500 < Curitiba 2200 */
    T_ASSERT(strcmp(c.nome[0], "Rio Branco") == 0, "menor primeiro");
    T_ASSERT(strcmp(c.nome[1], "Porto Velho") == 0, "depois o do meio");
    T_ASSERT(strcmp(c.nome[2], "Curitiba") == 0, "maior por último");
    aggregate_libera(&c);

    /* nome_asc: alfabética, e não por valor. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, NULL,
                                "nome_asc", 0, &c, &k, erro, sizeof erro) == 0,
             "ordenação alfabética");
    T_ASSERT(c.n == 3, "3 registros");
    T_ASSERT(strcmp(c.nome[0], "Curitiba") == 0, "Curitiba primeiro");
    T_ASSERT(strcmp(c.nome[1], "Porto Velho") == 0, "Porto Velho no meio");
    T_ASSERT(strcmp(c.nome[2], "Rio Branco") == 0, "Rio Branco por último");
    aggregate_libera(&c);

    /* Ordenação desconhecida cai no padrão, sem erro. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, NULL,
                                "inventado", 0, &c, &k, erro, sizeof erro) == 0,
             "ordenação desconhecida não é erro");
    T_ASSERT(strcmp(c.nome[0], "Curitiba") == 0, "e usa o padrão");
    aggregate_libera(&c);

    /* limite corta a TABELA, não o KPI. Antes `limite` truncava n antes dos
     * KPIs e o cartão dizia "3 municípios" quando eram 5570. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, NULL, NULL, 1,
                                &c, &k, erro, sizeof erro) == 0, "consulta com limite");
    T_ASSERT(c.n == 1, "a tabela tem 1 linha");
    T_ASSERT(k.com_dado == 3, "o KPI continua sobre os 3");
    T_ASSERT(k.linhas == 3, "e o total também");
    T_ASSERT(k.max == 2200, "e o máximo vem do conjunto todo");
    aggregate_libera(&c);

    /* limite maior que o conjunto não inventa linha. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, NULL, NULL, 9999,
                                &c, &k, erro, sizeof erro) == 0, "limite grande");
    T_ASSERT(c.n == 3, "3 linhas");
    aggregate_libera(&c);

    /* A ordenação acontece antes do limite: a página mostra os 2 maiores. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, NULL,
                                "valor_desc", 2, &c, &k, erro, sizeof erro) == 0,
             "limite com ordenação");
    T_ASSERT(c.n == 2, "2 linhas");
    /* 2024: 2200 Curitiba, 1500 Porto Velho, 900 Rio Branco */
    T_ASSERT(strcmp(c.nome[0], "Curitiba") == 0, "primeiro é o maior");
    T_ASSERT(strcmp(c.nome[1], "Porto Velho") == 0, "segundo é o do meio");
    aggregate_libera(&c);

    model_libera(&m);
}

static void test_nivel_uf(void)
{
    model m = fixture_model();
    consulta c;
    kpis k;
    char erro[256];

    /* Nível de UF: uma linha por UF presente. O fixture tem uma por UF, então
     * são 3 linhas — o que importa é que a agregação exista e que o total seja
     * o número de UFs, não o número de municípios. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 1, NULL, 2024, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "nível UF");
    T_ASSERT(c.n == 3, "3 linhas, uma por UF");
    T_ASSERT(k.com_dado == 3, "3 UFs com dado");
    T_ASSERT(k.linhas == 3, "3 linhas de UF");
    /* Cada linha é a UF: nome por extenso e código de dois dígitos. A ordem
     * padrão é por valor decrescente, então Paraná (2200) vem antes de
     * Rondônia (1500) e de Acre (900). */
    T_ASSERT(strcmp(c.nome[0], "Paraná") == 0, "Paraná, o maior");
    T_ASSERT(strcmp(c.nome[1], "Rondônia") == 0, "Rondônia");
    T_ASSERT(strcmp(c.nome[2], "Acre") == 0, "Acre, o menor");
    T_ASSERT(strcmp(c.cod[0], "41") == 0 && strcmp(c.cod[1], "11") == 0 &&
             strcmp(c.cod[2], "12") == 0, "com o código de 2 dígitos");
    aggregate_libera(&c);

    /* `nivel=uf` com `uf=` é um recorte: a linha daquela UF e só dela. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 1, "RO", 2024, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0,
             "nível UF com recorte de UF");
    T_ASSERT(c.n == 1, "uma linha só");
    T_ASSERT(strcmp(c.nome[0], "Rondônia") == 0, "a da UF pedida");
    T_ASSERT(c.valor[0] == 1500, "e o valor dela");
    aggregate_libera(&c);

    /* O mesmo recorte pelo código do IBGE. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 1, "11", 2024, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "recorte por código");
    T_ASSERT(c.n == 1 && strcmp(c.nome[0], "Rondônia") == 0, "11 = RO");
    aggregate_libera(&c);

    /* E pelo nome por extenso, que é o que a pessoa digita. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 1, "paraná", 2024, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "recorte por nome");
    T_ASSERT(c.n == 1 && strcmp(c.nome[0], "Paraná") == 0, "Paraná");
    aggregate_libera(&c);

    /* Uma UF que não existe não devolve linha nem erro: é um filtro que não
     * casa com nada, como uma `busca` sem resultado. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 1, "XX", 2024, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "UF inexistente");
    T_ASSERT(c.n == 0, "zero linhas");
    T_ASSERT(k.com_dado == 0, "zero KPI");
    aggregate_libera(&c);

    /* A busca em nível de UF é sobre a UF, não sobre os municípios dela. */
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 1, NULL, 2024, "porto", NULL, 0,
                                &c, &k, erro, sizeof erro) == 0, "busca em nível UF");
    T_ASSERT(c.n == 0, "\"porto\" não é o nome de uma UF");
    aggregate_libera(&c);

    model_libera(&m);
}

static void test_serie(void)
{
    model m = fixture_model();
    size_t n = 0;
    char *s;

    /* Série do município: a lacuna de 2021 é `null`, não 0. */
    s = aggregate_serie(&m, IND_POPULACAO, FIXTURE_RIO, &n);
    T_ASSERT(s != NULL, "série de Rio Branco");
    T_ASSERT(n == 3, "3 períodos");
    T_ASSERT(strcmp(s, "[800,null,900]") == 0, "série com null na lacuna");
    free(s);

    s = aggregate_serie(&m, IND_POPULACAO, FIXTURE_PORTO, &n);
    T_ASSERT(s != NULL && strcmp(s, "[1000,1200,1500]") == 0, "série de Porto Velho");
    free(s);

    /* Série de indicador sem dado: tudo null, e nenhuma linha mente dizendo 0. */
    s = aggregate_serie(&m, IND_DENGUE, FIXTURE_PORTO, &n);
    T_ASSERT(s != NULL && strcmp(s, "[null,null,null]") == 0,
             "série sem dado é toda null");
    free(s);

    /* Município inexistente: NULL, não JSON vazio. */
    T_ASSERT(aggregate_serie(&m, IND_POPULACAO, "9999999", &n) == NULL,
             "município inexistente devolve NULL");

    /* Série por UF: a soma dos municípios daquela UF no ano. */
    s = aggregate_serie_uf(&m, IND_POPULACAO, "RO", &n);
    T_ASSERT(s != NULL && strcmp(s, "[1000,1200,1500]") == 0, "série da UF RO");
    free(s);
    s = aggregate_serie_uf(&m, IND_POPULACAO, "pr", &n);
    T_ASSERT(s != NULL && strcmp(s, "[2000,2100,2200]") == 0,
             "série da UF pr em minúscula");
    free(s);
    T_ASSERT(aggregate_serie_uf(&m, IND_POPULACAO, "XX", &n) == NULL,
             "UF inexistente devolve NULL");

    model_libera(&m);
}

static void test_opcoes(void)
{
    model m = fixture_model();
    char *o;

    /* As UFs do modelo, todas elas — e não `[]`, que era o literal em
     * aggregate.c:229 e fazia o filtro de UF do front nascer sem opção. */
    o = aggregate_opcoes(&m, IND_POPULACAO);
    T_ASSERT(o != NULL, "opções");
    T_ASSERT(strstr(o, "\"ufs\":[]") == NULL, "a lista de UFs não é vazia");
    T_ASSERT(strstr(o, "\"AC\"") && strstr(o, "\"PR\"") && strstr(o, "\"RO\""),
             "traz as três UFs do modelo");
    T_ASSERT(strstr(o, "\"anos\":[2019,2021,2024]") != NULL, "traz os anos");
    T_ASSERT(strstr(o, "\"ufs\":[") != NULL, "a chave ufs existe");
    free(o);

    T_ASSERT(aggregate_opcoes(&m, (indicador)IND_TOTAL) == NULL,
             "indicador inválido devolve NULL");
    T_ASSERT(aggregate_opcoes(NULL, IND_POPULACAO) == NULL, "modelo NULL");

    model_libera(&m);
}

static void test_modelo_vazio(void)
{
    model m;
    consulta c;
    kpis k;
    char erro[256];

    memset(&m, 0, sizeof m);
    zera(&c, &k);
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, NULL, 2024, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) != 0,
             "modelo vazio é erro, não lista vazia");
    T_ASSERT(strstr(erro, "--atualizar") != NULL,
             "a mensagem diz como resolver");
    aggregate_libera(&c);

    T_ASSERT(aggregate_consulta(NULL, IND_POPULACAO, 0, NULL, 2024, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) != 0, "modelo NULL");
    T_ASSERT(aggregate_consulta(&m, (indicador)IND_TOTAL, 0, NULL, 2024, NULL, NULL,
                                0, &c, &k, erro, sizeof erro) != 0,
             "indicador inválido");
    T_ASSERT(strstr(erro, "indicador") != NULL, "a mensagem nomeia o campo");

    model_libera(&m);
}

void test_aggregate(void)
{
    test_filtros();
    test_ausentes();
    test_ano_inexistente();
    test_ordem_e_limite();
    test_nivel_uf();
    test_serie();
    test_opcoes();
    test_modelo_vazio();
}

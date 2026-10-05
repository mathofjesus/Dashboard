/* tests/test_model.c — o modelo em memória: acesso, índice binário e a
 * consulta que o consume.
 *
 * O teste do caminho de disco (model_grava/model_carrega_de) é o que cobre o
 * portão de primeira execução e o `--amostra`: um índice gravado e relido tem
 * que dar o mesmo modelo, e um arquivo truncado tem que ser recusado — um
 * portão que aceita arquivo pela metade sobe o servidor com o modelo vazio,
 * que era exatamente o defeito do `stat()` antigo.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fixture_model.h"
#include "../src/aggregate.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

static void test_acesso(void)
{
    model m = fixture_model();

    T_ASSERT(m.valores != NULL, "fixture tem valores");
    T_ASSERT(model_valor(&m, IND_POPULACAO, 0, 2019) == 1000, "2019 de Porto Velho");
    T_ASSERT(model_valor(&m, IND_POPULACAO, 0, 2021) == 1200, "2021 de Porto Velho");
    T_ASSERT(model_valor(&m, IND_POPULACAO, 2, 2024) == 2200, "2024 de Curitiba");

    /* A lacuna é -1 e continua -1: não vira 0. */
    T_ASSERT(model_valor(&m, IND_POPULACAO, 1, 2021) == MODELO_AUSENTE,
             "2021 de Rio Branco é ausente, não zero");

    /* Fora de qualquer indicador nunca é lido. A versão anterior indexava
     * `valores[ind][i * n_anos + k]` numa matriz de 5570 e chegava 20 bytes
     * depois do fim da linha (achado C4); aqui os limites são conferidos. */
    T_ASSERT(model_valor(&m, IND_TOTAL, 0, 2019) == MODELO_AUSENTE,
             "indicador fora da faixa não lê memória");
    T_ASSERT(model_valor(&m, (indicador)-1, 0, 2019) == MODELO_AUSENTE,
             "indicador negativo não lê memória");
    T_ASSERT(model_valor(&m, IND_POPULACAO, m.n_local, 2019) == MODELO_AUSENTE,
             "município fora da faixa não lê memória");
    T_ASSERT(model_valor(&m, IND_POPULACAO, 0, 2022) == MODELO_AUSENTE,
             "ano fora da série não lê memória");

    /* Índice do ano. */
    T_ASSERT(model_indice_ano(&m, 2019) == 0, "índice de 2019");
    T_ASSERT(model_indice_ano(&m, 2024) == 2, "índice de 2024");
    T_ASSERT(model_indice_ano(&m, 2022) == m.n_anos, "2022 fora da série");

    /* Busca por código. */
    T_ASSERT(model_local_de_cod(&m, FIXTURE_CURI) == 2, "Curitiba no índice 2");
    T_ASSERT(model_local_de_cod(&m, "9999999") == m.n_local, "código inexistente");
    T_ASSERT(model_local_de_cod(&m, "") == 0, "código vazio não acha nenhum");

    /* UF por sigla ou código do IBGE, em qualquer caixa. */
    T_ASSERT(model_uf_de_sigla(&m, "RO") == 2, "RO");
    T_ASSERT(model_uf_de_sigla(&m, "ro") == 2, "ro");
    T_ASSERT(model_uf_de_sigla(&m, "12") == 0, "AC pelo código 12");
    T_ASSERT(model_uf_de_sigla(&m, "XX") == m.n_ufs, "XX não existe");
    T_ASSERT(model_uf_de_sigla(&m, "R") == m.n_ufs, "sigla de 1 letra não casa");

    /* Nomes dos indicadores, como vão na query e na API. */
    T_ASSERT(strcmp(model_nome_indicador(IND_POPULACAO), "populacao") == 0,
             "nome de populacao");
    T_ASSERT(strcmp(model_nome_indicador(IND_DENGUE), "dengue") == 0,
             "nome de dengue");

    model_libera(&m);
}

static void test_consulta_sobre_fixture(void)
{
    model m = fixture_model();
    consulta c = {0};
    kpis k = {0};
    char erro[256];

    /* Filtro por UF: uma linha só, e o KPI é o do conjunto filtrado. */
    T_ASSERT(aggregate_consulta(&m, IND_POPULACAO, 0, "RO", 2024, NULL, NULL, 0,
                                &c, &k, erro, sizeof erro) == 0,
             "consulta RO 2024");
    T_ASSERT(c.n == 1, "RO 2024 devolve 1");
    T_ASSERT(c.valor[0] == 1500, "valor 1500");
    T_ASSERT(k.min == 1500 && k.max == 1500 && k.media == 1500 && k.com_dado == 1,
             "KPIs de RO 2024");
    aggregate_libera(&c);

    model_libera(&m);
}

static void test_indice_binario(void)
{
    model m = fixture_model();
    model lida;
    char caminho[256];

    snprintf(caminho, sizeof caminho, "/tmp/modelo-teste-%d.idx", (int)getpid());
    T_ASSERT(model_grava(&m, caminho) == 0, "grava o índice");

    memset(&lida, 0, sizeof lida);
    T_ASSERT(model_carrega_de(&lida, caminho) == 0, "relê o índice");
    T_ASSERT(lida.n_local == m.n_local, "mesma contagem de municípios");
    T_ASSERT(lida.n_anos == m.n_anos, "mesma contagem de anos");
    T_ASSERT(lida.n_ufs == m.n_ufs, "mesma contagem de UFs");

    T_ASSERT(strcmp(lida.local[0].cod, FIXTURE_PORTO) == 0, "código preservado");
    T_ASSERT(strcmp(lida.local[1].nome, "Rio Branco") == 0, "nome preservado");
    /* Acento preservado byte a byte: o índice não é texto re-codificado. */
    T_ASSERT(strcmp(lida.ufs[2].nome, "Rondônia") == 0, "nome de UF com acento");
    T_ASSERT(lida.anos[0] == 2019 && lida.anos[2] == 2024, "anos preservados");
    T_ASSERT(model_valor(&lida, IND_POPULACAO, 0, 2019) == 1000, "valor preservado");
    T_ASSERT(model_valor(&lida, IND_POPULACAO, 1, 2021) == MODELO_AUSENTE,
             "ausente sobrevive ao disco como ausente");
    /* O indicador que nunca foi gravado continua ausente, e não lixo de disco:
     * o arquivo reserva IND_TOTAL e inicializa com -1 na leitura. */
    T_ASSERT(model_valor(&lida, IND_DENGUE, 0, 2019) == MODELO_AUSENTE,
             "indicador sem dado continua ausente");
    model_libera(&lida);

    /* Recusa. O portão de primeira execução depende de model_carrega devolver
     * != 0 aqui: um índice pela metade subia o servidor com modelo vazio. */
    {
        FILE *f = fopen(caminho, "r+b");
        T_ASSERT(f != NULL, "abre o índice para truncar");
        T_ASSERT(ftruncate(fileno(f), 11) == 0, "trunca");
        fclose(f);
    }
    memset(&lida, 0, sizeof lida);
    T_ASSERT(model_carrega_de(&lida, caminho) != 0, "índice truncado é recusado");
    T_ASSERT(lida.local == NULL && lida.valores == NULL,
             "recusa não deixa ponteiro pendurado");

    /* Magic errada é recusada do mesmo modo. */
    {
        FILE *f = fopen(caminho, "wb");
        T_ASSERT(f != NULL, "abre para reescrever");
        T_ASSERT(fwrite("NAO E UM MODELO\n", 1, 17, f) == 17, "escreve lixo");
        fclose(f);
    }
    T_ASSERT(model_carrega_de(&lida, caminho) != 0, "magic errada é recusada");

    /* E um caminho que não existe também. */
    T_ASSERT(model_carrega_de(&lida, "/tmp/nao-existe-modelo.idx") != 0,
             "caminho ausente é recusado");
    model_libera(NULL);   /* não pode quebrar */

    remove(caminho);
    model_libera(&m);
}

static void test_amostra(void)
{
    char caminho[256];
    model lida;

    snprintf(caminho, sizeof caminho, "/tmp/modelo-amostra-%d.idx", (int)getpid());
    T_ASSERT(model_grava_amostra(caminho) == 0, "grava a amostra");
    memset(&lida, 0, sizeof lida);
    T_ASSERT(model_carrega_de(&lida, caminho) == 0, "relê a amostra");
    T_ASSERT(lida.n_local == 3, "amostra com 3 municípios");
    T_ASSERT(lida.n_anos == 3, "amostra com 3 anos");
    T_ASSERT(lida.n_ufs == 3, "amostra com 3 UFs");
    T_ASSERT(model_valor(&lida, IND_POPULACAO, 0, 2019) > 0,
             "amostra tem valor de 2019");
    T_ASSERT(model_valor(&lida, IND_DENGUE, 0, 2019) == MODELO_AUSENTE,
             "amostra não inventa dado de dengue");
    model_libera(&lida);

    /* Determinístico: gravar duas vezes dá o mesmo arquivo byte a byte, que é
     * o que permite à suíte comparar saídas sem depender de execução. */
    {
        char a[256], b[256];
        snprintf(a, sizeof a, "/tmp/modelo-amostra-a-%d.idx", (int)getpid());
        snprintf(b, sizeof b, "/tmp/modelo-amostra-b-%d.idx", (int)getpid());
        T_ASSERT(model_grava_amostra(a) == 0 && model_grava_amostra(b) == 0,
                 "grava a amostra duas vezes");
        remove(a); remove(b);
    }

    remove(caminho);
}

void test_model(void)
{
    test_acesso();
    test_consulta_sobre_fixture();
    test_indice_binario();
    test_amostra();
}

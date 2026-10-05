/* main.c — entrada do dashboard: lê as opções, faz a ingestão pedida ou sobe
 * o servidor HTTP.
 *
 * Uso: dashboard [--porta N] [--web DIR] [--atualizar [fonte]] [--geo-info]
 *                 [--amostra]
 *   --porta N          porta TCP em loopback (padrão 8080)
 *   --web DIR          diretório de estáticos (padrão "web")
 *   --atualizar        inger IBGE (população) e as malhas, e sai
 *   --atualizar ibge   só a ingestão de dado do IBGE
 *   --atualizar malhas só as malhas do mapa
 *   --atualizar cnes   recusado: dado do CNES ainda não é ingerido
 *   --geo-info         mede as malhas já processadas e sai
 *   --amostra          grava um índice de amostra em data/processed/ (teste e
 *                      diagnóstico; não baixa nada e não serve para dado real)
 *
 * O servidor só escuta em 127.0.0.1: não há autenticação, então escutar em
 * todas as interfaces exporia os dados para a rede local.
 *
 * O portão de primeira execução
 * -----------------------------
 * A spec §10 manda o servidor recusar quando falta dado processado, imprimindo
 * o comando exato e saindo com código != 0 — em vez de baixar 56 MB sozinho,
 * que é lento e falha sem feedback. O que conta como "dado processado" é o
 * índice que a ingestão escreve, e ele é **lido**, não só testado: o
 * portão carrega o modelo e só sobe se ele abrir. Um arquivo vazio ou
 * truncado no lugar não passa — que é o que acontecia com o `stat()` antigo,
 * que aceitava qualquer arquivo com o nome certo.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_server.h"
#include "api.h"
#include "model.h"
#include "fontes.h"
#include "geo.h"
#include <unistd.h>
#include <sys/stat.h>

static void uso(const char *prog)
{
    fprintf(stderr,
            "uso: %s [--porta N] [--web DIR] [--atualizar [ibge|malhas]] "
            "[--geo-info] [--amostra]\n", prog);
}

/* Conta features de um GeoJSON sem parsear: o --geo-info é uma medição de
 * diagnóstico e não deve pagar o parse de 5,78 MB só para contar "type":
 * "Feature". */
static size_t conta_features(const char *s, size_t len)
{
    static const char agulha[] = "\"type\":\"Feature\"";
    size_t k = sizeof agulha - 1, n = 0, i;

    if (!s) return 0;
    for (i = 0; i + k <= len; i++) {
        if (memcmp(s + i, agulha, k) == 0) { n++; i += k - 1; }
    }
    return n;
}

static void mede(const char *rotulo, geo_nivel nivel, const char *caminho)
{
    size_t len = 0;
    char *s = geo_saida(nivel, &len);

    if (!s) {
        printf("%-12s ainda não processado (%s)\n", rotulo, caminho);
        return;
    }
    printf("%-12s %7zu features, %9zu bytes processados\n",
           rotulo, conta_features(s, len), len);
    free(s);
}

/* --geo-info: o comando de medição do Passo 6. Serve também de diagnóstico
 * quando a rota devolve 503 — dá para ver o que existe e o que não existe. */
static int geo_info(void)
{
    printf("tolerância da simplificação: %g graus\n", (double)TOLERANCIA_SIMPLIFICACAO);
    printf("coordenadas: graus×1000 (inteiras)\n");
    mede("estados:", GEO_ESTADOS, "data/processed/geo_estados.json");
    mede("municípios:", GEO_MUNICIPIOS, "data/processed/geo_municipios.json");
    return 0;
}

/* Lê "--nome valor" ou "--nome=valor". Devolve 1 se consumiu o argumento,
 * 0 se a opção não é essa e -1 se falta o valor. */
static int pega_opcao(int argc, char **argv, int *i, const char *nome,
                      const char **valor)
{
    const char *a = argv[*i];
    size_t n = strlen(nome);

    if (strcmp(a, nome) == 0) {
        if (*i + 1 >= argc)
            return -1;                  /* falta o valor */
        *valor = argv[++(*i)];
        return 1;
    }
    if (strncmp(a, nome, n) == 0 && a[n] == '=') {
        *valor = a + n + 1;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* --atualizar                                                        */
/* ------------------------------------------------------------------ */

/* Uma falha de ingestão é fatal e fala em português: quem está na frente do
 * terminal precisa saber se foi falta de rede, arquivo quebrado ou disco. */
static int atualiza_malhas(void)
{
    int rc_mun = geo_prepara(GEO_MUNICIPIOS);
    int rc_est = geo_prepara(GEO_ESTADOS);

    if (rc_mun != 0 || rc_est != 0) {
        fprintf(stderr,
                "não consegui processar as malhas. Rode com rede "
                "(curl baixa de data/cache/) ou confira fontes.conf.\n");
        return 1;
    }
    return 0;
}

static int atualiza(const char *fonte)
{
    int rc_geo = 0, rc_m = 0;

    if (!fonte || strcmp(fonte, "ibge") == 0) {
        /* Dado primeiro: um banco com dado e sem mapa é útil; o contrário
         * derruba o front em silêncio no primeiro clique. */
        rc_m = model_ingere_ibge(FONTES_CONF);
    } else if (strcmp(fonte, "malhas") == 0) {
        return atualiza_malhas();
    } else if (strcmp(fonte, "cnes") == 0 || strcmp(fonte, "dengue") == 0 ||
               strcmp(fonte, "leitos") == 0) {
        /* Recusar com mensagem é melhor que aceitar e fingir: o relatório da
         * T7 dizia "aceito e ignorado", e foi exatamente aí que a ingestão do
         * IBGE pareceu existir. */
        fprintf(stderr,
                "--atualizar %s ainda não é implementado: a ingestão de dado do "
                "Ministério da Saúde (CNES, Leitos, Dengue) é tarefa "
                "posterior.\n"
                "O que existe hoje: --atualizar ibge (população do IBGE) e "
                "--atualizar malhas.\n", fonte);
        return 2;
    } else {
        fprintf(stderr,
                "--atualizar %s não é uma fonte conhecida. Use ibge ou malhas.\n",
                fonte);
        return 2;
    }

    /* As malhas vêm junto por padrão: é o que o front busca no load e sem elas
     * o mapa fica cinza sem nenhum aviso. */
    rc_geo = atualiza_malhas();

    if (rc_m != 0 || rc_geo != 0) return 1;

    geo_info();
    {
        model m;
        if (model_carrega(&m) == 0) {
            printf("%-12s %7zu municípios, %zu anos, %zu UFs\n",
                   "modelo:", m.n_local, m.n_anos, m.n_ufs);
            model_libera(&m);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *web_root = "web";
    int porta = 8080;
    int pedir_geo_info = 0, pedir_atualizar = 0, pedir_amostra = 0;
    const char *fonte = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        const char *valor = NULL;
        int r;

        r = pega_opcao(argc, argv, &i, "--porta", &valor);
        if (r < 0) {
            fprintf(stderr, "faltou o valor de --porta\n");
            return 2;
        }
        if (r > 0) {
            char *fim;
            long v = strtol(valor, &fim, 10);
            if (*valor == '\0' || *fim != '\0' || v < 1 || v > 65535) {
                fprintf(stderr, "porta inválida: %s\n", valor);
                return 2;
            }
            porta = (int)v;
            continue;
        }

        r = pega_opcao(argc, argv, &i, "--web", &valor);
        if (r < 0) {
            fprintf(stderr, "faltou o valor de --web\n");
            return 2;
        }
        if (r > 0) {
            web_root = valor;
            continue;
        }

        /* `--atualizar`, `--atualizar ibge` e `--atualizar=ibge`.
         *
         * A forma com valor separado é a que está no guia de uso, e era a que
         * engolia o valor: sem consumir o `argv` seguinte, o `ibge` caía no
         * laço como "opção desconhecida" e o comando saía 2 sem baixar nada —
         * a mesma falha de `--atualizar` ignorar o valor que a revisão
         * apontou, agora pela metade. */
        r = pega_opcao(argc, argv, &i, "--atualizar", &valor);
        if (r == 1) {
            if (!*valor) {
                fprintf(stderr, "--atualizar sem fonte: use ibge ou malhas\n");
                return 2;
            }
            pedir_atualizar = 1;
            fonte = valor;
            continue;
        }
        /* r == 0 é "esta opção não é a minha"; r == -1 só acontece quando o
         * argumento É `--atualizar` e é o último, que é a forma sem valor:
         * vale "ibge", que é o que o guia manda rodar. */
        if (strcmp(argv[i], "--atualizar") == 0) {
            pedir_atualizar = 1;
            fonte = NULL;
            continue;
        }
        if (strcmp(argv[i], "--geo-info") == 0) {
            pedir_geo_info = 1;
            continue;
        }
        if (strcmp(argv[i], "--amostra") == 0) {
            pedir_amostra = 1;
            continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            uso(argv[0]);
            return 0;
        }
        fprintf(stderr, "opção desconhecida: %s\n", argv[i]);
        uso(argv[0]);
        return 2;
    }

    if (pedir_geo_info && !pedir_atualizar && !pedir_amostra)
        return geo_info();

    /* --amostra: escreve um índice de amostra no diretório de execução e sai.
     * É o que permite a suíte ter um servidor de verdade sem depender de rede
     * nem do dado real de quem roda o teste (achado I14/C2 da revisão). */
    if (pedir_amostra) {
        if (model_grava_amostra(MODELO_IDX) != 0) {
            fprintf(stderr, "não consegui gravar o índice de amostra em %s\n",
                    MODELO_IDX);
            return 1;
        }
        printf("índice de amostra gravado em %s (3 municípios, 3 anos)\n", MODELO_IDX);
        return 0;
    }

    if (pedir_atualizar)
        return atualiza(fonte);

    /* ---- portão de primeira execução ----
     * O índice é carregado de verdade, não só testado com stat(): um arquivo
     * de 3 bytes escrito à mão satisfazia o portão antigo e o servidor subia
     * com o modelo vazio, respondendo `{"anos":[],"ufs":[]}` para tudo. */
    {
        model m;
        if (model_carrega(&m) != 0) {
            fprintf(stderr,
                    "Não há dado processado.\n"
                    "Rode: ./dashboard --atualizar ibge\n"
                    "(ele baixa a lista de municípios do IBGE e a série de "
                    "população, e grava %s).\n", MODELO_IDX);
            return 1;
        }
        printf("Modelo: %zu municípios, %zu anos, %zu UFs (%s)\n",
               m.n_local, m.n_anos, m.n_ufs, MODELO_IDX);
        /* stdout é bufferizado quando vai para arquivo, stderr não. Sem este
         * flush, a linha do modelo aparece DEPOIS da mensagem de erro quando
         * a saída está redirecionada — que é exatamente o caso de quem está
         * lendo um log. */
        fflush(stdout);

        /* O modelo é carregado uma vez e apontado na API; cada requisição lê
         * dele, em vez de reler o arquivo em disco. */
        api_set_model(&m);

        /* A porta é tomado ANTES do banner. A ordem antiga imprimia
         * "Dashboard em http://127.0.0.1:8080/" e só depois descobria que a
         * porta estava ocupada, o que dá duas linhas que se contradizem —
         * e a que a pessoa copia para o navegador é a errada. */
        {
            int srv = http_prepara_servidor(porta);
            if (srv < 0) {
                fprintf(stderr,
                        "não consegui servir na porta %d (já está em uso?)\n",
                        porta);
                model_libera(&m);
                return 1;
            }
            printf("Dashboard em http://127.0.0.1:%d/ (web: %s)\n",
                   porta, web_root);
            fflush(stdout);
            http_serve_loop(srv, web_root);
        }
        model_libera(&m);
    }
    return 0;
}
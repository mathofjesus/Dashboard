/* main.c -- entrada do dashboard: lê --porta/--web e sobe o servidor HTTP.
 *
 * Uso: dashboard [--porta N] [--web DIR] [--atualizar] [--geo-info]
 *   --porta N     porta TCP em loopback (padrão 8080)
 *   --web DIR     diretório de arquivos estáticos (padrão "web")
 *   --atualizar   processa as malhas do IBGE e sai
 *   --geo-info    mede as malhas já processadas e sai
 *
 * O servidor só escuta em 127.0.0.1: não há autenticação, então escutar em
 * todas as interfaces exporia os dados para a rede local.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_server.h"
#include "geo.h"
#include <unistd.h>
#include <sys/stat.h>


static int modelo_ja_existe(void)
{
    struct stat st;
    return stat("data/processed/modelo.idx", &st) == 0;
}
static void uso(const char *prog)
{
    fprintf(stderr, "uso: %s [--porta N] [--web DIR] [--atualizar] [--geo-info]\n", prog);
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
 * 0 se a opção não é essa e -1 se falta o valor.
 */
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

static int geoprep_pendente = 0;

int main(int argc, char **argv)
{
    const char *web_root = "web";
    int porta = 8080;
    int pedir_geo_info = 0;
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

        if (strcmp(argv[i], "--atualizar") == 0 || strncmp(argv[i], "--atualizar=", 12) == 0) {
            geoprep_pendente = 1;
            continue;
        }
        if (strcmp(argv[i], "--geo-info") == 0) {
            pedir_geo_info = 1;
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

    if (pedir_geo_info)
        return geo_info();

    /* --atualizar processa a geometria e sai: baixar e simplificar 5,78 MB é
     * lento e não é serviço de servidor. As malhas já em cache não pedem
     * rede nenhuma. */
    if (geoprep_pendente) {
        int rc = geo_prepara(GEO_MUNICIPIOS);
        int rc2 = geo_prepara(GEO_ESTADOS);
        if (rc != 0 && rc2 != 0) {
            fprintf(stderr,
                    "não consegui processar as malhas. Rode com rede "
                    "(curl baixa de data/cache/) ou confira fontes.conf.\n");
            return 1;
        }
        return geo_info();
    }

    if (!modelo_ja_existe()) {
        fprintf(stdout, "Execute: ./dashboard --atualizar\n");
        return 1;
    }

    printf("Dashboard em http://127.0.0.1:%d/ (web: %s)\n", porta, web_root);
    fflush(stdout);

    if (http_listen_and_serve(porta, web_root) != 0) {
        fprintf(stderr, "não consegui servir na porta %d\n", porta);
        return 1;
    }
    return 0;
}
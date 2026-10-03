/* main.c -- entrada do dashboard: lê --porta/--web e sobe o servidor HTTP.
 *
 * Uso: dashboard [--porta N] [--web DIR]
 *   --porta N   porta TCP em loopback (padrão 8080)
 *   --web DIR   diretório de arquivos estáticos (padrão "web")
 *
 * O servidor só escuta em 127.0.0.1: não há autenticação, então escutar em
 * todas as interfaces exporia os dados para a rede local.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_server.h"

static void uso(const char *prog)
{
    fprintf(stderr, "uso: %s [--porta N] [--web DIR]\n", prog);
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

int main(int argc, char **argv)
{
    const char *web_root = "web";
    int porta = 8080;
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

        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            uso(argv[0]);
            return 0;
        }
        fprintf(stderr, "opção desconhecida: %s\n", argv[i]);
        uso(argv[0]);
        return 2;
    }

    printf("Dashboard em http://127.0.0.1:%d/ (web: %s)\n", porta, web_root);
    fflush(stdout);

    if (http_listen_and_serve(porta, web_root) != 0) {
        fprintf(stderr, "não consegui servir na porta %d\n", porta);
        return 1;
    }
    return 0;
}
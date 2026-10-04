/* tests/test_main.c -- runner de testes do dashboard.
 *
 * Uma função por suíte. Para adicionar uma suíte nova: declare o protótipo
 * abaixo, chame a função em main() e acrescente o .c da suíte em TEST_SRC no
 * Makefile.
 *
 * A suíte test_http_client precisa de internet (baixa a API do IBGE) e por
 * isso só roda quando o binário recebe --rede, que é o alvo `make test-rede`.
 * Sem o argumento ela é pulada, para o `make test` padrão não depender de
 * rede.
 *
 * Não usamos assert() da biblioteca padrão porque ele some quando o código é
 * compilado com NDEBUG e o teste passaria sem verificar nada.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

void test_http(void);
void test_json(void);
void test_cache(void);
void test_charset(void);
void test_zip(void);
void test_csv(void);
void test_http_client(void);
void test_fontes(void);
void test_modelo(void);
void test_model(void);
void test_aggregate(void);
void test_api(void);
void test_geo(void);

static void cabecalho(const char *nome)
{
    printf("--- %s\n", nome);
}

int main(int argc, char **argv)
{
    int com_rede = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rede") == 0)
            com_rede = 1;
        else {
            fprintf(stderr, "argumento desconhecido: %s\n", argv[i]);
            return 2;
        }
    }

    cabecalho("http");
    test_http();

    cabecalho("json");
    test_json();

    cabecalho("cache");
    test_cache();

    cabecalho("charset");
    test_charset();

    cabecalho("zip");
    test_zip();

    cabecalho("csv");
    test_csv();

    cabecalho("fontes");
    test_fontes();

    cabecalho("modelo");
    test_modelo();

    cabecalho("model");
    test_model();

    cabecalho("aggregate");
    test_aggregate();

    cabecalho("api");
    test_api();

    cabecalho("geo");
    test_geo();

    if (com_rede) {
        cabecalho("http_client (precisa de internet)");
        test_http_client();
    } else {
        printf("--- http_client: pulado (rode com ./tests/test_bin --rede)\n");
    }

    printf("OK: todos os testes passaram\n");
    return 0;
}

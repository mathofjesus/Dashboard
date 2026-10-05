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
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* As suítes que tocam o disco escrevem as fixtures em /tmp/opencode — o mesmo
 * prefixo que a Tarefa 2 pediu, e o mesmo que o achado M6 da revisão apontou.
 * O problema não é o caminho: é que ninguém o criava. Numa máquina limpa, ou
 * depois de um tmpfiles que limpa /tmp, a suíte morria em
 * `não consegui abrir o arquivo de fixture` (tests/test_http.c) — e morria
 * depois de já ter executado metade dos testes, o que sugere defeito no
 * produto quando é só diretório faltando.
 *
 * Criar o diretório é papel do runner, e é aqui: as suítes são função de
 * `void f(void)` e não recebem contexto, então o lugar único onde isto pode
 * ser garantido é antes da primeira delas. O mkdir é recursivo para não
 * depender de o diretório pai já existir.
 */
#define DIR_FIXTURES "/tmp/opencode"

static int cria_dir_recursivo(const char *path)
{
    char buf[1024];
    size_t i, n;

    n = strlen(path);
    if (n == 0 || n >= sizeof buf)
        return -1;
    memcpy(buf, path, n + 1);

    for (i = 1; i < n; i++) {
        if (buf[i] != '/')
            continue;
        buf[i] = '\0';
        if (mkdir(buf, 0777) != 0 && errno != EEXIST)
            return -1;
        buf[i] = '/';
    }
    if (mkdir(buf, 0777) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static void prepara_dir_fixtures(void)
{
    if (cria_dir_recursivo(DIR_FIXTURES) != 0) {
        fprintf(stderr,
                "FALHOU: não consegui criar %s para as fixtures (%s).\n"
                "As suítes de cache, fontes, geo, http e http_client escrevem\n"
                "as fixtures nesse diretório. Crie-o, aponte TMPDIR para um\n"
                "diretório gravável, ou rode em um ambiente com /tmp gravável.\n",
                DIR_FIXTURES, strerror(errno));
        exit(1);
    }
}

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
void test_model(void);
void test_aggregate(void);
void test_api(void);
void test_geo(void);
void test_saude(void);

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
    prepara_dir_fixtures();
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

    cabecalho("model");
    test_model();

    cabecalho("aggregate");
    test_aggregate();

    cabecalho("api");
    test_api();

    cabecalho("geo");
    test_geo();

    cabecalho("saude");
    test_saude();

    if (com_rede) {
        cabecalho("http_client (precisa de internet)");
        test_http_client();
    } else {
        printf("--- http_client: pulado (rode com ./tests/test_bin --rede)\n");
    }

    printf("OK: todos os testes passaram\n");
    return 0;
}

/* tests/test_main.c -- runner de testes do dashboard.
 *
 * Uma função por suíte. Para adicionar uma suíte nova (por exemplo
 * test_http_client na Tarefa 2): declare o protótipo abaixo, chame a função
 * em main() e acrescente o .c da suíte em TEST_SRC no Makefile.
 *
 * Não usamos assert() da biblioteca padrão porque ele some quando o código é
 * compilado com NDEBUG e o teste passaria sem verificar nada.
 */
#include <stdio.h>
#include <stdlib.h>

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

void test_http(void);

static void cabecalho(const char *nome)
{
    printf("--- %s\n", nome);
}

int main(void)
{
    cabecalho("http");
    test_http();

    printf("OK: todos os testes passaram\n");
    return 0;
}
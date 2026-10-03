/* tests/test_main.c -- runner de testes. Uma funcao por suite; para adicionar
 * uma suite nova: declare o prototipo aqui, chame-a em main() e acrescente o
 * .c da suite em TEST_SRC no Makefile.
 *
 * Nao usamos assert() da biblioteca padrao porque ele some com NDEBUG.
 */
#include <stdio.h>
#include <stdlib.h>

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

void test_http(void);

static void banner(const char *nome)
{
    printf("--- %s\n", nome);
}

int main(void)
{
    banner("http");
    test_http();

    printf("OK: todos os testes passaram\n");
    return 0;
}
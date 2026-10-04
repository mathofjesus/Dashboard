#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/fontes.h"
#include "../src/modelo.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

void test_modelo(void)
{
    fonte_dados *fontes = NULL;
    size_t n = 0;
    int r;

    r = fontes_ler("fontes.conf", &fontes, &n);
    T_ASSERT(r == 0, "fontes_ler deve retornar 0");

    /* modelo_inicializa só carrega configuração (não faz ingestão) */
    r = modelo_inicializa("fontes.conf", fontes, n);
    T_ASSERT(r == 0, "modelo_inicializa deve retornar 0");
    T_ASSERT(modelo_tem_dados() == 0, "modelo_tem_dados deve ser 0 (não faz ingestão)");

    modelo_finaliza();
    fontes_libera(fontes, n);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/fontes.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

void test_fontes(void)
{
    fonte_dados *fontes = NULL;
    size_t n = 0;
    int r;
    fonte_dados *selecionada = NULL;

    /* fontes_carrega("fontes.conf") → 5 entradas ou mais */
    r = fontes_ler("fontes.conf", &fontes, &n);
    T_ASSERT(r == 0, "fontes_ler deve retornar 0");
    T_ASSERT(n >= 5, "deve haver ao menos 5 fontes");
    T_ASSERT(fontes != NULL, "fontes não deve ser NULL");

    /* fontes_obtem/função equivalente via fontes_garente para garantir seleção? */
    /* O contrato diz: fontes_garente(fonte_dados *fontes, size_t n, fonte_dados **selecionada, const char *id) */
    r = fontes_garente(fontes, n, &selecionada, "cnes");
    T_ASSERT(r == 0, "fontes_garente(cnes) deve retornar 0");
    T_ASSERT(selecionada != NULL, "selecionada não deve ser NULL");

    /* Chave inexistente → != 0 */
    r = fontes_garente(fontes, n, &selecionada, "chave_inexistente");
    T_ASSERT(r != 0, "chave inexistente deve falhar");

    fontes_libera(fontes, n);
    fontes = NULL;
    n = 0;

    /* Arquivo inexistente → retorno != 0 */
    r = fontes_ler("arquivo_inexistente.conf", &fontes, &n);
    T_ASSERT(r != 0, "arquivo inexistente deve falhar");

    /* Linha sem = é ignorada sem crash - testamos carregando novamente */
    r = fontes_ler("fontes.conf", &fontes, &n);
    T_ASSERT(r == 0, "recarregar deve funcionar");
    fontes_libera(fontes, n);
}

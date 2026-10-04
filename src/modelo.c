#include <stdio.h>
#include "modelo.h"

int modelo_inicializa(const char *path_cfg, const fonte_dados *fontes, size_t nfontes)
{
    (void)path_cfg;
    (void)fontes;
    (void)nfontes;
    return 0;
}

int modelo_tem_dados(void)
{
    return 0;
}

void modelo_finaliza(void)
{
    /* nada a fazer */
}

#ifndef MODELO_H
#define MODELO_H

#include <stddef.h>
#include "fontes.h"

int modelo_inicializa(const char *path_cfg, const fonte_dados *fontes, size_t nfontes);
int modelo_tem_dados(void);
void modelo_finaliza(void);

#endif /* MODELO_H */

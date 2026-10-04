#ifndef FONTES_H
#define FONTES_H

#include <stddef.h>

typedef struct {
    char *id;
    char *nome;
    char *tipo;
    char *url_zip;
    char *url_cnes;
    char *url_leitos;
    char *url_dengue;
    int tem_utf8;
    char **colunas;
    size_t ncolunas;
    char *ibge_tid;
    char *cod_ibge;
    char *uf;
} fonte_dados;

int fontes_ler(const char *path, fonte_dados **fontes, size_t *n);
int fontes_garente(fonte_dados *fontes, size_t n, fonte_dados **selecionada, const char *id);
void fontes_libera(fonte_dados *fontes, size_t n);
int modelo_inicializa(const char *path_cfg, const fonte_dados *fontes, size_t nfontes);
int modelo_tem_dados(void);
void modelo_finaliza(void);

#endif /* FONTES_H */

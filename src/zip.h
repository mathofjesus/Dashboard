#ifndef ZIP_H
#define ZIP_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Estrutura para entrada de arquivo ZIP */
typedef struct {
    char  *nome;
    size_t tam_comprimido;
    size_t tam_descomprimido;
    long   offset_local;
    unsigned metodo;     /* 8 = deflate, 0 = stored */
} zip_entrada;

typedef struct {
    zip_entrada *entradas;
    size_t       n;
    FILE        *fp;
} zip_arq;

int  zip_abre(const char *caminho, zip_arq *z);
void zip_fecha(zip_arq *z);
int  zip_lista(const char *caminho, zip_entrada **saida, size_t *n);
char *zip_extrai(const zip_arq *z, size_t idx, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ZIP_H */

#ifndef CSV_H
#define CSV_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CSV_ISO88591, CSV_UTF8 } csv_codificacao;

typedef struct csv csv;

/* Detecção de delimitador: retorna ';' ou ',' com base na primeira linha fora de aspas */
int   csv_detecta_delimitador(const char *buf, size_t len);

csv  *csv_abre_mem(const char *buf, size_t len, char delimitador, csv_codificacao cod);
void  csv_fecha(csv *c);
size_t csv_num_linhas(const csv *c);
const char *csv_cabecalho(const csv *c, size_t idx_coluna);
int   csv_indice_coluna(const csv *c, const char *nome);
int   csv_campo(const csv *c, size_t linha, size_t coluna, char *out, size_t out_len);      /* 0 ok, -1 fora */
long long csv_int(const csv *c, size_t linha, size_t coluna, long long padrao);
double csv_double(const csv *c, size_t linha, size_t coluna, double padrao);
int   csv_vazio(const csv *c, size_t linha, size_t coluna);
char *csv_linha_json(const csv *c, size_t linha, const size_t *cols, size_t ncols, size_t chave_json);   /* chave_json = índice da coluna de código */

#ifdef __cplusplus
}
#endif

#endif /* CSV_H */

#ifndef AGGREGATE_H
#define AGGREGATE_H

#include <stddef.h>
#include "model.h"

/* KPIs sobre o conjunto TODO que satisfaz o filtro, antes do limite: o cartão
 * de "Total de municípios" tem que dizer quantos são, não quantas linhas a
 * tabela mostra. */
typedef struct {
    int min;
    int max;
    int media;
    long long soma;
    /* Localidades com valor no ano. Um ausente (-1) não conta: nem para o
     * total, nem para o mínimo, nem para a média. */
    int com_dado;
    /* Localidades que satisfazem o filtro, com ou sem valor no ano, ANTES do
     * limite. É o que o cartão de "Total de municípios" mostra: quantos são,
     * não quantas linhas a tabela está exibindo. */
    int linhas;
} kpis;

/* A tabela da resposta: linhas paralelas de largura fixa. `ordem` é o índice
 * de chegada, que sobrevive à ordenação para desempatá-la. */
typedef struct {
    size_t n;
    char (*cod)[8];
    char (*nome)[96];
    int *valor;
    size_t *ordem;
} consulta;

/* Tabela filtrada e ordenada.
 *
 * `nivel_uf` != 0 agrega por UF: a linha é a própria UF e o valor é a soma dos
 * municípios dela no ano. Nesse modo `uf` é um recorte de verdade (`uf=RO`
 * devolve a linha "Rondônia" e só ela); no modo município, `uf` filtra as
 * linhas. `limite` corta só o que é devolvido, nunca os KPIs. */
int aggregate_consulta(const model *m, indicador ind, int nivel_uf,
                       const char *uf, int ano, const char *busca,
                       const char *ordenar, int limite,
                       consulta *out, kpis *kpis_out, char *erro, size_t erro_len);

void aggregate_libera(consulta *c);

/* Série histórica de um município, malloc'd: "[1000,null,1200]". Devolve NULL
 * se o código não existir ou se a série estiver vazia. */
char *aggregate_serie(const model *m, indicador ind, const char *cod,
                      size_t *n);

/* A mesma série somada por UF — é o que `nivel=uf` mostra no gráfico de
 * linha. `uf` aceita sigla ("ro", "RO") ou código do IBGE ("11"). */
char *aggregate_serie_uf(const model *m, indicador ind, const char *uf,
                         size_t *n);

/* Anos e UFs realmente disponíveis (spec §8). malloc'd. */
char *aggregate_opcoes(const model *m, indicador ind);

#endif /* AGGREGATE_H */
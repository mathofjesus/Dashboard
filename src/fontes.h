/* fontes.h -- fontes de dados externas declaradas em fontes.conf.
 *
 * O arquivo é a lista do que se baixa e de onde. As URLs do Ministério da
 * Saúde embutem o ano (Leitos_csv_2026, DENGBR26) e por isso vivem aqui, e
 * nunca fixas na fonte: mudar de ano de publicação é editar o .conf, não
 * recompilar.
 *
 * Contrato de memória:
 *   - fontes_ler devolve um vetor malloc'd; fontes_libera devolve tudo.
 *   - campos char* dentro de fonte_dados são malloc'd e pertencem ao vetor.
 *   - fontes_garente devolve o corpo malloc'd; quem chama libera com free().
 */
#ifndef FONTES_H
#define FONTES_H

#include <stddef.h>

/* Onde ficam as URLs. Relativo ao diretório de execução, como o resto de
 * data/ — o binário é rodado da raiz do projeto. */
#define FONTES_CONF "fontes.conf"

typedef struct {
    char *id;          /* chave da seção, sem os colchetes            */
    char *nome;        /* rótulo, opcional                           */
    char *tipo;        /* "ibge", "geometria", "csv", opcional        */
    char *url_zip;     /* url principal (o `url` do .conf)           */
    char *url_cnes;
    char *url_leitos;
    char *url_dengue;
    char *dataset;     /* nome do arquivo dentro do ZIP, ou "-"       */
    int ttl;           /* prazo em segundos; CACHE_TTL_NENHUM = nunca  */
    int tem_utf8;      /* 1 = o CSV já vem em UTF-8 (Dengue)          */
    char **colunas;
    size_t ncolunas;
    char *ibge_tid;
    char *cod_ibge;
    char *uf;
} fonte_dados;

/* Lê o .conf. Devolve 0 e preenche os parâmetros de saída, ou != 0. */
int fontes_ler(const char *path, fonte_dados **fontes, size_t *n);

/* Libera o vetor devolvido por fontes_ler. */
void fontes_libera(fonte_dados *fontes, size_t n);

/* Procura a chave e devolve o ponteiro para dentro do vetor, ou NULL.
 * Não baixa nada, não toca no cache: é só o lookup. */
const fonte_dados *fontes_obtem(const fonte_dados *fontes, size_t n,
                                const char *id);

/* Passo 7 do brief — **garante o download**, que é o que o nome promete:
 *
 *   1. se a chave está no cache de data/cache/ e o TTL dela não expirou,
 *      devolve o que está lá, sem rede;
 *   2. senão lê o .conf, baixa a url com http_get, grava no cache e arma o
 *      TTL a partir do `ttl` da seção;
 *   3. devolve o corpo malloc'd em *out com o tamanho em *out_len.
 *
 * Chave inexistente no .conf devolve != 0. Falha de rede devolve o código do
 * curl (maior que zero) ou -1, para o chamador diferenciar "não tem essa
 * fonte" de "não tem rede".
 */
int fontes_garente(const char *arquivo_conf, const char *id,
                   char **out, size_t *out_len);

/* O mesmo caminho, com os dois ajustes que a ingestão precisa e o
 * `fontes_garente` simples não tem:
 *
 *   - `sufixo` é concatenado à url da seção. A url do SIDRA é um *prefixo*
 *     (".../v/9324/p") e o ano vem depois, porque a API aceita uma lista de
 *     períodos por requisição e não aceita a série inteira.
 *   - `chave_cache` é a chave sob a qual o corpo é guardado. Sem ela a chave
 *     seria o id, e as três requisições da série escreveriam todas no mesmo
 *     arquivo, com a última sobrescrevendo as duas primeiras.
 *
 * `sufixo` e `chave_cache` NULL significam "nada a acrescentar" e "usar o id".
 */
int fontes_baixa(const char *arquivo_conf, const char *id, const char *sufixo,
                 const char *chave_cache, char **out, size_t *out_len);

#endif /* FONTES_H */

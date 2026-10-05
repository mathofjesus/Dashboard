/* model.h -- o modelo em memória: localidades, anos e valores por indicador.
 *
 * O que existe aqui
 * -----------------
 * O modelo é o índice do dashboard: para cada município, cada ano e cada
 * indicador, um inteiro. É o que `aggregate.c` consulta e o que `api.c`
 * serializa. Nada mais é dado — a geometria é de `geo.h`, e os downloads
 * crus são de `cache.h`.
 *
 * Layout dos valores (e por que ele é alocado)
 * -------------------------------------------
 * Os valores ficam em UM array 1-D alocado, com passo `n_anos` por município:
 *
 *     valores[ind * (n_local * n_anos) + i * n_anos + k]
 *
 * A versão anterior declarava `int valores[IND_TOTAL][5570]` e indexava por
 * `i * n_anos + k`. Com 5570 municípios e 22 anos o índice chega a 122 539
 * numa linha de 5570 ints: é leitura 20 bytes depois do fim da linha, e
 * alguns milhares depois do fim do `model` inteiro. AddressSanitizer confirmou
 * (achado C4 da revisão). A matriz fixa não comporta `n_local * n_anos`, e
 * `n_anos` não é constante: é a lista de anos que a série do SIDRA devolveu.
 *
 * Ordem dentro do array: indicador por fora, depois município, depois ano.
 * Uma consulta varre **um ano inteiro** de todos os municípios, que é o caso
 * comum (`/api/consulta?ano=…`), então os valores de um ano ficam contíguos
 * por município.
 *
 * Ausência é -1, não 0
 * --------------------
 * A série histórica tem lacunas: a tabela 6579 não tem 2007, 2022 nem 2023, e
 * um município pode não existir ainda no primeiro ano da série. `""`, `".."`
 * e `"..."` no campo `V` do SIDRA viram MODELO_AUSENTE, nunca 0 — 0 é uma
 * população possível e fingir que vale 0 contamina mínimo, máximo e média.
 *
 * Contrato de memória
 * -------------------
 *   - `model_carrega` preenche um `model` malloc'd por quem chama; o conteúdo
 *     (local, anos, valores, ufs) é alocado e pertence a ele.
 *   - `model_libera` devolve tudo e zera os ponteiros.
 *   - `model_grava`/`model_carrega_de` falam com data/processed/modelo.idx.
 */
#ifndef MODEL_H
#define MODEL_H

#include <stddef.h>

/* Onde fica o índice binário. Relativo ao diretório de execução, como o resto
 * de data/ — o binário é rodado da raiz do projeto. É este arquivo que o
 * portão de primeira execução exige: sem ele nada foi ingerido, e aceitar a
 * sua presença seria aceitar um arquivo de 3 bytes que não tem dado nenhum. */
#define MODEL_DIR "data/processed"
#define MODELO_IDX "data/processed/modelo.idx"

/* Cabeçalho do arquivo binário. Mudou, muda junto — o leitor recusa a versão
 * antiga em vez de interpretar os bytes errado. */
#define MODELO_MAGIC "DMODIDX1\n"
#define MODELO_MAGIC_LEN 9
#define MODELO_VERSAO 1u

/* Valor de "não existe dado para esta combinação". */
#define MODELO_AUSENTE (-1)

typedef struct {
    char cod[8];        /* código de município do IBGE, 7 dígitos, com NUL */
    char nome[96];      /* nome sem acento antigo: o IBGE já devolve UTF-8 */
    char uf[3];         /* sigla da UF: "RO"                            */
    char regiao[24];    /* "Norte", "Sudeste", …                        */
} locality;

typedef struct {
    char sigla[3];      /* "RO"                                          */
    char nome[64];      /* "Rondônia"                                    */
    char regiao[24];    /* "Norte"                                       */
} uf_info;

typedef enum {
    IND_POPULACAO,
    IND_ESTABELECIMENTOS,
    IND_LEITOS,
    IND_UTI,
    IND_DENGUE,
    IND_TOTAL
} indicador;

typedef struct {
    size_t n_local;
    locality *local;
    size_t n_anos;
    int *anos;            /* n_anos, ordem crescente                    */
    int *valores;         /* IND_TOTAL * n_local * n_anos, ver topo      */
    size_t n_ufs;
    uf_info *ufs;         /* n_ufs, em ordem de código do IBGE          */
} model;

/* Lê MODELO_IDX. Devolve 0 e preenche *m, ou != 0 (com *m zerado) quando não
 * há índice, ele está truncado, ou a magic não bate. */
int model_carrega(model *m);

/* Igual a model_carrega, mas de um caminho explícito — é o que o teste usa
 * para não depender do diretório de execução. */
int model_carrega_de(model *m, const char *caminho);

/* Grava o índice em `caminho`, por rename: um servidor já no ar nunca lê um
 * arquivo pela metade. */
int model_grava(const model *m, const char *caminho);

/* Ingestão real do IBGE: Localidades (a lista de municípios, a UF e a região)
 * mais a série de população da tabela 6579 do SIDRA. Escreve MODELO_IDX no
 * fim e devolve 0, ou != 0 com mensagem já impressa em português.
 *
 * Os outros indicadores ficam MODELO_AUSENTE: CNES, Leitos e Dengue são
 * tarefas posteriores e nenhum dado deles é inventado aqui. */
int model_ingere_ibge(const char *conf);

/* Modelo de amostra determinístico (3 municípios, 3 anos), para teste e
 * diagnóstico. Não toca na rede nem no dado real; devolve 0. */
int model_grava_amostra(const char *caminho);

/* Valor de (indicador, município, ano), ou MODELO_AUSENTE se qualquer um dos
 * três estiver fora do modelo. É o único acesso a `valores`. */
int model_valor(const model *m, indicador ind, size_t i_local, int ano);

/* Índice do ano dentro de `anos`, ou `m->n_anos` se o ano não está na série. */
size_t model_indice_ano(const model *m, int ano);

/* Índice do município de código `cod`, ou `m->n_local` se não houver. */
size_t model_local_de_cod(const model *m, const char *cod);

/* Índice da UF de sigla `uf`, ou `m->n_ufs` se não houver. Aceita a sigla em
 * qualquer caixa ("ro", "RO") e o código de dois dígitos do IBGE ("11"). */
size_t model_uf_de_sigla(const model *m, const char *uf);

/* Libera tudo e zera os campos. model_libera(NULL) não faz nada. */
void model_libera(model *m);

/* Nome do indicador como vai na query e na API. */
const char *model_nome_indicador(indicador ind);

#endif /* MODEL_H */
/* src/model.c -- o modelo em memória e a ingestão que o produz (ver model.h).
 *
 * Este arquivo tem duas metades que se explicam uma pela outra:
 *
 *   1. A INGESTÃO (model_ingere_ibge), que é rede: as localidades do IBGE e a
 *      série de população da tabela 6579 do SIDRA, em lotes de anos porque a
 *      API recusa a série inteira. Ela monta o `model` e grava o índice.
 *
 *   2. O LEITOR/GRAVADOR do índice binário (model_carrega, model_grava), que é
 *      disco: o servidor carrega uma vez e não rebaixa nada por requisição.
 *
 * Nenhuma das duas inventa dado. Se o IBGE responder diferente do que o
 * formato diz, a ingestão falha com mensagem — ela não preenche com zero nem
 * com `-1` o que não leu, porque `-1` é "não existe" e 0 é um valor.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>

#include "model.h"
#include "fontes.h"
#include "json.h"

/* ------------------------------------------------------------------ */
/* leitura de inteiros do texto                                        */
/* ------------------------------------------------------------------ */

/* Quantos anos o SIDRA aceita numa requisição. Medido: a série inteira (22
 * períodos) responde 400, e 3 anos em 5570 municípios são 4,5 MB e ~65 s —
 * dentro do teto de 120 s do http_client (HTTP_CLIENT_MAX_TIME_S). Cinco anos
 * estourariam esse teto. */
#define ANOS_POR_LOTE 3

/* ------------------------------------------------------------------ */
/* acesso                                                              */
/* ------------------------------------------------------------------ */

size_t model_indice_ano(const model *m, int ano)
{
    size_t i;
    if (!m || !m->anos) return 0;
    for (i = 0; i < m->n_anos; i++) {
        if (m->anos[i] == ano) return i;
    }
    return m->n_anos;
}

size_t model_local_de_cod(const model *m, const char *cod)
{
    size_t i;
    if (!m || !m->local || !cod || !*cod) return 0;
    for (i = 0; i < m->n_local; i++) {
        if (strcmp(m->local[i].cod, cod) == 0) return i;
    }
    return m->n_local;
}

/* Os dois primeiros dígitos do código do município são o código da UF: 11 é
 * Rondônia, 41 é Paraná. É essa a identidade que `geo_codigo_uf` já usava,
 * e é por isso que `uf=11` e `uf=RO` precisam dar a mesma coisa. */
static int eh_codigo_uf(const char *s)
{
    return strlen(s) == 2 &&
           s[0] >= '0' && s[0] <= '9' &&
           s[1] >= '0' && s[1] <= '9';
}

size_t model_uf_de_sigla(const model *m, const char *uf)
{
    size_t i;
    int codigo;

    if (!m) return 0;
    if (!m->ufs) return 0;
    /* Uma UF tem dois caracteres. Uma letra só não é uma UF, e devolver 0
     * (que é um índice válido) faria `uf=R` casar com a primeira da tabela. */
    if (!uf || strlen(uf) < 2) return m->n_ufs;
    codigo = eh_codigo_uf(uf);

    for (i = 0; i < m->n_ufs; i++) {
        if (codigo) {
            /* Casa pelo prefixo do código de um município daquela UF, e não
             * pela sigla: as siglas são letras, e comparar "11" com "RO"
             * byte a byte nunca casa. */
            size_t j;
            for (j = 0; j < m->n_local; j++) {
                if (strcmp(m->local[j].uf, m->ufs[i].sigla) != 0) continue;
                if (m->local[j].cod[0] == uf[0] && m->local[j].cod[1] == uf[1])
                    return i;
                break;   /* o primeiro município da UF já diz a identidade */
            }
            continue;
        }
        if (m->ufs[i].sigla[0] == (char)toupper((unsigned char)uf[0]) &&
            m->ufs[i].sigla[1] == (char)toupper((unsigned char)uf[1]))
            return i;
    }
    return m->n_ufs;
}

int model_valor(const model *m, indicador ind, size_t i_local, int ano)
{
    size_t passo, base, k;

    if (!m || !m->valores) return MODELO_AUSENTE;
    if (ind < 0 || ind >= IND_TOTAL) return MODELO_AUSENTE;
    if (i_local >= m->n_local) return MODELO_AUSENTE;
    if (m->n_anos == 0) return MODELO_AUSENTE;
    k = model_indice_ano(m, ano);
    if (k >= m->n_anos) return MODELO_AUSENTE;
    passo = m->n_local * m->n_anos;
    base = (size_t)ind * passo + i_local * m->n_anos + k;
    return m->valores[base];
}

/* ------------------------------------------------------------------ */
/* alocação e liberação                                               */
/* ------------------------------------------------------------------ */

void model_libera(model *m)
{
    if (!m) return;
    free(m->local);
    free(m->anos);
    free(m->valores);
    free(m->ufs);
    m->local = NULL;
    m->anos = NULL;
    m->valores = NULL;
    m->ufs = NULL;
    m->n_local = 0;
    m->n_anos = 0;
    m->n_ufs = 0;
}

/* Reserva o array de valores inteiro, todo em MODELO_AUSENTE. Devolve 0 ou
 * -1. O sentinela de arranque importa: sem ele, malloc devolve lixo e uma
 * leitura de um par (município, ano) que o SIDRA não cobriu sairia como
 * número qualquer em vez de "não existe". */
static int model_reserva_valores(model *m)
{
    size_t passo, total;

    if (m->n_local == 0 || m->n_anos == 0) return -1;
    if (m->n_local > (size_t)-1 / m->n_anos) return -1;
    passo = m->n_local * m->n_anos;
    if (passo > (size_t)-1 / (size_t)IND_TOTAL) return -1;
    total = passo * (size_t)IND_TOTAL;
    m->valores = malloc(total * sizeof *m->valores);
    if (!m->valores) return -1;
    for (size_t i = 0; i < total; i++) m->valores[i] = MODELO_AUSENTE;
    return 0;
}

/* ------------------------------------------------------------------ */
/* gravação binária                                                    */
/* ------------------------------------------------------------------ */

/* Escrita com verificação: o arquivo final só existe inteiro. O mesmo motivo
 * de geo.c:grava_processado — um servidor já no ar não pode ver um índice pela
 * metade e servir dado misturado com lixo. */

static int w_bytes(FILE *f, const void *p, size_t n)
{
    return (n == 0 || fwrite(p, 1, n, f) == n) ? 0 : -1;
}

static int w_u32(FILE *f, unsigned v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFFu);
    b[1] = (unsigned char)((v >> 8) & 0xFFu);
    b[2] = (unsigned char)((v >> 16) & 0xFFu);
    b[3] = (unsigned char)((v >> 24) & 0xFFu);
    return w_bytes(f, b, sizeof b);
}

static int w_i32(FILE *f, int v)
{
    return w_u32(f, (unsigned)v);
}

/* Campos de comprimento fixo são escritos byte a byte, nunca como memcpy da
 * struct: `locality` tem 131 bytes mas a struct C pode ter padding, e o
 * formato binário não pode depender do compilador que o leu. */
static int w_txt(FILE *f, const char *s, size_t campo)
{
    char buf[96];
    size_t n;

    if (campo > sizeof buf) return -1;
    memset(buf, 0, campo);
    if (s) {
        n = strlen(s);
        if (n >= campo) n = campo - 1;
        memcpy(buf, s, n);
    }
    return w_bytes(f, buf, campo);
}

int model_grava(const model *m, const char *caminho)
{
    char tmp[512];
    FILE *f;
    size_t i;
    int rc = -1;

    if (!m || !caminho || !*caminho) return -1;
    if (m->n_local == 0 || m->n_anos == 0 || !m->local || !m->anos || !m->valores)
        return -1;
    if ((size_t)snprintf(tmp, sizeof tmp, "%s.tmp", caminho) >= sizeof tmp)
        return -1;

    /* data/processed/ pode não existir ainda: é a primeira escrita da árvore
     * limpa. Um nível só, que é o que o caminho tem. */
    {
        char dir[512];
        char *slash;
        if ((size_t)snprintf(dir, sizeof dir, "%s", caminho) >= sizeof dir) return -1;
        slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            if (dir[0]) mkdir(dir, 0777);
        }
    }

    f = fopen(tmp, "wb");
    if (!f) return -1;

    if (w_bytes(f, MODELO_MAGIC, MODELO_MAGIC_LEN) != 0) goto fim;
    if (w_u32(f, MODELO_VERSAO) != 0) goto fim;
    if (w_u32(f, (unsigned)m->n_anos) != 0) goto fim;
    if (w_u32(f, (unsigned)m->n_local) != 0) goto fim;
    if (w_u32(f, (unsigned)m->n_ufs) != 0) goto fim;
    if (w_u32(f, 0u) != 0) goto fim;

    for (i = 0; i < m->n_anos; i++)
        if (w_i32(f, m->anos[i]) != 0) goto fim;

    for (i = 0; i < m->n_ufs; i++) {
        if (!m->ufs) goto fim;
        if (w_txt(f, m->ufs[i].sigla, sizeof m->ufs[i].sigla) != 0) goto fim;
        if (w_txt(f, m->ufs[i].nome, sizeof m->ufs[i].nome) != 0) goto fim;
        if (w_txt(f, m->ufs[i].regiao, sizeof m->ufs[i].regiao) != 0) goto fim;
    }

    for (i = 0; i < m->n_local; i++) {
        if (w_txt(f, m->local[i].cod, sizeof m->local[i].cod) != 0) goto fim;
        if (w_txt(f, m->local[i].nome, sizeof m->local[i].nome) != 0) goto fim;
        if (w_txt(f, m->local[i].uf, sizeof m->local[i].uf) != 0) goto fim;
        if (w_txt(f, m->local[i].regiao, sizeof m->local[i].regiao) != 0) goto fim;
    }

    for (i = 0; i < (size_t)IND_TOTAL * m->n_local * m->n_anos; i++)
        if (w_i32(f, m->valores[i]) != 0) goto fim;

    if (fflush(f) != 0) goto fim;
    if (fclose(f) != 0) { f = NULL; goto fim; }
    f = NULL;
    if (rename(tmp, caminho) != 0) goto fim;
    rc = 0;

fim:
    if (f) fclose(f);
    if (rc != 0) remove(tmp);
    return rc;
}

/* ------------------------------------------------------------------ */
/* leitura binária                                                     */
/* ------------------------------------------------------------------ */

static int r_bytes(FILE *f, void *p, size_t n)
{
    return (n == 0 || fread(p, 1, n, f) == n) ? 0 : -1;
}

static int r_u32(FILE *f, unsigned *v)
{
    unsigned char b[4];
    if (r_bytes(f, b, sizeof b) != 0) return -1;
    *v = (unsigned)b[0] | ((unsigned)b[1] << 8) |
         ((unsigned)b[2] << 16) | ((unsigned)b[3] << 24);
    return 0;
}

static int r_i32(FILE *f, int *v)
{
    unsigned u;
    if (r_u32(f, &u) != 0) return -1;
    *v = (int)u;
    return 0;
}

static int r_txt(FILE *f, char *campo, size_t n)
{
    if (r_bytes(f, campo, n) != 0) return -1;
    campo[n - 1] = '\0';
    return 0;
}

int model_carrega_de(model *m, const char *caminho)
{
    char magic[MODELO_MAGIC_LEN];
    FILE *f = NULL;
    unsigned versao, n_anos, n_local, n_ufs, reserva;
    size_t i, total;
    int rc = -1;

    if (!m || !caminho) return -1;
    memset(m, 0, sizeof *m);

    f = fopen(caminho, "rb");
    if (!f) return -1;

    if (r_bytes(f, magic, sizeof magic) != 0 ||
        memcmp(magic, MODELO_MAGIC, MODELO_MAGIC_LEN) != 0) goto fim;
    if (r_u32(f, &versao) != 0 || versao != MODELO_VERSAO) goto fim;
    if (r_u32(f, &n_anos) != 0) goto fim;
    if (r_u32(f, &n_local) != 0) goto fim;
    if (r_u32(f, &n_ufs) != 0) goto fim;
    if (r_u32(f, &reserva) != 0) goto fim;
    (void)reserva;

    if (n_anos == 0 || n_local == 0 || n_anos > 100000u || n_local > 1000000u)
        goto fim;

    m->n_anos = n_anos;
    m->n_local = n_local;
    m->n_ufs = n_ufs;

    m->anos = malloc(m->n_anos * sizeof *m->anos);
    m->local = malloc(m->n_local * sizeof *m->local);
    m->ufs = n_ufs ? malloc(m->n_ufs * sizeof *m->ufs) : NULL;
    if (!m->anos || !m->local) goto fim;
    if (n_ufs && !m->ufs) goto fim;
    if (model_reserva_valores(m) != 0) goto fim;

    for (i = 0; i < m->n_anos; i++)
        if (r_i32(f, &m->anos[i]) != 0) goto fim;

    for (i = 0; i < m->n_ufs; i++) {
        if (r_txt(f, m->ufs[i].sigla, sizeof m->ufs[i].sigla) != 0) goto fim;
        if (r_txt(f, m->ufs[i].nome, sizeof m->ufs[i].nome) != 0) goto fim;
        if (r_txt(f, m->ufs[i].regiao, sizeof m->ufs[i].regiao) != 0) goto fim;
    }

    for (i = 0; i < m->n_local; i++) {
        if (r_txt(f, m->local[i].cod, sizeof m->local[i].cod) != 0) goto fim;
        if (r_txt(f, m->local[i].nome, sizeof m->local[i].nome) != 0) goto fim;
        if (r_txt(f, m->local[i].uf, sizeof m->local[i].uf) != 0) goto fim;
        if (r_txt(f, m->local[i].regiao, sizeof m->local[i].regiao) != 0) goto fim;
    }

    total = (size_t)IND_TOTAL * m->n_local * m->n_anos;
    for (i = 0; i < total; i++)
        if (r_i32(f, &m->valores[i]) != 0) goto fim;

    rc = 0;

fim:
    if (f) fclose(f);
    if (rc != 0) model_libera(m);
    return rc;
}

int model_carrega(model *m)
{
    return model_carrega_de(m, MODELO_IDX);
}

/* ------------------------------------------------------------------ */
/* ordenação                                                          */
/* ------------------------------------------------------------------ */

/* O IBGE devolve as localidades por UF e por nome; o SIDRA devolve a série
 * numa ordem que é a da tabela, não a do código. Ordenar por código é o que
 * faz duas execuções da ingestão produzirem índices idênticos byte a byte —
 * e o que faz a consulta não depender de em que ordem a rede respondeu. */
static int cmp_local(const void *pa, const void *pb)
{
    return strcmp(((const locality *)pa)->cod, ((const locality *)pb)->cod);
}

static int cmp_uf(const void *pa, const void *pb)
{
    return strcmp(((const uf_info *)pa)->sigla, ((const uf_info *)pb)->sigla);
}

/* ------------------------------------------------------------------ */
/* utilidades de texto                                                 */
/* ------------------------------------------------------------------ */

/* Copia para um campo de comprimento fixo, truncando. Devolve 0 ou -1. */
static int copia_campo(char *campo, size_t n, const char *src)
{
    size_t k;
    if (!n) return -1;
    if (!src) { campo[0] = '\0'; return 0; }
    k = strlen(src);
    if (k >= n) k = n - 1;
    memcpy(campo, src, k);
    campo[k] = '\0';
    return 0;
}

/* O texto do SIDRA é um número limpo ou uma lacuna: "26919", "1.234" ou
 * ".." / "..." / "-". Devolve o valor, ou MODELO_AUSENTE quando não é número.
 *
 * O ponto é o que impede `""` e `..` de virarem 0: nada aqui devolve zero por
 * falta de informação. */
static int inteiro_do_sidra(const char *s)
{
    long long v = 0;
    int digitos = 0;

    if (!s || !*s) return MODELO_AUSENTE;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            /* Só ponto e espaço em branco são tolerados como separador de
             * milhar/decimal; qualquer outro caractere é lacuna. */
            if (*s != '.' && *s != ' ' && *s != '\t') return MODELO_AUSENTE;
            continue;
        }
        if (v > 2147483647LL / 10) return MODELO_AUSENTE;  /* não cabe em int */
        v = v * 10 + (*s - '0');
        digitos++;
    }
    if (digitos == 0) return MODELO_AUSENTE;
    if (v > 2147483647LL) return MODELO_AUSENTE;
    return (int)v;
}

/* ------------------------------------------------------------------ */
/* ingestão: localidades                                               */
/* ------------------------------------------------------------------ */

/* A UF e a região de um município estão aninhadas em
 * microrregiao.mesorregiao.UF. Nem todo município tem esse caminho: "Boa
 * Esperança do Norte" (MT, criado em 2013) vem sem microrregião, e é por
 * isso que a lista do IBGE tem 5571 entradas para 5570 municípios.
 *
 * O critério de manter uma localidade é exatamente ter esse caminho — e não
 * um filtro por nome ou por código, que quebraria quando o IBGE mudar a
 * lista. O que sobra são os 5570 municípios. */
static const json_val *uf_do_municipio(const json_val *mun)
{
    const json_val *mi = json_obj_get(mun, "microrregiao");
    const json_val *me = json_obj_get(mi, "mesorregiao");
    return json_obj_get(me, "UF");
}

static int ingere_localidades(const char *json, size_t len, model *m)
{
    json_val *raiz = json_parse(json, len);
    const json_val *arr;
    size_t n, i, n_ufs = 0;
    int rc = -1;

    if (!raiz) {
        fprintf(stderr, "IBGE Localidades: resposta não é JSON válido\n");
        return -1;
    }
    arr = raiz;
    n = json_arr_len(arr);
    if (n == 0) {
        fprintf(stderr, "IBGE Localidades: a lista de municípios veio vazia\n");
        goto fim;
    }

    m->local = calloc(n, sizeof *m->local);
    if (!m->local) goto fim;

    for (i = 0; i < n; i++) {
        const json_val *mun = json_arr_get(arr, i);
        const json_val *uf = uf_do_municipio(mun);
        const json_val *regiao = json_obj_get(uf, "regiao");
        const char *nome = json_str(json_obj_get(mun, "nome"));
        const char *sigla = json_str(json_obj_get(uf, "sigla"));
        const json_val *idv = json_obj_get(mun, "id");
        locality *dest;

        if (!uf || !*sigla || !nome || !*nome) continue;
        if (json_tipo_de(idv) != JSON_NUM) continue;
        {
            char cod[8];
            /* O código do município é o id do IBGE: 7 dígitos, o mesmo que
             * o codarea da malha e o D1C do SIDRA usam como chave de junção. */
            snprintf(cod, sizeof cod, "%d", (int)json_num(idv));
            if (strlen(cod) != 7) continue;
            dest = &m->local[m->n_local];
            copia_campo(dest->cod, sizeof dest->cod, cod);
            copia_campo(dest->nome, sizeof dest->nome, nome);
            copia_campo(dest->uf, sizeof dest->uf, sigla);
            copia_campo(dest->regiao, sizeof dest->regiao,
                        json_str(regiao));
            m->n_local++;
        }
    }
    if (m->n_local == 0) {
        fprintf(stderr, "IBGE Localidades: nenhum município tem microrregião — "
                        "o formato da resposta mudou?\n");
        goto fim;
    }

    qsort(m->local, m->n_local, sizeof *m->local, cmp_local);

    /* A tabela de UFs sai da própria lista de municípios: são as siglas que
     * aparecem, e nada mais. `aggregate_opcoes` devolve isto como "ufs
     * realmente disponíveis" (spec §8) — uma lista fixa seria mentira se o
     * dado não trouxesse a UF. */
    m->ufs = calloc(m->n_local, sizeof *m->ufs);
    if (!m->ufs) goto fim;
    for (i = 0; i < m->n_local; i++) {
        size_t j;
        int achou = 0;
        for (j = 0; j < n_ufs; j++) {
            if (strcmp(m->ufs[j].sigla, m->local[i].uf) == 0) { achou = 1; break; }
        }
        if (achou) continue;
        copia_campo(m->ufs[n_ufs].sigla, sizeof m->ufs[n_ufs].sigla, m->local[i].uf);
        copia_campo(m->ufs[n_ufs].regiao, sizeof m->ufs[n_ufs].regiao, m->local[i].regiao);
        {
            /* O nome da UF ("Rondônia") não vem no caminho do município, só a
             * sigla. Ele sai da série do SIDRA, preenchido depois; aqui fica
             * vazio em vez de inventado. */
            m->ufs[n_ufs].nome[0] = '\0';
        }
        n_ufs++;
    }
    m->n_ufs = n_ufs;
    qsort(m->ufs, m->n_ufs, sizeof *m->ufs, cmp_uf);

    rc = 0;

fim:
    json_free(raiz);
    if (rc != 0) {
        free(m->local); m->local = NULL; m->n_local = 0;
        free(m->ufs); m->ufs = NULL; m->n_ufs = 0;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* ingestão: série do SIDRA                                            */
/* ------------------------------------------------------------------ */

/* Os anos que a série tem, vindos do nível de UF: uma requisição de 162 KB
 * contra 4,5 MB por lote no nível de município. É também de lá que vem o nome
 * da UF em extenso, que o caminho das localidades não traz. */
static int ingere_anos_e_nomes_uf(const char *json, size_t len, model *m)
{
    json_val *raiz = json_parse(json, len);
    size_t n, i;
    int rc = -1;

    if (!raiz) {
        fprintf(stderr, "SIDRA 6579 (nível de UF): resposta não é JSON válido\n");
        return -1;
    }
    n = json_arr_len(raiz);
    if (n < 2) {
        fprintf(stderr, "SIDRA 6579 (nível de UF): a série veio vazia\n");
        goto fim;
    }

    m->anos = calloc(n, sizeof *m->anos);
    if (!m->anos) goto fim;

    for (i = 1; i < n; i++) {
        const json_val *linha = json_arr_get(raiz, i);
        const char *d1c = json_str(json_obj_get(linha, "D1C"));
        const char *d3c = json_str(json_obj_get(linha, "D3C"));
        int ano = inteiro_do_sidra(d3c);
        size_t j;

        if (!*d1c || ano <= 0) continue;     /* a linha de cabeçalho */
        if (m->n_anos == n) goto fim;        /* a lista cheia: número demais */
        m->anos[m->n_anos++] = ano;

        /* Nome da UF em extenso, de onde já temos o código. */
        for (j = 0; j < m->n_ufs; j++) {
            char cod[3];
            if (strlen(d1c) != 2) break;
            cod[0] = d1c[0]; cod[1] = d1c[1]; cod[2] = '\0';
            if (strcmp(m->ufs[j].sigla, cod) != 0) continue;
            copia_campo(m->ufs[j].nome, sizeof m->ufs[j].nome,
                        json_str(json_obj_get(linha, "D1N")));
            break;
        }
    }
    if (m->n_anos == 0) {
        fprintf(stderr, "SIDRA 6579 (nível de UF): nenhum ano reconhecido\n");
        goto fim;
    }

    /* A ordem dos anos é a ordem em que a tabela os trouxe, que não é
     * crescente; ordenar aqui é o que faz `anos` ser o índice do ano. */
    {
        int trocou = 1;
        while (trocou) {                      /* insertion sort: 22 elementos */
            size_t k;
            trocou = 0;
            for (k = 1; k < m->n_anos; k++) {
                int x = m->anos[k];
                size_t j = k;
                while (j > 0 && m->anos[j - 1] > x) {
                    m->anos[j] = m->anos[j - 1];
                    j--;
                }
                m->anos[j] = x;
                if (j != k) trocou = 1;
            }
        }
    }

    /* Deduplica: a API devolve uma linha por UF por ano, e uma UF repetida
     * viraria ano repetido, o que faz `model_indice_ano` devolver a primeira
     * ocorrência e a segunda ficar inalcançável. */
    {
        size_t k = 1;
        while (k < m->n_anos) {
            if (m->anos[k] == m->anos[k - 1]) {
                for (size_t j = k; j + 1 < m->n_anos; j++) m->anos[j] = m->anos[j + 1];
                m->n_anos--;
            } else {
                k++;
            }
        }
    }

    rc = 0;

fim:
    json_free(raiz);
    if (rc != 0) {
        free(m->anos); m->anos = NULL; m->n_anos = 0;
    }
    return rc;
}

/* Um lote de anos no nível de município. `preenchidos` recebe quantos
 * municípios ganharam valor em pelo menos um ano do lote. */
static int ingere_lote_municipios(const char *json, size_t len, model *m,
                                  size_t *preenchidos)
{
    json_val *raiz = json_parse(json, len);
    size_t n, i;
    size_t passo = m->n_local * m->n_anos;
    int rc = -1;

    if (preenchidos) *preenchidos = 0;
    if (!raiz) {
        fprintf(stderr, "SIDRA 6579 (municípios): resposta não é JSON válido\n");
        return -1;
    }
    n = json_arr_len(raiz);
    if (n < 2) {
        fprintf(stderr, "SIDRA 6579 (municípios): o lote veio vazio\n");
        goto fim;
    }

    for (i = 1; i < n; i++) {
        const json_val *linha = json_arr_get(raiz, i);
        const char *cod = json_str(json_obj_get(linha, "D1C"));
        const char *v = json_str(json_obj_get(linha, "V"));
        int ano = inteiro_do_sidra(json_str(json_obj_get(linha, "D3C")));
        size_t idx_ano, idx_local;
        int valor;

        if (!*cod) continue;
        idx_local = model_local_de_cod(m, cod);
        if (idx_local >= m->n_local) continue;   /* município fora da lista */
        idx_ano = model_indice_ano(m, ano);
        if (idx_ano >= m->n_anos) continue;      /* ano fora da série */
        valor = inteiro_do_sidra(v);
        m->valores[(size_t)IND_POPULACAO * passo + idx_local * m->n_anos + idx_ano] =
            valor;
        if (valor != MODELO_AUSENTE && preenchidos) (*preenchidos)++;
    }
    rc = 0;

fim:
    json_free(raiz);
    return rc;
}

/* ------------------------------------------------------------------ */
/* ingestão: o comando inteiro                                         */
/* ------------------------------------------------------------------ */

int model_ingere_ibge(const char *conf)
{
    model m;
    char *buf = NULL;
    size_t len = 0, i, n_lote = 0;
    int rc = -1;

    memset(&m, 0, sizeof m);

    /* --- localidades --- */
    printf("IBGE Localidades: baixando a lista de municípios...\n");
    fflush(stdout);
    if (fontes_garente(conf, "ibge_localidades", &buf, &len) != 0) {
        fprintf(stderr, "não consegui baixar a lista de municípios do IBGE "
                        "(fonte ibge_localidades em fontes.conf)\n");
        goto fim;
    }
    if (ingere_localidades(buf, len, &m) != 0) goto fim;
    free(buf); buf = NULL; len = 0;
    printf("  %zu municípios, %zu UFs\n", m.n_local, m.n_ufs);

    /* --- anos e nome das UFs, pelo nível de UF --- */
    printf("SIDRA tabela 6579 (variável 9324): descobrindo os anos...\n");
    fflush(stdout);
    if (fontes_garente(conf, "ibge_sidra_ufs", &buf, &len) != 0) {
        fprintf(stderr, "não consegui baixar a série do SIDRA no nível de UF "
                        "(fonte ibge_sidra_ufs em fontes.conf)\n");
        goto fim;
    }
    if (ingere_anos_e_nomes_uf(buf, len, &m) != 0) goto fim;
    free(buf); buf = NULL; len = 0;
    printf("  anos: ");
    for (i = 0; i < m.n_anos; i++) {
        printf("%s%d", i ? ", " : "", m.anos[i]);
    }
    printf("\n");

    if (model_reserva_valores(&m) != 0) {
        fprintf(stderr, "sem memória para a série\n");
        goto fim;
    }

    /* --- a série municipal, em lotes --- */
    n_lote = (m.n_anos + ANOS_POR_LOTE - 1) / ANOS_POR_LOTE;
    for (i = 0; i < m.n_anos; i += ANOS_POR_LOTE) {
        char sufixo[64];
        char chave[96];
        size_t j, preenchidos = 0;
        size_t ate = (m.n_anos - i < ANOS_POR_LOTE) ? m.n_anos - 1
                                                    : i + ANOS_POR_LOTE - 1;
        int k;

        /* sufixo = "/2001,2002,2003" — a url do .conf é um prefixo, e o
         * período vem depois (fontes_baixa). */
        k = snprintf(sufixo, sizeof sufixo, "/");
        for (j = i; j <= ate; j++)
            k += snprintf(sufixo + k, sizeof sufixo - (size_t)k, "%s%d",
                          j > i ? "," : "", m.anos[j]);

        /* A chave de cache é o intervalo do lote, não o id da fonte: sem ela
         * os oito lotes gravariam todos no mesmo arquivo e o último
         * sobrescreveria os sete anteriores. */
        snprintf(chave, sizeof chave, "ibge_sidra_mun_%d_%d",
                 m.anos[i], m.anos[ate]);

        printf("  lote %zu/%zu, anos %s...\n", i / ANOS_POR_LOTE + 1,
               n_lote, sufixo + 1);
        fflush(stdout);
        if (fontes_baixa(conf, "ibge_sidra_municipios", sufixo, chave,
                         &buf, &len) != 0) {
            fprintf(stderr, "não consegui baixar a série do SIDRA para os anos %s "
                            "(fonte ibge_sidra_municipios em fontes.conf)\n",
                    sufixo + 1);
            goto fim;
        }
        if (ingere_lote_municipios(buf, len, &m, &preenchidos) != 0) {
            free(buf); buf = NULL;
            goto fim;
        }
        free(buf); buf = NULL; len = 0;
        printf("    %zu valores preenchidos\n", preenchidos);
    }

    /* --- outros indicadores --- */
    fprintf(stderr, "aviso: por enquanto só a população foi ingerida; "
                    "estabelecimentos, leitos, UTI e dengue continuam sem "
                    "valor (dados do Ministério da Saúde são tarefa "
                    "posterior)\n");

    if (model_grava(&m, MODELO_IDX) != 0) {
        fprintf(stderr, "não consegui gravar %s\n", MODELO_IDX);
        goto fim;
    }
    printf("índice gravado em %s (%zu municípios × %zu anos)\n",
           MODELO_IDX, m.n_local, m.n_anos);
    rc = 0;

fim:
    free(buf);
    model_libera(&m);
    return rc;
}

/* ------------------------------------------------------------------ */
/* amostra determinística                                              */
/* ------------------------------------------------------------------ */

/* Três municípios, três anos, população plausível. Serve para teste e para
 * diagnóstico (`--amostra`), e é isso que a suíte usa para ter um servidor de
 * verdade sem depender de rede nem do dado real de quem roda o teste.
 *
 * Ela grava um índice de VERDADE: o mesmo formato, as mesmas restrições de
 * ausente, só que pequeno. Nada no servidor sabe a diferença, e é por isso
 * que isto fica documentado como diagnóstico e nunca é chamado pelo caminho
 * normal. */
int model_grava_amostra(const char *caminho)
{
    static const locality LOCAIS[] = {
        { "1100015", "Alta Floresta D'Oeste", "RO", "Norte" },
        { "1200401", "Rio Branco",            "AC", "Norte" },
        { "4106902", "Curitiba",              "PR", "Sul"   }
    };
    static const int ANOS[] = { 2019, 2021, 2024 };
    static const int POP[][3] = {
        { 30014, 30995, 22853 },
        { 114382, 142012, 145035 },
        { 1853097, 1950764, 1969868 }
    };
    static const uf_info UFS[] = {
        { "AC", "Acre",   "Norte" },
        { "PR", "Paraná", "Sul"   },
        { "RO", "Rondônia", "Norte" }
    };
    model m;
    size_t i, j;
    int rc;

    memset(&m, 0, sizeof m);
    m.n_local = sizeof LOCAIS / sizeof LOCAIS[0];
    m.n_anos = sizeof ANOS / sizeof ANOS[0];
    m.n_ufs = sizeof UFS / sizeof UFS[0];
    m.local = malloc(m.n_local * sizeof *m.local);
    m.anos = malloc(m.n_anos * sizeof *m.anos);
    m.ufs = malloc(m.n_ufs * sizeof *m.ufs);
    if (!m.local || !m.anos || !m.ufs) goto falha;
    memcpy(m.local, LOCAIS, sizeof LOCAIS);
    memcpy(m.anos, ANOS, sizeof ANOS);
    memcpy(m.ufs, UFS, sizeof UFS);
    if (model_reserva_valores(&m) != 0) goto falha;

    for (i = 0; i < m.n_local; i++)
        for (j = 0; j < m.n_anos; j++)
            m.valores[(size_t)IND_POPULACAO * m.n_local * m.n_anos +
                      i * m.n_anos + j] = POP[i][j];

    rc = model_grava(&m, caminho);
    model_libera(&m);
    return rc;

falha:
    model_libera(&m);
    return -1;
}

/* ------------------------------------------------------------------ */
/* nomes                                                              */
/* ------------------------------------------------------------------ */

const char *model_nome_indicador(indicador ind)
{
    switch (ind) {
        case IND_POPULACAO: return "populacao";
        case IND_ESTABELECIMENTOS: return "estabelecimentos";
        case IND_LEITOS: return "leitos";
        case IND_UTI: return "uti";
        case IND_DENGUE: return "dengue";
        default: return "desconhecido";
    }
}
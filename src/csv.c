#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "csv.h"
#include "charset.h"

struct csv {
    char **linhas;      /* array de ponteiros para linhas completas (normalizadas) */
    size_t nlinhas;
    char ***campos;     /* campos[linha][col] */
    size_t *ncampos;    /* número de campos por linha */
    size_t maxcol;      /* máximo de colunas */
    char *data;         /* cópia dos dados */
};

static void trim_crlf(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

/* Detecta delimitador contando ocorrências fora de aspas na primeira linha.
 *
 * Empate (nem ';' nem ',') vai para ';': é o separador dos CSVs públicos
 * brasileiros, e num arquivo de uma coluna só a escolha é indiferente. */
int csv_detecta_delimitador(const char *buf, size_t len) {
    size_t i = 0;
    size_t linha_end;
    int count_sem = 0, count_virg = 0;
    int aspas = 0;

    if (!buf || len == 0) return ',';
    /* pula BOM UTF-8 */
    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        i = 3;
    }
    linha_end = i;
    while (linha_end < len && buf[linha_end] != '\n' && buf[linha_end] != '\r') {
        linha_end++;
    }
    for (size_t j = i; j < linha_end; j++) {
        char c = buf[j];
        if (c == '"') {
            /* aspas duplas "" são um literal, não abrem nem fecham campo */
            if (j + 1 < linha_end && buf[j + 1] == '"') {
                j++;
            } else {
                aspas = !aspas;
            }
        } else if (!aspas) {
            if (c == ';') count_sem++;
            if (c == ',') count_virg++;
        }
    }
    if (count_sem > count_virg) return ';';
    if (count_virg > count_sem) return ',';
    return ';';
}

static char *processa_campo(const char *inicio, size_t len_campo, int *tem_aspas) {
    /* copia campo, remove aspas externas e desescapa "" */
    char *tmp = (char *)malloc(len_campo + 1);
    if (!tmp) return NULL;
    memcpy(tmp, inicio, len_campo);
    tmp[len_campo] = '\0';
    trim_crlf(tmp);

    size_t tlen = strlen(tmp);
    if (tlen >= 2 && tmp[0] == '"' && tmp[tlen - 1] == '"') {
        if (tem_aspas) *tem_aspas = 1;
        /* remove aspas externas */
        memmove(tmp, tmp + 1, tlen - 1);
        tmp[tlen - 2] = '\0';
        tlen = strlen(tmp);
        /* desescapa "" */
        char *res = (char *)malloc(tlen + 1);
        if (!res) {
            free(tmp);
            return NULL;
        }
        size_t w = 0;
        for (size_t k = 0; k < tlen; k++) {
            if (tmp[k] == '"' && k + 1 < tlen && tmp[k + 1] == '"') {
                res[w++] = '"';
                k++;
            } else {
                res[w++] = tmp[k];
            }
        }
        res[w] = '\0';
        free(tmp);
        return res;
    } else {
        if (tem_aspas) *tem_aspas = 0;
        /* remove CRLF já feito */
        char *res = strdup(tmp);
        free(tmp);
        return res;
    }
}

csv *csv_abre_mem(const char *buf, size_t len, char delimitador, csv_codificacao cod) {
    if (!buf) return NULL;
    csv *c = (csv *)calloc(1, sizeof(csv));
    if (!c) return NULL;

    /* §9: a transcodificação é POR DATASET. `cod` é a declaração do dataset —
     * CNES e Leitos vêm em Latin-1, Dengue já vem em UTF-8 — e converter
     * "latin-1 em tudo" estraga justamente o que já estava certo: em UTF-8 o
     * byte 0xC3 é o começo de "ã" e 0xA9 é o fim; lidos como Latin-1 viram
     * "Ã£" e "©". Por isso o parâmetro existe e por isso ele é respeitado
     * aqui, em vez de ser descartado com um (void).
     *
     * Quem chama decide o que passa: nenhum dataset é adivinhado a partir do
     * conteúdo, porque um arquivo Latin-1 que só tem ASCII é indistinguível de
     * um UTF-8 — e a diferença só aparece quando o acento já chegou torto. */
    if (cod == CSV_ISO88591) {
        size_t ulen = 0;
        c->data = text_latin1_para_utf8(buf, len, &ulen);
        if (!c->data) {
            free(c);
            return NULL;
        }
        len = ulen;
    } else {
        c->data = (char *)malloc(len + 1);
        if (!c->data) {
            free(c);
            return NULL;
        }
        memcpy(c->data, buf, len);
        c->data[len] = '\0';
    }

    /* divide em linhas */
    char *p = c->data;
    /* pula BOM */
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF && p == c->data) {
        p += 3;
    }
    size_t capacidade = 16;
    c->linhas = (char **)malloc(capacidade * sizeof(char *));
    c->nlinhas = 0;

    char *start = p;
    while (*start || start < c->data + len) { /* loop */
        char *nl = start;
        while (*nl && *nl != '\n' && *nl != '\r') nl++;
        size_t linelen = nl - start;
        /* extrai linha */
        char *linha = (char *)malloc(linelen + 1);
        memcpy(linha, start, linelen);
        linha[linelen] = '\0';
        /* não trim aqui - mantém */

        if (c->nlinhas >= capacidade) {
            capacidade *= 2;
            c->linhas = (char **)realloc(c->linhas, capacidade * sizeof(char *));
        }
        c->linhas[c->nlinhas++] = linha;

        if (*nl == '\0') break;
        if (*nl == '\r' && nl[1] == '\n') {
            start = nl + 2;
        } else {
            start = nl + 1;
        }
        if (start >= c->data + len) {
            /* linha vazia final? */
            break;
        }
    }

    /* agora parseia campos para cada linha */
    c->ncampos = (size_t *)calloc(c->nlinhas, sizeof(size_t));
    c->campos = (char ***)calloc(c->nlinhas, sizeof(char **));
    c->maxcol = 0;

    for (size_t l = 0; l < c->nlinhas; l++) {
        char *linha = c->linhas[l];
        /* conta campos */
        size_t ncam = 1;
        int aspas = 0;
        size_t llen = strlen(linha);
        for (size_t i = 0; i < llen; i++) {
            if (linha[i] == '"') {
                if (i + 1 < llen && linha[i + 1] == '"') {
                    i++;
                } else {
                    aspas = !aspas;
                }
            } else if (!aspas && linha[i] == delimitador) {
                ncam++;
            }
        }
        if (ncam > c->maxcol) c->maxcol = ncam;
        c->ncampos[l] = ncam;
        c->campos[l] = (char **)calloc(ncam, sizeof(char *));
        /* extrai */
        size_t col = 0;
        aspas = 0;
        size_t campo_inicio = 0;
        for (size_t i = 0; i <= llen; i++) {
            int fim = 0;
            if (i == llen) {
                fim = 1;
            } else {
                char ch = linha[i];
                if (ch == '"') {
                    if (i + 1 < llen && linha[i + 1] == '"') {
                        i++;
                    } else {
                        aspas = !aspas;
                    }
                } else if (!aspas && ch == delimitador) {
                    fim = 1;
                }
            }
            if (fim) {
                /* extrai campo de campo_inicio até i */
                size_t clen = i - campo_inicio;
                c->campos[l][col] = processa_campo(linha + campo_inicio, clen, NULL);
                col++;
                campo_inicio = i + 1;
                if (i == llen) break;
            }
        }
    }

    return c;
}

void csv_fecha(csv *c) {
    if (!c) return;
    if (c->linhas) {
        for (size_t i = 0; i < c->nlinhas; i++) {
            free(c->linhas[i]);
        }
        free(c->linhas);
    }
    if (c->campos) {
        for (size_t i = 0; i < c->nlinhas; i++) {
            if (c->campos[i]) {
                for (size_t j = 0; j < c->ncampos[i]; j++) {
                    free(c->campos[i][j]);
                }
                free(c->campos[i]);
            }
        }
        free(c->campos);
    }
    free(c->ncampos);
    free(c->data);
    free(c);
}

size_t csv_num_linhas(const csv *c) {
    if (!c) return 0;
    return c->nlinhas;
}

const char *csv_cabecalho(const csv *c, size_t idx_coluna) {
    if (!c || c->nlinhas == 0) return NULL;
    if (idx_coluna >= c->ncampos[0]) return NULL;
    return c->campos[0][idx_coluna];
}

int csv_indice_coluna(const csv *c, const char *nome) {
    if (!c || !nome || c->nlinhas == 0) return -1;
    for (size_t i = 0; i < c->ncampos[0]; i++) {
        if (c->campos[0][i] && strcmp(c->campos[0][i], nome) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int csv_campo(const csv *c, size_t linha, size_t coluna, char *out, size_t out_len) {
    if (!c || !out || out_len == 0) return -1;
    if (linha >= c->nlinhas) return -1;
    if (coluna >= c->ncampos[linha]) return -1;
    const char *val = c->campos[linha][coluna];
    if (!val) {
        out[0] = '\0';
        return 0;
    }
    size_t vlen = strlen(val);
    if (vlen >= out_len) {
        /* copia até caber? mas contrato diz out recebe valor; mas tamanho limitado */
        memcpy(out, val, out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }
    strcpy(out, val);
    return 0;
}

long long csv_int(const csv *c, size_t linha, size_t coluna, long long padrao) {
    if (!c) return padrao;
    if (linha >= c->nlinhas || coluna >= c->ncampos[linha]) return padrao;
    const char *val = c->campos[linha][coluna];
    if (!val || *val == '\0') return padrao;
    char *end = NULL;
    long long res = strtoll(val, &end, 10);
    if (end == val) return padrao;
    return res;
}

double csv_double(const csv *c, size_t linha, size_t coluna, double padrao) {
    if (!c) return padrao;
    if (linha >= c->nlinhas || coluna >= c->ncampos[linha]) return padrao;
    const char *val = c->campos[linha][coluna];
    if (!val || *val == '\0') return padrao;
    char *end = NULL;
    double res = strtod(val, &end);
    if (end == val) return padrao;
    return res;
}

int csv_vazio(const csv *c, size_t linha, size_t coluna) {
    if (!c) return 1;
    if (linha >= c->nlinhas || coluna >= c->ncampos[linha]) return 1;
    const char *val = c->campos[linha][coluna];
    if (!val || *val == '\0') return 1;
    return 0;
}

/* Acumulador de texto com crescimento dobrando.
 *
 * O mesmo motivo do acumulador de src/api.c: um buffer de tamanho fixo com
 * `snprintf` e subtração de tamanho é a forma curta de um heap-buffer-overflow
 * de escrita, e um CSV de CNES tem 40+ colunas com nome de unidade de saúde
 * comprido o bastante para que "cabe no meu palpite" seja uma estratégia
 * errada. */
struct txt {
    char  *p;
    size_t n;      /* usado, sem o NUL */
    size_t cap;
};

static void txt_init(struct txt *t)
{
    t->p = NULL;
    t->n = 0;
    t->cap = 0;
}

static int txt_reserva(struct txt *t, size_t extra)
{
    size_t need = t->n + extra + 1;
    char *novo;
    size_t cap;

    if (need <= t->cap) return 0;
    cap = t->cap ? t->cap : 256;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) return -1;
        cap *= 2;
    }
    novo = (char *)realloc(t->p, cap);
    if (!novo) return -1;
    t->p = novo;
    t->cap = cap;
    return 0;
}

static int txt_puts(struct txt *t, const char *s)
{
    size_t n = strlen(s);
    if (txt_reserva(t, n) != 0) return -1;
    memcpy(t->p + t->n, s, n + 1);
    t->n += n;
    return 0;
}

/* Um campo do CSV como campo JSON.
 *
 * Se o valor for um inteiro inteiro (só dígitos, com sinal opcional), sai como
 * número: é o que o front quer para ordenar e somar sem reconverter. Se não
 * for, sai como string escapada. Campo vazio sai como string vazia — a coluna
 * existe no CSV, e isso é informação; `null` aqui diria que o dado não veio,
 * que é outra coisa. */
static int txt_json_campo(struct txt *t, const char *rotulo, const char *valor)
{
    char *esc;
    const char *v = valor ? valor : "";
    char *fim;
    long long num;
    char buf[32];
    int rc = -1;

    esc = text_json_escape(rotulo);
    if (!esc) return -1;

    if (*v) {
        num = strtoll(v, &fim, 10);
        if (*fim == '\0' && fim != v) {
            snprintf(buf, sizeof buf, "%lld", num);
            if (txt_puts(t, "\"") != 0) goto fim;
            if (txt_puts(t, esc) != 0) goto fim;
            if (txt_puts(t, "\":") != 0) goto fim;
            if (txt_puts(t, buf) != 0) goto fim;
            rc = 0;
            goto fim;
        }
    }
    if (txt_puts(t, "\"") != 0) goto fim;
    if (txt_puts(t, esc) != 0) goto fim;
    if (txt_puts(t, "\":\"") != 0) goto fim;
    {
        char *esc_val = text_json_escape(v);
        if (!esc_val) goto fim;
        rc = txt_puts(t, esc_val);
        free(esc_val);
        if (rc != 0) goto fim;
    }
    if (txt_puts(t, "\"") != 0) goto fim;
    rc = 0;
fim:
    free(esc);
    return rc;
}

/* Uma linha do CSV como objeto JSON, para consumo direto pelo navegador.
 *
 * `cols` são os índices das colunas a incluir, e o rótulo de cada campo é o
 * cabeçalho daquela coluna — que é o nome que a pessoa lê no CSV. `chave_json`,
 * quando não é SIZE_MAX, é a coluna cujo valor vira a chave `"id"` do objeto:
 * é assim que `CO_IBGE` de Leitos/CNES vira a chave de junção com o `codarea`
 * da malha (spec §8). Sem chave, o objeto sai com os campos e sem `"id"`.
 *
 * Antes isto devolvia a string constante "{}", que é JSON válido e não diz
 * nada: um consumidor que receba "{}" para toda linha tem uma resposta vazia
 * que parece uma resposta certa. Devolve NULL só quando não há o que montar —
 * linha fora da faixa, ou nenhuma coluna. */
char *csv_linha_json(const csv *c, size_t linha, const size_t *cols,
                     size_t ncols, size_t chave_json)
{
    struct txt t;
    size_t i;
    int primeiro = 1;

    if (!c || !cols || ncols == 0) return NULL;
    if (linha >= c->nlinhas) return NULL;

    txt_init(&t);
    if (txt_puts(&t, "{") != 0) goto fim;

    for (i = 0; i < ncols; i++) {
        size_t col = cols[i];
        char rotulo[64];
        const char *cab;
        const char *valor;

        if (col >= c->ncampos[linha]) continue;
        valor = c->campos[linha][col];

        /* Rótulo: o cabeçalho da coluna. Cai para "c<N>" quando não há
         * cabeçalho — uma linha sem cabeçalho não pode fazer a coluna
         * desaparecer da saída. */
        cab = csv_cabecalho(c, col);
        if (cab && *cab)
            snprintf(rotulo, sizeof rotulo, "%s", cab);
        else
            snprintf(rotulo, sizeof rotulo, "c%zu", col);

        if (!primeiro && txt_puts(&t, ",") != 0) goto fim;
        if (txt_json_campo(&t, rotulo, valor) != 0) goto fim;
        primeiro = 0;
    }

    if (chave_json != (size_t)-1 && chave_json < c->ncampos[linha]) {
        char *esc_id = text_json_escape(c->campos[linha][chave_json]);
        if (!esc_id) goto fim;
        if (!primeiro && txt_puts(&t, ",") != 0) { free(esc_id); goto fim; }
        if (txt_puts(&t, "\"id\":\"") != 0) { free(esc_id); goto fim; }
        if (txt_puts(&t, esc_id) != 0) { free(esc_id); goto fim; }
        if (txt_puts(&t, "\"") != 0) { free(esc_id); goto fim; }
        free(esc_id);
    }

    if (txt_puts(&t, "}") != 0) goto fim;
    return t.p;

fim:
    free(t.p);
    return NULL;
}

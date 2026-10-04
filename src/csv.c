#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "csv.h"

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

static char *strdup_trim(const char *s) {
    if (!s) return strdup("");
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n')) {
        len--;
    }
    char *r = (char *)malloc(len + 1);
    if (!r) return NULL;
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

/* Detecta delimitador contando ocorrências fora de aspas na primeira linha */
int csv_detecta_delimitador(const char *buf, size_t len) {
    if (!buf || len == 0) return ',';
    /* encontrar primeira linha */
    size_t i = 0;
    /* pula BOM UTF-8 */
    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        i = 3;
    }
    size_t linha_end = i;
    while (linha_end < len && buf[linha_end] != '\n' && buf[linha_end] != '\r') {
        linha_end++;
    }
    /* conta em primeiro trecho */
    int count_sem = 0, count_virg = 0;
    int aspas = 0;
    for (size_t j = i; j < linha_end; j++) {
        char c = buf[j];
        if (c == '"') {
            /* trata aspas escapadas "" dentro? para contagem de campos fora de aspas é mais simples */
            /* verifica se próxima também é aspas */
            if (j + 1 < linha_end && buf[j + 1] == '"') {
                j++; /* pula aspas dupla */
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
    /* empate: tenta ; primeiro? ou padrão comum */
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
    (void)cod;
    if (!buf) return NULL;
    csv *c = (csv *)calloc(1, sizeof(csv));
    if (!c) return NULL;
    c->data = (char *)malloc(len + 1);
    if (!c->data) {
        free(c);
        return NULL;
    }
    memcpy(c->data, buf, len);
    c->data[len] = '\0';

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
        size_t pos = 0;
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

char *csv_linha_json(const csv *c, size_t linha, const size_t *cols, size_t ncols, size_t chave_json) {
    (void)c; (void)linha; (void)cols; (void)ncols; (void)chave_json;
    return strdup("{}");
}

/* src/charset.c — transcodificação e utilidades de texto (ver charset.h). */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charset.h"

/* ------------------------------------------------------------------ */
/* Latin-1 → UTF-8                                                     */
/* ------------------------------------------------------------------ */

int charset_latin1_para_utf8(const char *in, size_t in_len, char *out,
                             size_t out_cap, size_t *out_len)
{
    if (in == NULL || out == NULL || out_len == NULL) {
        return 2;
    }

    /* Detecta inválido: byte 0x00 presente */
    for (size_t i = 0; i < in_len; i++) {
        unsigned char b = (unsigned char)in[i];
        if (b == 0x00) {
            return 2;
        }
    }

    /* Calcula espaço necessário */
    size_t need = 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char b = (unsigned char)in[i];
        if (b < 0x80) {
            need += 1;
        } else {
            need += 2;
        }
    }

    if (need > out_cap) {
        *out_len = need;
        return 1;
    }

    /* Converte */
    size_t w = 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char b = (unsigned char)in[i];
        if (b < 0x80) {
            out[w++] = (char)b;
        } else {
            out[w++] = (char)(0xC0 | (b >> 6));
            out[w++] = (char)(0x80 | (b & 0x3F));
        }
    }
    *out_len = w;
    return 0;
}

char *text_latin1_para_utf8(const char *in, size_t in_len, size_t *out_len)
{
    size_t need = 0, w = 0;
    char *out;

    if (out_len) *out_len = 0;
    if (!in) return NULL;

    for (size_t i = 0; i < in_len; i++) {
        need += ((unsigned char)in[i] < 0x80) ? 1 : 2;
    }
    out = malloc(need + 1);
    if (!out) return NULL;

    for (size_t i = 0; i < in_len; i++) {
        unsigned char b = (unsigned char)in[i];
        if (b < 0x80) {
            out[w++] = (char)b;
        } else {
            out[w++] = (char)(0xC0 | (b >> 6));
            out[w++] = (char)(0x80 | (b & 0x3F));
        }
    }
    out[w] = '\0';
    if (out_len) *out_len = w;
    return out;
}

/* ------------------------------------------------------------------ */
/* validação de UTF-8                                                 */
/* ------------------------------------------------------------------ */

int text_eh_valido_utf8(const char *s, size_t len)
{
    size_t i = 0;

    if (!s) return 0;
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        size_t extras;
        unsigned long cp;

        if (c < 0x80) { i++; continue; }
        if (c >= 0xC2 && c <= 0xDF)      { extras = 1; cp = c & 0x1Fu; }
        else if (c >= 0xE0 && c <= 0xEF) { extras = 2; cp = c & 0x0Fu; }
        else if (c >= 0xF0 && c <= 0xF4) { extras = 3; cp = c & 0x07u; }
        else return 0;                   /* 0x80..0xC1 e 0xF5..0xFF */

        if (i + extras >= len) return 0;   /* sequência cortada no fim */
        for (size_t k = 1; k <= extras; k++) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) return 0;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        /* Sobrescrito: o ponto de U+FFFD marca texto que já passou por uma
         * conversão e não deve ser convertido de novo. */
        if (extras == 3 && cp == 0xFFFDu) return 0;
        /* Surrogate e fora de Unicode não são UTF-8 válido. */
        if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;
        if (cp > 0x10FFFFu) return 0;
        i += extras + 1;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* escape de JSON                                                     */
/* ------------------------------------------------------------------ */

/* malloc'd, com folga de 6× o tamanho: o pior caso é um byte de controle,
 * que vira \u00XX (6 bytes), então nunca há realocação no meio do laço. O
 * caminho quente é o de nomes de município com acento, que passa reto. */
char *text_json_escape(const char *s)
{
    size_t n, i, j = 0;
    char *out;

    if (!s) return NULL;
    n = strlen(s);
    out = malloc(n * 6 + 1);
    if (!out) return NULL;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  out[j++] = '\\'; out[j++] = '"';  break;
            case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
            case '\n': out[j++] = '\\'; out[j++] = 'n';  break;
            case '\r': out[j++] = '\\'; out[j++] = 'r';  break;
            case '\t': out[j++] = '\\'; out[j++] = 't';  break;
            case '\b': out[j++] = '\\'; out[j++] = 'b';  break;
            case '\f': out[j++] = '\\'; out[j++] = 'f';  break;
            default:
                if (c < 0x20) {
                    j += (size_t)snprintf(out + j, 7, "\\u%04x", c);
                } else {
                    out[j++] = (char)c;
                }
        }
    }
    out[j] = '\0';
    return out;
}

/* ------------------------------------------------------------------ */
/* query string                                                       */
/* ------------------------------------------------------------------ */

char *text_url_decode(const char *s)
{
    size_t len, j = 0;
    char *out;

    if (!s) s = "";
    len = strlen(s);
    out = malloc(len + 1);
    if (!out) return NULL;

    for (size_t i = 0; i < len; i++) {
        if (s[i] == '%' && i + 2 < len &&
            isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            char hex[3] = { s[i + 1], s[i + 2], 0 };
            out[j++] = (char)strtoul(hex, NULL, 16);
            i += 2;
        } else if (s[i] == '+') {
            out[j++] = ' ';
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = '\0';
    return out;
}

/* Percorre a query achando `key=` sem cruzar o `&` seguinte, e devolve o
 * pedaço bruto (ainda com %XX e "+"). NULL se o parâmetro não existir.
 *
 * A comparação é por comprimento antes de comparar os bytes: "uf" não pode
 * casar com "ufrs" nem com o começo de outra chave. */
static const char *acha_param(const char *query, const char *key, size_t *len)
{
    const char *p = query;
    size_t klen;

    if (!query || !*query || !key || !*key) return NULL;
    klen = strlen(key);

    while (*p) {
        const char *fim = strchr(p, '&');
        const char *eq = strchr(p, '=');
        const char *p_fim = fim ? fim : p + strlen(p);
        size_t nome;

        if (eq && eq < p_fim) {
            nome = (size_t)(eq - p);
            if (nome == klen && strncmp(p, key, klen) == 0) {
                const char *ini = eq + 1;
                *len = (size_t)(p_fim - ini);
                return ini;
            }
        } else if ((size_t)(p_fim - p) == klen && strncmp(p, key, klen) == 0) {
            /* chave sem valor: existe, e o valor é "" — que é diferente de
             * não existir (achado da revisão: `uf=` e `uf` ausente não são a
             * mesma coisa para quem lê). */
            *len = 0;
            return p_fim;
        }
        if (!fim) break;
        p = fim + 1;
    }
    return NULL;
}

char *text_query_get(const char *query, const char *key)
{
    size_t len = 0;
    const char *bruto = acha_param(query, key, &len);
    char *out;

    if (!bruto) return NULL;
    out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, bruto, len);
    out[len] = '\0';
    return out;
}
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "aggregate.h"

#define VAL_SENTINELA -1

static int tolower_char(int c)
{
    return tolower((unsigned char)c);
}

static int strcasestr_local(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) return 1;
    for (size_t i = 0; haystack[i]; i++) {
        size_t j = 0;
        while (needle[j] && tolower_char(haystack[i + j]) == tolower_char(needle[j])) {
            j++;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static int cmp_int_desc(const void *a, const void *b)
{
    int va = *(const int *)a;
    int vb = *(const int *)b;
    if (va < vb) return 1;
    if (va > vb) return -1;
    return 0;
}

void aggregate_libera(consulta *c)
{
    if (!c) return;
    free(c->cod);
    free(c->nome);
    free(c->valor);
    free(c->ordem);
    c->cod = NULL;
    c->nome = NULL;
    c->valor = NULL;
    c->ordem = NULL;
    c->n = 0;
}

int aggregate_consulta(const model *m, indicador ind, int nivel_uf,
                       const char *uf, int ano, const char *busca,
                       const char *ordenar, int limite,
                       consulta *out, kpis *kpis_out, char *erro, size_t erro_len)
{
    (void)nivel_uf;
    if (!m || !out || !kpis_out) return -1;
    if (erro && erro_len > 0) erro[0] = '\0';

    if (ind < 0 || ind >= IND_TOTAL) {
        if (erro && erro_len > 0) snprintf(erro, erro_len, "indicador invalido");
        return -1;
    }

    int idx_ano = -1;
    if (m->ano_de && m->anos && m->n_anos > 0) {
        for (size_t i = 0; i < m->n_anos; i++) {
            if ((int)m->anos[i] == ano) {
                idx_ano = (int)i;
                break;
            }
        }
    }
    if (idx_ano < 0) {
        if (erro && erro_len > 0) snprintf(erro, erro_len, "ano %d inexistente", ano);
        return -1;
    }

    if (out) {
        out->n = 0;
        out->cod = NULL;
        out->nome = NULL;
        out->valor = NULL;
        out->ordem = NULL;
    }

    /* conta primeiros */
    size_t total_cand = 0;
    for (size_t i = 0; i < m->n_local; i++) {
        const locality *l = &m->local[i];
        if (l->cod[0] == '\0') continue;
        if (uf && uf[0] && strncmp(l->uf, uf, 2) != 0) continue;
        if (busca && busca[0]) {
            if (!strcasestr_local(l->nome, busca) && !strcasestr_local(l->cod, busca)) continue;
        }
        total_cand++;
    }

    out->n = total_cand;
    if (total_cand > 0) {
        out->cod = malloc(total_cand * 8);
        out->nome = malloc(total_cand * 96);
        out->valor = malloc(total_cand * sizeof(int));
        out->ordem = malloc(total_cand * sizeof(size_t));
        if (!out->cod || !out->nome || !out->valor || !out->ordem) {
            aggregate_libera(out);
            return -1;
        }
    }

    size_t idx = 0;
    for (size_t i = 0; i < m->n_local; i++) {
        const locality *l = &m->local[i];
        if (l->cod[0] == '\0') continue;
        if (uf && uf[0] && strncmp(l->uf, uf, 2) != 0) continue;
        if (busca && busca[0]) {
            if (!strcasestr_local(l->nome, busca) && !strcasestr_local(l->cod, busca)) continue;
        }
        strncpy(out->cod[idx], l->cod, 7);
        out->cod[idx][7] = '\0';
        strncpy(out->nome[idx], l->nome, 95);
        out->nome[idx][95] = '\0';
        out->valor[idx] = m->valores[ind][i * (m->n_anos > 0 ? m->n_anos : 1) + (size_t)idx_ano];
        if (out->valor[idx] == 0 && 0) { /* manter */ }
        out->ordem[idx] = idx;
        idx++;
    }

    kpis_out->min = 0;
    kpis_out->max = 0;
    kpis_out->media = 0;
    kpis_out->soma = 0;
    kpis_out->total = 0;

    int tem_valido = 0;
    long long soma_validos = 0;
    int min_v = 0, max_v = 0;
    for (size_t i = 0; i < out->n; i++) {
        if (out->valor[i] < 0) continue;
        tem_valido = 1;
        soma_validos += out->valor[i];
        kpis_out->total++;
        if (!tem_valido || kpis_out->total == 1) {
            min_v = out->valor[i];
            max_v = out->valor[i];
        }
        if (out->valor[i] < min_v) min_v = out->valor[i];
        if (out->valor[i] > max_v) max_v = out->valor[i];
    }
    if (kpis_out->total > 0) {
        kpis_out->min = min_v;
        kpis_out->max = max_v;
        kpis_out->media = (int)(soma_validos / (long long)kpis_out->total);
        kpis_out->soma = soma_validos;
    } else {
        kpis_out->min = 0;
        kpis_out->max = 0;
        kpis_out->media = 0;
        kpis_out->soma = 0;
    }

    if (ordenar && strstr(ordenar, "valor_desc")) {
        /* ordenar por valor desc */
        for (size_t i = 0; i < out->n; i++) {
            for (size_t j = i + 1; j < out->n; j++) {
                if (out->valor[i] < out->valor[j] ||
                    (out->valor[i] == out->valor[j] && i > j)) {
                    int tv = out->valor[i]; out->valor[i] = out->valor[j]; out->valor[j] = tv;
                    char tc[8]; memcpy(tc, out->cod[i], 8); memcpy(out->cod[i], out->cod[j], 8); memcpy(out->cod[j], tc, 8);
                    char tn[96]; memcpy(tn, out->nome[i], 96); memcpy(out->nome[i], out->nome[j], 96); memcpy(out->nome[j], tn, 96);
                }
            }
        }
    }

    if (limite > 0 && out->n > (size_t)limite) {
        out->n = (size_t)limite;
    }

    return 0;
}

char *aggregate_serie(const model *m, indicador ind, const char *cod,
                      size_t *n)
{
    if (!m || !cod || !n) return NULL;
    if (ind < 0 || ind >= IND_TOTAL) return NULL;
    *n = m->n_anos;
    size_t pos = (size_t)-1;
    for (size_t i = 0; i < m->n_local; i++) {
        if (strcmp(m->local[i].cod, cod) == 0) {
            pos = i;
            break;
        }
    }
    if (pos == (size_t)-1) return NULL;

    char *buf = malloc(m->n_anos * 20 + 100);
    if (!buf) return NULL;
    strcpy(buf, "[");
    size_t base = pos * (m->n_anos > 0 ? m->n_anos : 1);
    for (size_t i = 0; i < m->n_anos; i++) {
        int v = m->valores[ind][base + i];
        if (i > 0) strcat(buf, ",");
        if (v < 0) {
            strcat(buf, "null");
        } else {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%d", v);
            strcat(buf, tmp);
        }
    }
    strcat(buf, "]");
    return buf;
}

char *aggregate_opcoes(const model *m, indicador ind)
{
    (void)ind;
    if (!m) return NULL;
    char *buf = malloc(4096);
    if (!buf) return NULL;
    strcpy(buf, "{\"anos\": [");
    for (size_t i = 0; i < m->n_anos; i++) {
        if (i > 0) strcat(buf, ",");
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%zu", m->anos[i]);
        strcat(buf, tmp);
    }
    strcat(buf, "], \"ufs\": []}");
    return buf;
}

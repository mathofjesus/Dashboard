#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "fontes.h"

static char *dup_str(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len + 1);
    return p;
}

static void trim(char *s)
{
    if (!s) return;
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    /* if shifted, move */
    if (s != s) {
        memmove(s, s, strlen(s) + 1);
    }
}

void fontes_libera(fonte_dados *fontes, size_t n)
{
    if (!fontes) return;
    for (size_t i = 0; i < n; i++) {
        free(fontes[i].id);
        free(fontes[i].nome);
        free(fontes[i].tipo);
        free(fontes[i].url_zip);
        free(fontes[i].url_cnes);
        free(fontes[i].url_leitos);
        free(fontes[i].url_dengue);
        free(fontes[i].ibge_tid);
        free(fontes[i].cod_ibge);
        free(fontes[i].uf);
        if (fontes[i].colunas) {
            for (size_t j = 0; j < fontes[i].ncolunas; j++) {
                free(fontes[i].colunas[j]);
            }
            free(fontes[i].colunas);
        }
    }
    free(fontes);
}

int fontes_ler(const char *path, fonte_dados **fontes, size_t *n)
{
    if (!path || !fontes || !n) return -1;
    *fontes = NULL;
    *n = 0;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[4096];
    fonte_dados *arr = NULL;
    size_t count = 0;
    size_t cap = 0;
    
    while (fgets(line, sizeof(line), f)) {
        /* skip empty lines */
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '\n' || *p == '\r') continue;
        /* skip comments if any */
        if (*p == '#') continue;
        /* check for section [name] */
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                char *sec = p + 1;
                trim(sec);
                if (count == cap) {
                    cap = cap == 0 ? 8 : cap * 2;
                    fonte_dados *tmp = realloc(arr, cap * sizeof(fonte_dados));
                    if (!tmp) {
                        fontes_libera(arr, count);
                        fclose(f);
                        return -1;
                    }
                    arr = tmp;
                }
                memset(&arr[count], 0, sizeof(fonte_dados));
                arr[count].id = dup_str(sec);
                arr[count].tem_utf8 = 0;
                count++;
            }
            continue;
        }
        /* key = value */
        char *eq = strchr(p, '=');
        if (!eq) continue; /* linha sem = é ignorada sem crash */
        *eq = '\0';
        char *key = p;
        char *val = eq + 1;
        trim(key);
        trim(val);
        /* remove trailing newline from val */
        char *endv = val + strlen(val) - 1;
        while (endv >= val && (*endv == '\n' || *endv == '\r')) {
            *endv = '\0';
            endv--;
        }
        if (count == 0) continue; /* no section yet */
        fonte_dados *cur = &arr[count - 1];
        if (strcmp(key, "id") == 0) {
            free(cur->id);
            cur->id = dup_str(val);
        } else if (strcmp(key, "nome") == 0) {
            cur->nome = dup_str(val);
        } else if (strcmp(key, "tipo") == 0) {
            cur->tipo = dup_str(val);
        } else if (strcmp(key, "url_zip") == 0 || strcmp(key, "url") == 0) {
            cur->url_zip = dup_str(val);
        } else if (strcmp(key, "url_cnes") == 0) {
            cur->url_cnes = dup_str(val);
        } else if (strcmp(key, "url_leitos") == 0) {
            cur->url_leitos = dup_str(val);
        } else if (strcmp(key, "url_dengue") == 0) {
            cur->url_dengue = dup_str(val);
        } else if (strcmp(key, "tem_utf8") == 0) {
            cur->tem_utf8 = atoi(val);
        } else if (strcmp(key, "ibge_tid") == 0) {
            cur->ibge_tid = dup_str(val);
        } else if (strcmp(key, "cod_ibge") == 0) {
            cur->cod_ibge = dup_str(val);
        } else if (strcmp(key, "uf") == 0) {
            cur->uf = dup_str(val);
        } else if (strcmp(key, "dataset") == 0) {
            /* store in nome or just ignore if not needed */
        } else if (strcmp(key, "ttl") == 0) {
            /* ignore */
        }
    }
    fclose(f);
    *fontes = arr;
    *n = count;
    if (count == 0) {
        free(arr);
        *fontes = NULL;
        return -1;
    }
    return 0;
}

int fontes_garente(fonte_dados *fontes, size_t n, fonte_dados **selecionada, const char *id)
{
    if (!fontes || n == 0 || !id) return -1;
    for (size_t i = 0; i < n; i++) {
        if (fontes[i].id && strcmp(fontes[i].id, id) == 0) {
            if (selecionada) *selecionada = &fontes[i];
            return 0;
        }
    }
    if (selecionada) *selecionada = NULL;
    return -1;
}

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "fontes.h"
#include "cache.h"
#include "http_client.h"

static char *dup_str(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len + 1);
    return p;
}

/* Aponta o começo e o fim do texto sem espaço nas pontas, **no lugar**.
 *
 * Devolve o começo do texto já aparado, mas também desloca os bytes: sem o
 * memmove, o ponteiro devolvido serve só para ler e quem chamou continues
 * apontando para a cópia dentro do buffer — que era o bug do `if (s != s)`,
 * condição que nunca é verdadeira e portanto nunca movia nada. O sintoma era
 * toda URL de fontes.conf saindo com um espaço à esquerda e sendo recusada
 * por http_client.c:url_aceita(), que só aceita o que começa por "http".
 */
static char *trim(char *s)
{
    char *ini, *fim;

    if (!s) return NULL;
    ini = s;
    while (isspace((unsigned char)*ini)) ini++;
    fim = ini + strlen(ini);
    while (fim > ini && isspace((unsigned char)fim[-1])) fim--;
    *fim = '\0';
    if (ini != s) memmove(s, ini, (size_t)(fim - ini) + 1);
    return s;
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
        free(fontes[i].dataset);
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

static int subst(char **campo, const char *valor)
{
    free(*campo);
    *campo = dup_str(valor);
    return *campo ? 0 : -1;
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
        char *p = trim(line);
        if (*p == '\0') continue;             /* linha vazia ou só espaços */
        if (*p == '#') continue;              /* comentário */
        if (*p == '[') {                      /* nova seção */
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                if (count == cap) {
                    fonte_dados *tmp;
                    cap = cap == 0 ? 8 : cap * 2;
                    tmp = realloc(arr, cap * sizeof(fonte_dados));
                    if (!tmp) goto falha;
                    arr = tmp;
                }
                memset(&arr[count], 0, sizeof(fonte_dados));
                arr[count].ttl = CACHE_TTL_NENHUM;
                arr[count].id = dup_str(trim(p + 1));
                if (!arr[count].id) goto falha;
                count++;
            }
            continue;
        }

        {
            char *eq = strchr(p, '=');
            char *key, *val;
            fonte_dados *cur;

            if (!eq) continue;                /* linha sem "=" é ignorada */
            *eq = '\0';
            key = trim(p);
            val = trim(eq + 1);
            if (count == 0) continue;         /* chave antes de qualquer seção */
            cur = &arr[count - 1];

            if (strcmp(key, "id") == 0)              subst(&cur->id, val);
            else if (strcmp(key, "nome") == 0)       subst(&cur->nome, val);
            else if (strcmp(key, "tipo") == 0)       subst(&cur->tipo, val);
            else if (strcmp(key, "url_zip") == 0 || strcmp(key, "url") == 0)
                                                subst(&cur->url_zip, val);
            else if (strcmp(key, "url_cnes") == 0)  subst(&cur->url_cnes, val);
            else if (strcmp(key, "url_leitos") == 0) subst(&cur->url_leitos, val);
            else if (strcmp(key, "url_dengue") == 0) subst(&cur->url_dengue, val);
            else if (strcmp(key, "tem_utf8") == 0)  cur->tem_utf8 = atoi(val);
            else if (strcmp(key, "ibge_tid") == 0)  subst(&cur->ibge_tid, val);
            else if (strcmp(key, "cod_ibge") == 0)  subst(&cur->cod_ibge, val);
            else if (strcmp(key, "uf") == 0)         subst(&cur->uf, val);
            /* `dataset` e `ttl` não são decorativos: o primeiro diz qual
             * entrada do ZIP é o CSV e em que codificação ele vem, e o segundo
             * é o prazo que fontes_garente arma no cache. Descartá-los (como
             * antes) fazia o Passo 7 do brief não ter prazo nenhum. */
            else if (strcmp(key, "dataset") == 0)   subst(&cur->dataset, val);
            else if (strcmp(key, "ttl") == 0) {
                char *fim;
                long v = strtol(val, &fim, 10);
                if (fim != val && *fim == '\0') cur->ttl = (int)v;
            }
        }
    }
    fclose(f);

    if (count == 0) {
        free(arr);
        return -1;
    }
    *fontes = arr;
    *n = count;
    return 0;

falha:
    fclose(f);
    fontes_libera(arr, count);
    return -1;
}

const fonte_dados *fontes_obtem(const fonte_dados *fontes, size_t n,
                                const char *id)
{
    size_t i;

    if (!fontes || !id) return NULL;
    for (i = 0; i < n; i++) {
        if (fontes[i].id && strcmp(fontes[i].id, id) == 0) return &fontes[i];
    }
    return NULL;
}

int fontes_baixa(const char *conf, const char *id, const char *sufixo,
                 const char *chave_cache, char **out, size_t *out_len)
{
    char *buf = NULL;
    size_t len = 0;
    char *url = NULL;
    const char *chave;
    char tmp[600];
    fonte_dados *fontes = NULL;
    size_t n = 0;
    const fonte_dados *f;
    int rc, ttl;

    if (!out || !out_len || !id || !*id) return -1;
    *out = NULL;
    *out_len = 0;
    chave = (chave_cache && *chave_cache) ? chave_cache : id;

    /* 1. cache válido? Sem rede e sem abrir o .conf — é o caminho de todo
     *    servidor que já rodou --atualizar. */
    if (cache_tem(chave) && cache_le(chave, &buf, &len) == 0 && len > 0) {
        *out = buf;
        *out_len = len;
        return 0;
    }
    free(buf);
    buf = NULL;
    len = 0;

    /* 2. não está no cache (ou expirou): a url mora no .conf, nunca no
     *    código. O `url` é malloc'd porque vive no vetor, que é liberado no
     *    fim, e o download pode levar dezenas de segundos. */
    if (!conf) return -1;
    if (fontes_ler(conf, &fontes, &n) != 0) return -1;
    f = fontes_obtem(fontes, n, id);
    if (!f || !f->url_zip) {
        fontes_libera(fontes, n);
        return -1;
    }
    ttl = f->ttl;
    if ((size_t)snprintf(tmp, sizeof tmp, "%s%s", f->url_zip,
                         sufixo ? sufixo : "") >= sizeof tmp) {
        fontes_libera(fontes, n);
        return -1;
    }
    url = dup_str(tmp);
    fontes_libera(fontes, n);
    if (!url) return -1;

    /* 3. baixa e grava — com UMA segunda tentativa.
     *
     * A ingestão do IBGE leva minutos e passa por oito requisições grandes ao
     * SIDRA. O teto do curl é de 120 s por requisição, e um servidor público
     * que responde em 1,2 s na primeira vez e estoura os 120 s na segunda é
     * uma coisa que acontece; sem repetição, o trabalho de dois minutos ia
     * embora por um tempo esgotado e a pessoa tinha que rodar tudo de novo do
     * zero. Uma repetição não conserta rede caída — ela conserta o transiente,
     * que é o caso comum.
     *
     * Só uma: duas falhas seguidas são rede, não sorte, e insistir é só
     * demorar mais para dizer a mesma coisa. */
    rc = http_get(url, &buf, &len);
    if (rc != 0 || !buf || len == 0) {
        free(buf);
        buf = NULL;
        len = 0;
        fprintf(stderr, "  o download falhou; tentando mais uma vez...\n");
        rc = http_get(url, &buf, &len);
    }
    free(url);
    if (rc != 0 || !buf || len == 0) {
        free(buf);
        return rc != 0 ? rc : -1;
    }
    if (cache_grava(chave, buf, len) != 0) {
        /* Perder o cache é mais barato que perder a ingestão inteira. */
        fprintf(stderr, "aviso: não gravei a fonte %s no cache\n", chave);
    } else if (ttl != CACHE_TTL_NENHUM) {
        cache_set_ttl(chave, ttl);
    }
    *out = buf;
    *out_len = len;
    return 0;
}

int fontes_garente(const char *arquivo_conf, const char *id,
                   char **out, size_t *out_len)
{
    return fontes_baixa(arquivo_conf, id, NULL, id, out, out_len);
}

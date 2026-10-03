/* src/cache.c -- cache dos downloads brutos em data/cache/ (ver cache.h).
 *
 * Nome do arquivo = chave com tudo que não for [A-Za-z0-9._-] trocado por
 * '_'. Isso resolve de uma vez o nome de arquivo a partir de uma URL (que tem
 * '/', '?', '&', ':' e '%') e a impossibilidade de escapar do diretório: não
 * sobra nenhum separador, então '..' vira '__' e nada de subdiretório é
 * criado. Chaves que passariam do limite de nome do sistema são encurtadas
 * com um resumo do valor inteiro, não só com um prefixo — senão duas URLs
 * longas que só diferem no fim cairiam no mesmo arquivo e uma leitura pegaria
 * o download da outra.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "cache.h"

/* Tamanho máximo do nome gerado (o resto do caminho é fixo). */
#define CACHE_NOME_MAX 96
/* Sufixo do arquivo temporário da escrita. */
#define CACHE_SUFIX_TMP ".tmp"
/* Sufixo do arquivo de lado com o TTL. */
#define CACHE_SUFIX_TTL ".ttl"

/* ------------------------------------------------------------------ */
/* utilitários de caminho                                              */
/* ------------------------------------------------------------------ */

/* mkdir -p. Devolve 0 se o diretório existe no fim. */
static int cria_diretorio(const char *path)
{
    size_t n = strlen(path);
    char *buf;
    size_t i;

    buf = malloc(n + 1);
    if (!buf)
        return -1;
    memcpy(buf, path, n + 1);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char guardado = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0777) != 0 && errno != EEXIST) {
                free(buf);
                return -1;
            }
            buf[i] = guardado;
        }
    }
    free(buf);
    return 0;
}

/* FNV-1a de 64 bits: só para distinguir chaves longas que o prefixo não
 * distingue. Não é criptografia e não precisa ser. */
static unsigned long long hash_fnv1a(const char *s)
{
    unsigned long long h = 1469598103934665603ULL;

    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static char *nome_do_arquivo(const char *chave)
{
    size_t n = strlen(chave), i;
    char *nome, *saida = NULL;

    if (n == 0)
        return NULL;

    nome = malloc(n + 32);
    if (!nome)
        return NULL;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)chave[i];
        /* Só alfanumérico, '_' e '-'. O ponto fica de fora de propósito: é
         * ele que forma "..", e a chave é uma string que veio de fora. */
        int seguro = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '-';
        nome[i] = seguro ? (char)c : '_';
    }
    nome[n] = '\0';

    if (n <= CACHE_NOME_MAX) {
        saida = nome;
    } else {
        /* prefixo + '_' + 16 hex do resumo da chave inteira */
        size_t prefixo = CACHE_NOME_MAX - 17;
        unsigned long long h = hash_fnv1a(chave);

        saida = malloc(CACHE_NOME_MAX + 1);
        if (saida) {
            memcpy(saida, nome, prefixo);
            snprintf(saida + prefixo, CACHE_NOME_MAX + 1 - prefixo, "_%016llx",
                     h);
        }
        free(nome);
    }
    return saida;
}

char *cache_path(const char *chave)
{
    char *nome, *caminho;
    size_t n;

    if (!chave || !*chave)
        return NULL;
    nome = nome_do_arquivo(chave);
    if (!nome)
        return NULL;
    n = strlen(CACHE_DIR) + 1 + strlen(nome) + 1;
    caminho = malloc(n);
    if (caminho)
        snprintf(caminho, n, "%s/%s", CACHE_DIR, nome);
    free(nome);
    return caminho;
}

/* malloc'd: "<caminho><sufixo>". */
static char *caminho_com_sufixo(const char *caminho, const char *sufixo)
{
    size_t n = strlen(caminho) + strlen(sufixo) + 1;
    char *p = malloc(n);

    if (p)
        snprintf(p, n, "%s%s", caminho, sufixo);
    return p;
}

/* ------------------------------------------------------------------ */
/* interface                                                           */
/* ------------------------------------------------------------------ */

int cache_tem(const char *chave)
{
    char *path = cache_path(chave);
    struct stat st;
    long idade, ttl;
    int tem;

    if (!path)
        return 0;
    tem = 0;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        idade = (long)time(NULL) - (long)st.st_mtime;
        if (idade < 0)
            idade = 0;                  /* relógio andou para trás */
        ttl = cache_ttl(chave);
        /* TTL -1 (imutável) nunca expira; com TTL, só vale dentro do prazo */
        tem = (ttl == CACHE_TTL_NENHUM || idade <= ttl);
    }
    free(path);
    return tem;
}

int cache_grava(const char *chave, const void *buf, size_t len)
{
    char *path, *tmp;
    FILE *f;
    int rc = -1;

    path = cache_path(chave);
    if (!path)
        return -1;
    if (cria_diretorio(CACHE_DIR) != 0) {
        free(path);
        return -1;
    }
    /* Escreve num ".tmp" e renomeia: se o processo morrer no meio da
     * escrita (download cortado, disco cheio), o arquivo final continua
     * sendo o download inteiro antigo, e não um pedaço. */
    tmp = caminho_com_sufixo(path, CACHE_SUFIX_TMP);
    if (!tmp) {
        free(path);
        return -1;
    }
    f = fopen(tmp, "wb");
    if (!f) {
        free(tmp);
        free(path);
        return -1;
    }
    if (len == 0 || fwrite(buf, 1, len, f) == len)
        rc = 0;
    if (fclose(f) != 0)
        rc = -1;
    if (rc == 0 && rename(tmp, path) != 0)
        rc = -1;
    if (rc != 0)
        remove(tmp);                   /* não deixa lixo para trás */
    free(tmp);
    free(path);
    return rc;
}

int cache_le(const char *chave, char **out, size_t *out_len)
{
    char *path;
    FILE *f;
    struct stat st;
    char *buf;
    size_t lido = 0;

    if (!out || !out_len)
        return -1;
    *out = NULL;
    *out_len = 0;

    path = cache_path(chave);
    if (!path)
        return -1;
    f = fopen(path, "rb");
    free(path);
    if (!f)
        return -1;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 0 || (unsigned long long)st.st_size > SIZE_MAX - 1) {
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)st.st_size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    while (lido < (size_t)st.st_size) {
        size_t k = fread(buf + lido, 1, (size_t)st.st_size - lido, f);
        if (k == 0)
            break;
        lido += k;
    }
    fclose(f);
    if (lido != (size_t)st.st_size) {   /* arquivo encolheu no meio da leitura */
        free(buf);
        return -1;
    }
    buf[lido] = '\0';                  /* convenience: dá para usar como texto */
    *out = buf;
    *out_len = lido;
    return 0;
}

int cache_idade_segundos(const char *chave)
{
    char *path = cache_path(chave);
    struct stat st;
    long idade;

    if (!path)
        return -1;
    idade = -1;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        idade = (long)time(NULL) - (long)st.st_mtime;
        if (idade < 0)
            idade = 0;
    }
    free(path);
    return idade;
}

int cache_ttl(const char *chave)
{
    char *path = cache_path(chave);
    char *ttl_path;
    FILE *f;
    int ttl = CACHE_TTL_NENHUM;

    if (!path)
        return CACHE_TTL_NENHUM;
    ttl_path = caminho_com_sufixo(path, CACHE_SUFIX_TTL);
    free(path);
    if (!ttl_path)
        return CACHE_TTL_NENHUM;
    f = fopen(ttl_path, "r");
    free(ttl_path);
    if (!f)
        return CACHE_TTL_NENHUM;        /* sem arquivo de lado: imutável */
    if (fscanf(f, "%d", &ttl) != 1 || ttl < 0)
        ttl = CACHE_TTL_NENHUM;
    fclose(f);
    return ttl;
}

void cache_set_ttl(const char *chave, int segundos)
{
    char *path = cache_path(chave);
    char *ttl_path;
    FILE *f;

    if (!path)
        return;
    ttl_path = caminho_com_sufixo(path, CACHE_SUFIX_TTL);
    free(path);
    if (!ttl_path)
        return;

    if (segundos < 0) {                 /* CACHE_TTL_NENHUM: limpa o prazo */
        remove(ttl_path);
        free(ttl_path);
        return;
    }
    if (cria_diretorio(CACHE_DIR) != 0) {
        free(ttl_path);
        return;
    }
    f = fopen(ttl_path, "w");
    if (!f) {
        free(ttl_path);
        return;
    }
    fprintf(f, "%d\n", segundos);
    fclose(f);
    free(ttl_path);
}

/* src/http_client.c -- download pela CLI do curl (ver http_client.h).
 *
 * popen() monta a chamada; a URL vai entre aspas simples, que no shell não
 * expandem nada (nem &, nem ?, nem $, nem crase) e fecham o problema do '&'
 * da query string do IBGE. Dentro delas só a própria aspa simples precisa
 * escapar, virada em '\'' (fecha, aspa escapada, aspa).
 *
 * A leitura é com fread direto do pipe, byte a byte, num buffer que dobra
 * até caber: o corpo é o download cru (o CNES são ZIP de 231 MB em tarefa
 * futura) e não pode passar por nenhuma conversão de texto.
 *
 * Nenhuma escrita em disco no caminho do download: o pipe do popen() já é um
 * destino temporário anônimo, sem nome de arquivo, sem limpeza e sem
 * corrida com outro download. Quem quiser gravar em disco passa o corpo para
 * cache_grava(), que faz a troca por rename.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "http_client.h"

/* Chão do buffer de leitura: 64 KiB já evita dezenas de milhares de chamadas
 * a fread num arquivo grande. */
#define HTTP_CLIENT_BLOCO 65536u

/* Só http e https. Isso fecha a injeção de opção: mesmo com a URL entre aspas
 * simples, uma url que começasse com '-' seria lida pelo curl como opção
 * (--help, -o /caminho). O "--" antes da url é a segunda camada: com ele, o
 * curl para de ler opções. */
static int url_aceita(const char *url)
{
    const unsigned char *p = (const unsigned char *)url;

    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
        return 0;
    for (; *p; p++) {
        if (*p < 0x20 || *p == 0x7F)
            return 0;              /* controle e DEL não entram na linha de comando */
    }
    return 1;
}

/* "curl -sL --fail --max-time <s> -- '<url>'". malloc'd ou NULL. */
static char *monta_comando(const char *url)
{
    static const char prefixo[] = "curl -sL --fail --max-time ";
    char numero[32];
    size_t n_url = strlen(url), i, n;
    char *cmd;

    snprintf(numero, sizeof numero, "%d -- '", HTTP_CLIENT_MAX_TIME_S);
    /* prefixo + número + 4 bytes por caractere de url (aspa simples vira
     * quatro) + aspas de fechamento + '\0' */
    cmd = malloc(sizeof prefixo + sizeof numero + 4 * n_url + 2);
    if (!cmd)
        return NULL;
    memcpy(cmd, prefixo, sizeof prefixo - 1);
    n = sizeof prefixo - 1;
    memcpy(cmd + n, numero, strlen(numero));
    n += strlen(numero);
    for (i = 0; i < n_url; i++) {
        if (url[i] == '\'') {
            cmd[n++] = '\'';
            cmd[n++] = '\\';
            cmd[n++] = '\'';
            cmd[n++] = '\'';
        } else {
            cmd[n++] = url[i];
        }
    }
    cmd[n++] = '\'';
    cmd[n] = '\0';
    return cmd;
}

int http_get(const char *url, char **out, size_t *out_len)
{
    char *cmd, *buf;
    FILE *f;
    size_t cap = HTTP_CLIENT_BLOCO, n = 0;
    int estado, codigo, leitura_ok;

    if (!out || !out_len)
        return -1;
    *out = NULL;
    *out_len = 0;
    if (!url || !*url || !url_aceita(url))
        return -1;

    cmd = monta_comando(url);
    if (!cmd)
        return -1;
    f = popen(cmd, "r");
    free(cmd);
    if (!f)
        return -1;                  /* curl não está no PATH, ou falta fork */

    buf = malloc(cap);
    if (!buf) {
        pclose(f);
        return -1;
    }
    for (;;) {
        size_t k = fread(buf + n, 1, cap - n - 1, f);
        if (k == 0)
            break;
        n += k;
        if (n + 1 >= cap) {
            size_t nova = cap * 2;
            char *maior;

            if (nova > HTTP_CLIENT_LIMITE_BYTES) {
                pclose(f);
                free(buf);
                return -1;          /* corpo maior que o limite: recusa */
            }
            maior = realloc(buf, nova);
            if (!maior) {
                pclose(f);
                free(buf);
                return -1;
            }
            buf = maior;
            cap = nova;
        }
    }
    leitura_ok = !ferror(f);
    estado = pclose(f);              /* fecha o pipe e espera o curl */
    if (!leitura_ok || estado == -1) {
        free(buf);
        return -1;
    }
    if (WIFSIGNALED(estado) || !WIFEXITED(estado)) {
        free(buf);
        return -1;                  /* o curl morreu por sinal */
    }
    codigo = WEXITSTATUS(estado);
    if (codigo != 0) {
        free(buf);
        return codigo;              /* 6 = DNS, 7 = conexão, 22 = HTTP, 28 = tempo */
    }
    buf[n] = '\0';
    *out = buf;
    *out_len = n;
    return 0;
}

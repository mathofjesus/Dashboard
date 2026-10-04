#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/zip.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

void test_zip(void)
{
    /* Teste com arquivo inexistente */
    zip_entrada *lista = NULL;
    size_t nlista = 0;
    int r = zip_lista("/tmp/opencode/nao_existe_12345.zip", &lista, &nlista);
    T_ASSERT(r != 0, "arquivo inexistente deve falhar");
    if (lista) {
        free(lista);
        lista = NULL;
    }
    nlista = 0;

    /* Teste com t.zip - 2 entradas deflate */
    r = zip_lista("/tmp/opencode/t.zip", &lista, &nlista);
    T_ASSERT(r == 0, "zip_lista deve funcionar");
    T_ASSERT(nlista == 2, "t.zip deve ter 2 entradas");
    if (nlista >= 2) {
        /* nomes podem vir em ordem do CD; verificamos existência */
        int tem_a = 0, tem_b = 0;
        for (size_t i = 0; i < nlista; i++) {
            if (strcmp(lista[i].nome, "a.txt") == 0) tem_a = 1;
            if (strcmp(lista[i].nome, "b.txt") == 0) tem_b = 1;
        }
        T_ASSERT(tem_a && tem_b, "nomes corretos para t.zip");
    }

    /* Extrair de t.zip */
    {
        zip_arq z;
        memset(&z, 0, sizeof(z));
        r = zip_abre("/tmp/opencode/t.zip", &z);
        T_ASSERT(r == 0, "zip_abre t.zip");
        T_ASSERT(z.n == nlista, "n consistente");
        /* extrai cada um */
        for (size_t i = 0; i < z.n; i++) {
            size_t outlen = 0;
            char *buf = zip_extrai(&z, i, &outlen);
            T_ASSERT(buf != NULL, "zip_extrai retorna buffer");
            if (buf) {
                if (strcmp(z.entradas[i].nome, "a.txt") == 0) {
                    T_ASSERT(outlen == 5, "a.txt tamanho");
                    T_ASSERT(memcmp(buf, "hello", 5) == 0, "a.txt conteudo");
                }
                if (strcmp(z.entradas[i].nome, "b.txt") == 0) {
                    T_ASSERT(outlen == 5, "b.txt tamanho");
                    T_ASSERT(memcmp(buf, "world", 5) == 0, "b.txt conteudo");
                }
                free(buf);
            }
        }
        zip_fecha(&z);
    }

    if (lista) {
        for (size_t i = 0; i < nlista; i++) {
            free(lista[i].nome);
        }
        free(lista);
        lista = NULL;
        nlista = 0;
    }

    /* Teste com t_stored.zip - método stored */
    r = zip_lista("/tmp/opencode/t_stored.zip", &lista, &nlista);
    T_ASSERT(r == 0, "zip_lista stored");
    T_ASSERT(nlista == 1, "t_stored.zip tem 1 entrada");
    if (nlista >= 1) {
        T_ASSERT(strcmp(lista[0].nome, "s.txt") == 0, "nome s.txt");
        T_ASSERT(lista[0].metodo == 0, "método stored");
    }

    {
        zip_arq z;
        memset(&z, 0, sizeof(z));
        r = zip_abre("/tmp/opencode/t_stored.zip", &z);
        T_ASSERT(r == 0, "zip_abre stored");
        if (z.n >= 1) {
            size_t outlen = 0;
            char *buf = zip_extrai(&z, 1, &outlen); /* testando idx 1-based? */
            /* wait - contrato diz idx, vamos testar com 0-based conforme uso normal */
        }
        zip_fecha(&z);
    }

    /* Melhor testar stored com índice correto */
    {
        zip_arq z;
        memset(&z, 0, sizeof(z));
        r = zip_abre("/tmp/opencode/t_stored.zip", &z);
        T_ASSERT(r == 0, "zip_abre stored2");
        if (z.n >= 1) {
            size_t outlen = 0;
            char *buf = zip_extrai(&z, 0, &outlen);
            T_ASSERT(buf != NULL, "extrai stored");
            if (buf) {
                T_ASSERT(outlen == 6 || outlen == 7 || outlen == strlen("stored"), "tamanho stored");
                T_ASSERT(memcmp(buf, "stored", 6) == 0, "conteudo stored");
                free(buf);
            }
        }
        zip_fecha(&z);
    }

    if (lista) {
        for (size_t i = 0; i < nlista; i++) {
            free(lista[i].nome);
        }
        free(lista);
        lista = NULL;
    }

    /* Validar arquivo real Leitos_csv_2026.zip se existir */
    r = zip_lista("/tmp/opencode/leitos.zip", &lista, &nlista);
    if (r == 0) {
        /* busca Leitos_2026.csv */
        int achou = 0;
        for (size_t i = 0; i < nlista; i++) {
            if (strstr(lista[i].nome, "Leitos_2026.csv") != NULL) {
                T_ASSERT(lista[i].tam_descomprimido == 19218438ULL, "tam_descomprimido Leitos");
                achou = 1;
                break;
            }
        }
        T_ASSERT(achou == 1, "encontrou Leitos_2026.csv");
        if (lista) {
            for (size_t i = 0; i < nlista; i++) {
                free(lista[i].nome);
            }
            free(lista);
            lista = NULL;
        }
    }
}

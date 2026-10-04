#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/csv.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

void test_csv(void)
{
    /* Detecção */
    {
        const char *buf = "a;b;c\n1;2;3\n";
        int d = csv_detecta_delimitador(buf, strlen(buf));
        T_ASSERT(d == ';', "detecta ;");
    }
    {
        const char *buf = "a,b,c\n1,2,3\n";
        int d = csv_detecta_delimitador(buf, strlen(buf));
        T_ASSERT(d == ',', "detecta ,");
    }

    /* Aspas */
    {
        const char *buf = "x;\"a;b\";z\n";
        int d = csv_detecta_delimitador(buf, strlen(buf));
        T_ASSERT(d == ';', "detecta ; aspas");
        csv *c = csv_abre_mem(buf, strlen(buf), (char)d, CSV_UTF8);
        T_ASSERT(c != NULL, "abre csv");
        T_ASSERT(csv_num_linhas(c) >= 1, "tem linhas");
        char out[64];
        int r = csv_campo(c, 0, 0, out, sizeof(out));
        T_ASSERT(r == 0, "campo 0,0");
        T_ASSERT(strcmp(out, "x") == 0, "campo x");
        r = csv_campo(c, 0, 1, out, sizeof(out));
        T_ASSERT(r == 0, "campo 0,1");
        T_ASSERT(strcmp(out, "a;b") == 0, "campo a;b com aspas");
        r = csv_campo(c, 0, 2, out, sizeof(out));
        T_ASSERT(r == 0, "campo 0,2");
        T_ASSERT(strcmp(out, "z") == 0, "campo z");
        csv_fecha(c);
    }

    /* Aspas mistas do Leitos: linha COMP;REGIAO;UF;CO_IBGE;MUNICIPIO sem aspas em CO_IBGE */
    {
        const char *buf = "COMP;REGIAO;UF;CO_IBGE;MUNICIPIO\n1;Norte;AC;120001;Acrelândia\n";
        int d = csv_detecta_delimitador(buf, strlen(buf));
        T_ASSERT(d == ';', "detecta ; leitos");
        csv *c = csv_abre_mem(buf, strlen(buf), (char)d, CSV_UTF8);
        T_ASSERT(c != NULL, "abre csv leitos");
        T_ASSERT(csv_num_linhas(c) == 2, "2 linhas");
        T_ASSERT(csv_cabecalho(c, 0) != NULL && strcmp(csv_cabecalho(c, 0), "COMP") == 0, "cabecalho");
        T_ASSERT(csv_cabecalho(c, 3) != NULL && strcmp(csv_cabecalho(c, 3), "CO_IBGE") == 0, "cabecalho CO_IBGE");
        char out[64];
        int r = csv_campo(c, 1, 3, out, sizeof(out));
        T_ASSERT(r == 0, "linha 1 col 3");
        T_ASSERT(strcmp(out, "120001") == 0, "CO_IBGE sem aspas");
        csv_fecha(c);
    }

    /* Aspas duplas escapadas */
    {
        /* "" dentro de campo → " ; "" como campo vazio → "" */
        const char *buf1 = "a;\"\"\"\";b\n"; /* a; ""; b ? "a;\"\"\"\";b" significa a, """" (aspas duplas dentro aspas) */
        /* "" dentro de campo entre aspas: no CSV, "" representa " */
        const char *buf2 = "a;\"ab\"\"cd\";b\n"; /* ab"cd */
        const char *buf3 = "\"\"\n"; /* campo vazio com aspas */
        csv *c;

        c = csv_abre_mem(buf2, strlen(buf2), ';', CSV_UTF8);
        T_ASSERT(c != NULL, "buf2");
        char out[64];
        csv_campo(c, 0, 1, out, sizeof(out));
        T_ASSERT(strcmp(out, "ab\"cd") == 0, "aspas escapadas dentro");
        csv_fecha(c);

        c = csv_abre_mem(buf3, strlen(buf3), ',', CSV_UTF8); /* delimitador irrelevante */
        T_ASSERT(c != NULL, "buf3");
        csv_campo(c, 0, 0, out, sizeof(out));
        T_ASSERT(strcmp(out, "") == 0, "campo vazio entre aspas vira vazio");
        csv_fecha(c);
    }

    /* CRLF e linha final sem \n */
    {
        const char *buf = "a;b\r\nc;d\r\n"; /* CRLF */
        csv *c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
        T_ASSERT(c != NULL, "crlf");
        T_ASSERT(csv_num_linhas(c) >= 2, "duas linhas");
        csv_fecha(c);
    }
    {
        const char *buf = "a;b\nc;d"; /* sem \n final */
        csv *c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
        T_ASSERT(c != NULL, "sem nl final");
        T_ASSERT(csv_num_linhas(c) == 2, "duas linhas mesmo sem nl");
        char out[8];
        csv_campo(c, 1, 1, out, sizeof(out));
        T_ASSERT(strcmp(out, "d") == 0, "ultimo campo");
        csv_fecha(c);
    }

    /* UTF-8 preservado */
    {
        const char *buf = "caf\xe9\n"; /* 'é' em UTF-8 */
        csv *c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
        T_ASSERT(c != NULL, "utf8");
        char out[16];
        csv_campo(c, 0, 0, out, sizeof(out));
        T_ASSERT(memcmp(out, "caf\xc3\xa9", 5) == 0 || strcmp(out, "caf\xe9") == 0, "utf8 preservado");
        csv_fecha(c);
    }

    /* Coluna fora da faixa */
    {
        const char *buf = "a;b\n";
        csv *c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
        T_ASSERT(c != NULL, "fora faixa");
        char out[8] = "X";
        int r = csv_campo(c, 0, 5, out, sizeof(out));
        T_ASSERT(r == -1, "retorna -1 fora faixa");
        T_ASSERT(strcmp(out, "X") == 0, "não escreve fora faixa");
        csv_fecha(c);
    }

    /* csv_vazio */
    {
        const char *buf = "a;\"\";b;;c\n";
        csv *c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
        T_ASSERT(c != NULL, "vazio");
        T_ASSERT(csv_vazio(c, 0, 0) == 0, "a não vazio");
        T_ASSERT(csv_vazio(c, 0, 1) == 1, "campo vazio entre aspas");
        T_ASSERT(csv_vazio(c, 0, 3) == 1, "campo ausente");
        csv_fecha(c);
    }
}

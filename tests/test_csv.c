/* tests/test_csv.c — o parser de CSV, a transcodificação por dataset e a
 * linha-em-JSON.
 *
 * Os dois achados que este arquivo segura:
 *
 *   I8  `csv_abre_mem` recebia `csv_codificacao` e fazia `(void)cod`: a
 *       transcodificação ISO-8859-1 → UTF-8 da spec §9 não existia para
 *       nenhum dataset. O teste passa os MESMOS bytes em `CSV_ISO88591` e em
 *       `CSV_UTF8` e exige dois resultados diferentes — se `cod` voltar a ser
 *       ignorado, o primeiro assert falha.
 *   I9  `csv_linha_json` devolvia a constante "{}", que é JSON válido e
 *       vazio. O teste exige o conteúdo, e exige que um campo com aspa ou
 *       contrabarra saia escapado, que é o que fecha o documento.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../src/csv.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

static void test_detecta(void)
{
    const char *buf;

    buf = "a;b;c\n1;2;3\n";
    T_ASSERT(csv_detecta_delimitador(buf, strlen(buf)) == ';', "detecta ;");

    buf = "a,b,c\n1,2,3\n";
    T_ASSERT(csv_detecta_delimitador(buf, strlen(buf)) == ',', "detecta ,");

    /* O ';' dentro de aspas não conta: contá-lo daria delimitador errado num
     * CSV cuja primeira linha tem mais vírgulas do que pontos e vírgulas. */
    buf = "x;\"a;b\";z\n";
    T_ASSERT(csv_detecta_delimitador(buf, strlen(buf)) == ';', "delimitador dentro de aspas não conta");

    /* Uma coluna só: a escolha é indiferente, mas não pode mudar de uma
     * chamada para outra. */
    buf = "so_uma_coluna\n";
    T_ASSERT(csv_detecta_delimitador(buf, strlen(buf)) ==
             csv_detecta_delimitador(buf, strlen(buf)), "uma coluna é estável");

    T_ASSERT(csv_detecta_delimitador(NULL, 0) == ',', "entrada vazia");
    T_ASSERT(csv_detecta_delimitador("", 0) == ',', "string vazia");
}

static void test_aspas(void)
{
    const char *buf;
    csv *c;
    char out[64];

    /* Campo com o delimitador dentro. */
    buf = "x;\"a;b\";z\n";
    c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
    T_ASSERT(c != NULL, "abre csv");
    T_ASSERT(csv_num_linhas(c) >= 1, "tem linhas");
    T_ASSERT(csv_campo(c, 0, 0, out, sizeof out) == 0 && strcmp(out, "x") == 0,
             "campo 0,0");
    T_ASSERT(csv_campo(c, 0, 1, out, sizeof out) == 0 && strcmp(out, "a;b") == 0,
             "campo a;b com aspas");
    T_ASSERT(csv_campo(c, 0, 2, out, sizeof out) == 0 && strcmp(out, "z") == 0,
             "campo z");
    csv_fecha(c);

    /* Aspas duplas escapadas. */
    buf = "a;\"ab\"\"cd\";b\n";           /* o campo do meio é ab"cd */
    c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
    T_ASSERT(c != NULL, "abre com escape");
    csv_campo(c, 0, 1, out, sizeof out);
    T_ASSERT(strcmp(out, "ab\"cd") == 0, "aspas escapadas dentro");
    csv_fecha(c);

    /* Campo vazio entre aspas. */
    buf = "\"\"\n";
    c = csv_abre_mem(buf, strlen(buf), ',', CSV_UTF8);
    T_ASSERT(c != NULL, "abre vazio entre aspas");
    csv_campo(c, 0, 0, out, sizeof out);
    T_ASSERT(strcmp(out, "") == 0, "campo vazio entre aspas vira vazio");
    csv_fecha(c);
}

static void test_cabecalho(void)
{
    /* Aspas mistas do Leitos: a linha de cabeçalho vem sem aspas e a
     * CO_IBGE também, que é exatamente o formato que a spec §4.3 descreve. */
    const char *buf =
        "COMP;REGIAO;UF;CO_IBGE;MUNICIPIO\n"
        "1;Norte;AC;120001;Acrel\xc3\xa2ndia\n";
    csv *c;
    char out[64];

    c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
    T_ASSERT(c != NULL, "abre csv leitos");
    T_ASSERT(csv_num_linhas(c) == 2, "2 linhas");
    T_ASSERT(csv_cabecalho(c, 0) && strcmp(csv_cabecalho(c, 0), "COMP") == 0,
             "primeiro cabeçalho");
    T_ASSERT(csv_cabecalho(c, 3) && strcmp(csv_cabecalho(c, 3), "CO_IBGE") == 0,
             "cabeçalho CO_IBGE");
    T_ASSERT(csv_indice_coluna(c, "MUNICIPIO") == 4, "índice por nome");
    T_ASSERT(csv_indice_coluna(c, "NAO_EXISTE") == -1, "nome ausente é -1");
    T_ASSERT(csv_campo(c, 1, 3, out, sizeof out) == 0 && strcmp(out, "120001") == 0,
             "CO_IBGE sem aspas");
    T_ASSERT(csv_campo(c, 1, 4, out, sizeof out) == 0, "município");
    T_ASSERT(strcmp(out, "Acrel\xc3\xa2ndia") == 0, "município com acento");
    T_ASSERT(csv_cabecalho(NULL, 0) == NULL, "cabeçalho de NULL");
    T_ASSERT(csv_cabecalho(c, 99) == NULL, "cabeçalho fora da faixa");
    csv_fecha(c);
}

/* I8. Os mesmos bytes, dois resultados — que é a transcodificação dirigida da
 * spec §9. O byte 0xE9 é "é" em Latin-1 e o começo de um caractere UTF-8, e é
 * por isso que "converter tudo" não serve: o resultado UTF-8 viraria "Ã©". */
static void test_codificacao(void)
{
    /* "POLICINICA;3509502\nPOLIC\xc3\x8dNICA;..." em Latin-1: Í = 0xCD. */
    const char latin1[] = "UNIDADE;CO_IBGE;NOME\nHOSPITAL;3509502;POLIC\xcdNICA\n";
    /* O mesmo texto já em UTF-8: Í = 0xC3 0x8D. */
    const char utf8[]   = "UNIDADE;CO_IBGE;NOME\nHOSPITAL;3509502;POLIC\xc3\x8dNICA\n";
    csv *c;
    char out[64];

    /* Declarado Latin-1: "POLICÍNICA" com acento de verdade. */
    c = csv_abre_mem(latin1, strlen(latin1), ';', CSV_ISO88591);
    T_ASSERT(c != NULL, "abre Latin-1");
    T_ASSERT(csv_campo(c, 1, 2, out, sizeof out) == 0, "campo convertido");
    T_ASSERT(strcmp(out, "POLIC\xc3\x8dNICA") == 0,
             "Latin-1 vira UTF-8 com acento");
    csv_fecha(c);

    /* Declarado UTF-8: os bytes passam intactos. Se alguém converter mesmo
     * assim, "Í" vira "Ã­" — daí o teste. */
    c = csv_abre_mem(utf8, strlen(utf8), ';', CSV_UTF8);
    T_ASSERT(c != NULL, "abre UTF-8");
    T_ASSERT(csv_campo(c, 1, 2, out, sizeof out) == 0, "campo intacto");
    T_ASSERT(strcmp(out, "POLIC\xc3\x8dNICA") == 0,
             "UTF-8 não é reconvertido");
    csv_fecha(c);

    /* E o caminho oposto: os mesmos bytes 0xC3 0x8D lidos como Latin-1 dão
     * "Ã­", que é o erro simétrico — e é por isso que a codificação é
     * declarada por dataset e não adivinhada. */
    c = csv_abre_mem(utf8, strlen(utf8), ';', CSV_ISO88591);
    T_ASSERT(c != NULL, "abre UTF-8 como Latin-1");
    csv_campo(c, 1, 2, out, sizeof out);
    T_ASSERT(strcmp(out, "POLIC\xc3\x83\xc2\x8dNICA") == 0,
             "declarar errado produz latin1 de latin1, e é visível");
    csv_fecha(c);

    /* Um CSV só com ASCII é indistinguível entre os dois: a conversão não
     * pode mudar nada, ou quebraria um dataset que não pediu nada. */
    {
        const char ascii[] = "a;b\nc;d\n";
        c = csv_abre_mem(ascii, strlen(ascii), ';', CSV_ISO88591);
        T_ASSERT(c != NULL, "abre ASCII");
        T_ASSERT(csv_campo(c, 1, 1, out, sizeof out) == 0 && strcmp(out, "d") == 0,
                 "ASCII sobrevive à conversão");
        csv_fecha(c);
    }
}

static void test_linhas(void)
{
    const char *buf;
    csv *c;
    char out[64];

    /* CRLF e linha final sem \n. */
    buf = "a;b\r\nc;d\r\n";
    c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
    T_ASSERT(c != NULL, "crlf");
    T_ASSERT(csv_num_linhas(c) >= 2, "duas linhas");
    csv_fecha(c);

    buf = "a;b\nc;d";
    c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
    T_ASSERT(c != NULL, "sem \n final");
    T_ASSERT(csv_num_linhas(c) == 2, "duas linhas mesmo sem \n");
    T_ASSERT(csv_campo(c, 1, 1, out, sizeof out) == 0 && strcmp(out, "d") == 0,
             "último campo");
    csv_fecha(c);

    /* Defensivos: csv_abre_mem(NULL) e csv_fecha(NULL) não podem quebrar. */
    T_ASSERT(csv_abre_mem(NULL, 0, ';', CSV_UTF8) == NULL, "abre(NULL) é NULL");
    csv_fecha(NULL);
    T_ASSERT(csv_num_linhas(NULL) == 0, "num_linhas(NULL)");
    T_ASSERT(csv_indice_coluna(NULL, "x") == -1, "indice_coluna(NULL)");
    T_ASSERT(csv_int(NULL, 0, 0, 7) == 7, "int(NULL) devolve o padrão");
    T_ASSERT(csv_double(NULL, 0, 0, 1.5) == 1.5, "double(NULL) devolve o padrão");
    T_ASSERT(csv_vazio(NULL, 0, 0) == 1, "vazio(NULL) é vazio");
}

/* I9. Uma linha tem que virar a linha. */
static void test_linha_json(void)
{
    const char *buf =
        "CO_IBGE;NOME;LEITOS\n"
        "3509502;Hospital de S\xc3\xa3o Paulo;120\n"
        "1100015;Hospital do Bairro;0\n";
    const size_t cols[] = { 0, 1, 2 };
    csv *c;
    char *j;
    char esperado[512];

    c = csv_abre_mem(buf, strlen(buf), ';', CSV_UTF8);
    T_ASSERT(c != NULL, "abre para o JSON");

    /* A coluna de código vira "id" e os números saem como número, para o
     * front ordenar sem reconverter. */
    snprintf(esperado, sizeof esperado,
             "{\"CO_IBGE\":3509502,\"NOME\":\"Hospital de S\xc3\xa3o Paulo\","
             "\"LEITOS\":120,\"id\":\"3509502\"}");
    j = csv_linha_json(c, 1, cols, 3, 0);
    T_ASSERT(j != NULL, "linha JSON");
    if (strcmp(j, esperado) != 0)
        fprintf(stderr, "  esperado: %s\n  obtido:   %s\n", esperado, j);
    T_ASSERT(strcmp(j, esperado) == 0, "conteúdo do objeto");
    T_ASSERT(strstr(j, "\"{}\"") == NULL, "não devolve mais o stub");
    free(j);

    /* Texto que não é inteiro sai como string escapada. */
    j = csv_linha_json(c, 2, cols, 3, 0);
    T_ASSERT(j != NULL, "segunda linha");
    T_ASSERT(strstr(j, "\"LEITOS\":0") != NULL, "zero é número, não ausente");
    T_ASSERT(strstr(j, "\"NOME\":\"Hospital do Bairro\"") != NULL,
             "texto não numérico sai como string");
    free(j);

    /* Campo com aspa e contrabarra: tem que sair escapado, ou o objeto deixa
     * de ser JSON e o navegador acusa erro de sintaxe em vez de mostrar o
     * nome do hospital. */
    {
        const char *com_aspas =
            "CO_IBGE;NOME\n"
            "1100015;Hospital \"Bom\" \\-fundado\n";
        csv *d = csv_abre_mem(com_aspas, strlen(com_aspas), ';', CSV_UTF8);
        T_ASSERT(d != NULL, "abre com aspas");
        j = csv_linha_json(d, 1, cols, 2, 0);
        T_ASSERT(j != NULL, "linha com aspas");
        T_ASSERT(strstr(j, "\\\"Bom\\\"") != NULL, "aspas escapadas");
        T_ASSERT(strstr(j, "\\\\") != NULL, "contrabarra escapada");
        free(j);
        csv_fecha(d);
    }

    /* Coluna repetida e coluna inexistente não quebram nem duplicam o
     * objeto: a repetição vai para a última, como em qualquer objeto JSON. */
    j = csv_linha_json(c, 1, cols, 3, 2);
    T_ASSERT(j != NULL, "chave em outra coluna");
    T_ASSERT(strstr(j, "\"id\":\"120\"") != NULL, "chave da coluna indicada");
    free(j);

    /* Recusas: sem csv, sem colunas, ou linha fora da faixa. NULL é melhor
     * que "{}": o consumidor distingue "não deu" de "não tem". */
    T_ASSERT(csv_linha_json(NULL, 1, cols, 3, 0) == NULL, "csv NULL");
    T_ASSERT(csv_linha_json(c, 1, cols, 0, 0) == NULL, "nenhuma coluna");
    T_ASSERT(csv_linha_json(c, 1, NULL, 3, 0) == NULL, "lista de colunas NULL");
    T_ASSERT(csv_linha_json(c, 99, cols, 3, 0) == NULL, "linha fora da faixa");
    /* Índice de coluna além do que a linha tem é ignorado, não fatal. */
    {
        const size_t longe[] = { 0, 42, 1 };
        j = csv_linha_json(c, 1, longe, 3, 0);
        T_ASSERT(j != NULL, "coluna fora da faixa é ignorada");
        T_ASSERT(strstr(j, "\"NOME\"") != NULL, "o resto continua");
        free(j);
    }

    csv_fecha(c);
}

void test_csv(void)
{
    test_detecta();
    test_aspas();
    test_cabecalho();
    test_codificacao();
    test_linhas();
    test_linha_json();
}
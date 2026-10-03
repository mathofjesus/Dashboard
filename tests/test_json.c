/* tests/test_json.c -- Tarefa 2: parser JSON.
 *
 * Cobre o contrato de src/json.h: objeto simples, escapes (inclusive \uXXXX
 * com acento), número em ponto flutuante, lixo depois do documento, entrada
 * malformada, acessores com tipo errado, entrada grande (50.000 números) e
 * aninhamento fundo.
 *
 * Nada de rede aqui: o cliente HTTP tem suíte própria.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/json.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

/* json_parse() sempre quer um ponteiro + tamanho; Convenience para literais. */
static json_val *parse(const char *texto)
{
    return json_parse(texto, strlen(texto));
}

/* ------------------------------------------------------------------ */
/* objeto do brief                                                     */
/* ------------------------------------------------------------------ */

static void testa_objeto_basico(void)
{
    json_val *v = parse("{\"a\":1,\"b\":\"x\",\"c\":[1,2,null],\"d\":true}");

    T_ASSERT(v != NULL, "parse do objeto devolve raiz");
    T_ASSERT(json_tipo_de(v) == JSON_OBJ, "a raiz é objeto");
    T_ASSERT(json_num(json_obj_get(v, "a")) == 1.0, "campo a == 1");
    T_ASSERT(json_tipo_de(json_obj_get(v, "a")) == JSON_NUM, "campo a é número");
    T_ASSERT(strcmp(json_str(json_obj_get(v, "b")), "x") == 0, "campo b == \"x\"");
    T_ASSERT(json_tipo_de(json_obj_get(v, "b")) == JSON_STR, "campo b é string");
    T_ASSERT(json_arr_len(json_obj_get(v, "c")) == 3, "campo c tem 3 elementos");
    T_ASSERT(json_tipo_de(json_obj_get(v, "c")) == JSON_ARR, "campo c é array");
    T_ASSERT(json_tipo_de(json_arr_get(json_obj_get(v, "c"), 2)) == JSON_NULO,
             "terceiro elemento de c é null");
    T_ASSERT(json_obj_get(v, "c") != NULL && json_arr_get(json_obj_get(v, "c"), 2)
             != NULL, "o elemento null do array existe (não é NULL do C)");
    T_ASSERT(json_bool(json_obj_get(v, "d")) == 1, "campo d é true");
    T_ASSERT(json_tipo_de(json_obj_get(v, "d")) == JSON_BOOL, "campo d é bool");
    T_ASSERT(json_num(json_obj_get(v, "a")) == 1.0, "leitura repetida de a");
    T_ASSERT(json_arr_get(json_obj_get(v, "c"), 0) != NULL &&
             json_num(json_arr_get(json_obj_get(v, "c"), 0)) == 1.0,
             "primeiro elemento de c == 1");
    T_ASSERT(json_num(json_arr_get(json_obj_get(v, "c"), 1)) == 2.0,
             "segundo elemento de c == 2");

    json_free(v);
}

/* ------------------------------------------------------------------ */
/* escapes e \uXXXX                                                     */
/* ------------------------------------------------------------------ */

static void testa_escapes(void)
{
    /* O valor precisa conter as três coisas: aspas, barra e newline. */
    json_val *v = parse("{\"s\":\"a\\\"b\\\\c\\n\"}");
    const char *s;

    T_ASSERT(v != NULL, "parse com escapes devolve raiz");
    s = json_str(json_obj_get(v, "s"));
    T_ASSERT(s != NULL, "campo s é string");
    T_ASSERT(strlen(s) == 6, "escape encurta: 9 bytes viram 6");
    T_ASSERT(s[0] == 'a' && s[1] == '"' && s[2] == 'b' && s[3] == '\\' &&
             s[4] == 'c' && s[5] == '\n',
             "valor tem aspas, barra invertida e newline");
    json_free(v);

    /* \/ \b \f \r \t */
    v = parse("[\"\\/\",\"\\b\",\"\\f\",\"\\r\",\"\\t\"]");
    T_ASSERT(v != NULL, "parse dos escapes simples devolve raiz");
    T_ASSERT(strcmp(json_str(json_arr_get(v, 0)), "/") == 0, "\\/ vira /");
    T_ASSERT(strcmp(json_str(json_arr_get(v, 1)), "\b") == 0, "\\b vira backspace");
    T_ASSERT(strcmp(json_str(json_arr_get(v, 2)), "\f") == 0, "\\f vira form feed");
    T_ASSERT(strcmp(json_str(json_arr_get(v, 3)), "\r") == 0, "\\r vira CR");
    T_ASSERT(strcmp(json_str(json_arr_get(v, 4)), "\t") == 0, "\\t vira tab");
    json_free(v);
}

static void testa_unicode_utf8(void)
{
    json_val *v = parse("{\"cidade\":\"S\\u00e3o Paulo\",\"uf\":\"SP\"}");
    const char *s;

    T_ASSERT(v != NULL, "parse de \\u devolve raiz");
    s = json_str(json_obj_get(v, "cidade"));
    T_ASSERT(strcmp(s, "S\xC3\xA3o Paulo") == 0,
             "\\u00e3 vira o byte UTF-8 de 'ã'");
    T_ASSERT(strcmp(json_str(json_obj_get(v, "uf")), "SP") == 0,
             "campo vizinho não é afetado pelo \\u");
    json_free(v);

    /* acento escrito direto no arquivo também tem de sair intacto */
    v = parse("{\"m\":\"Mogi das Cruzes\", \"c\":\"Ceará\"}");
    T_ASSERT(v != NULL, "parse de acento cru devolve raiz");
    T_ASSERT(strcmp(json_str(json_obj_get(v, "m")), "Mogi das Cruzes") == 0,
             "string sem escape volta igual");
    T_ASSERT(strcmp(json_str(json_obj_get(v, "c")), "Cear\xC3\xA1") == 0,
             "acento cru (2 bytes UTF-8) passa direto");
    json_free(v);

    /* par substituto: U+1F600 vira 4 bytes */
    v = parse("[\"\\ud83d\\ude00\"]");
    T_ASSERT(v != NULL, "parse de par substituto devolve raiz");
    T_ASSERT(strcmp(json_str(json_arr_get(v, 0)), "\xF0\x9F\x98\x80") == 0,
             "par substituto vira o emoji de 4 bytes");
    json_free(v);

    /* substituto sozinho não pode gerar lixo em UTF-8 */
    v = parse("[\"\\ud83d\"]");
    T_ASSERT(v != NULL, "substituto sozinho é aceito");
    T_ASSERT(strcmp(json_str(json_arr_get(v, 0)), "\xEF\xBF\xBD") == 0,
             "substituto sozinho vira U+FFFD");
    json_free(v);

    /* o Byte Order Mark do começo é ignorado */
    v = parse("\xEF\xBB\xBF[1]");
    T_ASSERT(v != NULL, "BOM antes do documento não quebra o parse");
    T_ASSERT(json_arr_len(v) == 1, "BOM não conta como elemento");
    json_free(v);
}

/* ------------------------------------------------------------------ */
/* números                                                             */
/* ------------------------------------------------------------------ */

static void testa_numeros(void)
{
    json_val *v;

    /* número do brief: população do município em ponto flutuante */
    v = parse("{\"v\":1757338.0}");
    T_ASSERT(v != NULL, "parse do número do brief devolve raiz");
    T_ASSERT(json_num(json_obj_get(v, "v")) == 1757338.0,
             "1757338.0 == 1757338.0");
    json_free(v);

    /* "+0" fica de fora de propósito: sinal de mais não é JSON (testado em
     * testa_malformado). */
    v = parse("[0,-1,1.5,-2.25,3e2,1E-3,1234567890123]");
    T_ASSERT(v != NULL, "parse da lista de números devolve raiz");
    T_ASSERT(json_arr_len(v) == 7, "7 números");
    T_ASSERT(json_num(json_arr_get(v, 0)) == 0.0, "0");
    T_ASSERT(json_num(json_arr_get(v, 1)) == -1.0, "-1");
    T_ASSERT(json_num(json_arr_get(v, 2)) == 1.5, "1.5");
    T_ASSERT(json_num(json_arr_get(v, 3)) == -2.25, "-2.25");
    T_ASSERT(json_num(json_arr_get(v, 4)) == 300.0, "3e2 == 300");
    T_ASSERT(json_num(json_arr_get(v, 5)) == 0.001, "1E-3 == 0.001");
    T_ASSERT(json_num(json_arr_get(v, 6)) == 1234567890123.0, "inteiro grande");
    json_free(v);
}

/* ------------------------------------------------------------------ */
/* lixo depois do documento e entrada malformada                       */
/* ------------------------------------------------------------------ */

static void testa_lixo_depois(void)
{
    json_val *v;

    v = parse("{\"a\":1} isto aqui é lixo");
    T_ASSERT(v != NULL, "lixo depois do JSON válido não derruba o parse");
    T_ASSERT(json_num(json_obj_get(v, "a")) == 1.0, "a raiz veio inteira");
    json_free(v);

    v = parse("[1,2,3]\x00e lixo");
    T_ASSERT(v != NULL, "lixo binário depois do JSON válido é ignorado");
    T_ASSERT(json_arr_len(v) == 3, "os 3 elementos estão lá");
    json_free(v);
}

static void testa_malformado(void)
{
    /* Entradas que têm de devolver NULL, sem estourar nada. */
    T_ASSERT(json_parse("{\"a\":", 5) == NULL, "objeto cortado devolve NULL");
    T_ASSERT(json_parse("", 0) == NULL, "entrada vazia devolve NULL");
    T_ASSERT(json_parse(NULL, 0) == NULL, "entrada NULL devolve NULL");
    T_ASSERT(json_parse("   ", 3) == NULL, "só espaços devolve NULL");
    T_ASSERT(json_parse("lixo", 4) == NULL, "texto solto devolve NULL");
    T_ASSERT(parse("{") == NULL, "abre objeto e acaba devolve NULL");
    T_ASSERT(parse("{\"a\"}") == NULL, "campo sem valor devolve NULL");
    T_ASSERT(parse("{\"a\":1,}") == NULL, "vírgula sobrando devolve NULL");
    T_ASSERT(parse("[1,2,]") == NULL, "vírgula no fim do array devolve NULL");
    T_ASSERT(parse("[1 2]") == NULL, "número sem vírgula devolve NULL");
    T_ASSERT(parse("\"aberta") == NULL, "string sem fecha devolve NULL");
    T_ASSERT(parse("{\"a\" 1}") == NULL, "dois pontos faltando devolve NULL");
    T_ASSERT(parse("{a:1}") == NULL, "chave sem aspas devolve NULL");
    T_ASSERT(parse("{\"a\":01x}") == NULL, "número com lixo devolve NULL");
    T_ASSERT(parse("{\"a\":+1}") == NULL, "sinal de mais não é JSON");
    T_ASSERT(parse("[1,2") == NULL, "array sem fecha devolve NULL");
    T_ASSERT(parse("{\"a\":tru}") == NULL, "literal incompleto devolve NULL");
    T_ASSERT(parse("{\"a\":\"\\x\"}") == NULL, "escape desconhecido devolve NULL");
    T_ASSERT(parse("{\"a\":\"\\u00zz\"}") == NULL, "\\u com hex errado devolve NULL");
    T_ASSERT(parse("{\"a\":nan}") == NULL, "nan não é JSON");
}

/* ------------------------------------------------------------------ */
/* acessores com tipo errado e NULL                                    */
/* ------------------------------------------------------------------ */

static void testa_acessores_defensivos(void)
{
    json_val *v = parse("{\"n\":1,\"s\":\"x\",\"a\":[1,2],\"o\":{\"k\":1}}");
    const json_val *n = json_obj_get(v, "n");
    const json_val *s = json_obj_get(v, "s");
    const json_val *a = json_obj_get(v, "a");
    const json_val *o = json_obj_get(v, "o");

    T_ASSERT(json_obj_get(v, "inexistente") == NULL, "campo ausente devolve NULL");
    T_ASSERT(json_obj_get(n, "n") == NULL, "número não tem campo");
    T_ASSERT(json_obj_get(NULL, "n") == NULL, "obj_get(NULL) devolve NULL");

    T_ASSERT(strcmp(json_str(n), "") == 0, "json_str de número devolve \"\"");
    T_ASSERT(strcmp(json_str(NULL), "") == 0, "json_str(NULL) devolve \"\"");
    T_ASSERT(json_num(s) == 0.0, "json_num de string devolve 0.0");
    T_ASSERT(json_num(NULL) == 0.0, "json_num(NULL) devolve 0.0");
    T_ASSERT(json_bool(s) == 0, "json_bool de string devolve 0");
    T_ASSERT(json_bool(NULL) == 0, "json_bool(NULL) devolve 0");
    T_ASSERT(json_bool(n) == 0, "1 não é true");
    T_ASSERT(json_bool(v) == 0, "objeto não é true");

    T_ASSERT(json_arr_len(s) == 0, "json_arr_len de string devolve 0");
    T_ASSERT(json_arr_len(NULL) == 0, "json_arr_len(NULL) devolve 0");
    T_ASSERT(json_arr_len(v) == 0, "json_arr_len de objeto devolve 0");
    T_ASSERT(json_arr_get(s, 0) == NULL, "json_arr_get de string devolve NULL");
    T_ASSERT(json_arr_get(a, 2) == NULL, "índice fora do array devolve NULL");
    T_ASSERT(json_arr_get(NULL, 0) == NULL, "json_arr_get(NULL) devolve NULL");

    /* duplicata: a primeira ocorrência ganha */
    T_ASSERT(json_num(json_obj_get(o, "k")) == 1.0, "campo do objeto interno");
    T_ASSERT(json_str(json_obj_get(o, "k"))[0] == '\0',
             "json_str de número aninhado devolve \"\"");

    T_ASSERT(json_tipo_de(NULL) == JSON_NULO, "tipo de NULL é JSON_NULO");
    json_free(v);
    json_free(NULL);                 /* tem de ser inofensivo */

    v = parse("[null,true,false]");
    T_ASSERT(json_tipo_de(json_arr_get(v, 0)) == JSON_NULO, "null é JSON_NULO");
    T_ASSERT(json_tipo_de(json_arr_get(v, 1)) == JSON_BOOL, "true é JSON_BOOL");
    T_ASSERT(json_tipo_de(json_arr_get(v, 2)) == JSON_BOOL, "false é JSON_BOOL");
    T_ASSERT(json_bool(json_arr_get(v, 2)) == 0, "false devolve 0");
    json_free(v);
}

/* ------------------------------------------------------------------ */
/* aninhamento                                                         */
/* ------------------------------------------------------------------ */

static void testa_aninhamento(void)
{
    int i, prof = 50;
    size_t cap = (size_t)prof * 8 + 32;
    char *buf = malloc(cap);
    json_val *v;

    T_ASSERT(buf != NULL, "sem memória para o teste de aninhamento");
    for (i = 0; i < prof; i++)
        buf[i] = '[';
    buf[prof] = '"';
    buf[prof + 1] = 'x';
    buf[prof + 2] = '"';
    for (i = 0; i < prof; i++)
        buf[prof + 3 + i] = ']';
    buf[prof * 2 + 3] = '\0';

    v = parse(buf);
    T_ASSERT(v != NULL, "50 níveis de array são aceitos");
    T_ASSERT(json_str(json_arr_get(v, 0)) != NULL, "a string chegou lá dentro");
    json_free(v);
    free(buf);
}

/* Aninhamento absurdo precisa devolver NULL, não estourar a pilha. */
static void testa_aninhamento_profundo_demais(void)
{
    int i, prof = 200000;
    size_t cap = (size_t)prof + 16;
    char *buf = malloc(cap);

    T_ASSERT(buf != NULL, "sem memória para o teste de profundidade");
    for (i = 0; i < prof; i++)
        buf[i] = '[';
    buf[prof] = '\0';

    T_ASSERT(parse(buf) == NULL,
             "200.000 '[' seguidos devolvem NULL em vez de estourar a pilha");
    free(buf);
}

/* ------------------------------------------------------------------ */
/* entrada grande                                                     */
/* ------------------------------------------------------------------ */

/* Monta "[n1,n2,...,n50000]" e confere que o array inteiro foi lido. É o
 * caminho que a lista de localidades (2,4 MB) e o GeoJSON (5,78 MB) do IBGE
 * vão exercitar em tarefas futuras. */
static void testa_entrada_grande(void)
{
    const int N = 50000;
    size_t cap = (size_t)N * 8 + 16;
    char *buf = malloc(cap);
    json_val *v;
    double soma = 0.0;
    int i;
    size_t pos, n;

    T_ASSERT(buf != NULL, "sem memória para o array de 50.000 números");
    pos = 0;
    buf[pos++] = '[';
    for (i = 0; i < N; i++)
        pos += (size_t)snprintf(buf + pos, cap - pos, "%s%d", i ? "," : "", i);
    buf[pos++] = ']';
    buf[pos] = '\0';

    T_ASSERT(pos < cap, "o array montado coube no buffer");

    v = json_parse(buf, pos);
    T_ASSERT(v != NULL, "50.000 números parseiam");
    T_ASSERT(json_tipo_de(v) == JSON_ARR, "a raiz é array");
    n = json_arr_len(v);
    T_ASSERT(n == (size_t)N, "o array tem 50.000 elementos");

    /* confere a contagem E o conteúdo dos dois extremos e do meio */
    T_ASSERT(json_num(json_arr_get(v, 0)) == 0.0, "primeiro elemento é 0");
    T_ASSERT(json_num(json_arr_get(v, N / 2)) == (double)(N / 2),
             "elemento do meio tem o valor certo");
    T_ASSERT(json_num(json_arr_get(v, N - 1)) == (double)(N - 1),
             "último elemento é 49999");
    T_ASSERT(json_arr_get(v, N) == NULL, "índice N é fora");

    /* soma confere: a soma dos números que esperamos, índice a índice */
    for (i = 0; i < N; i++)
        soma += json_num(json_arr_get(v, (size_t)i));
    T_ASSERT(soma == (double)N * (N - 1) / 2.0, "a soma dos elementos confere");

    json_free(v);
    free(buf);
}

/* Mesmo teste com buffer sem '\0' no fim, como vem de cache_le(). */
static void testa_entrada_grande_sem_terminador(void)
{
    const int N = 20000;
    size_t cap = (size_t)N * 8 + 16;
    char *buf = malloc(cap);
    json_val *v;
    size_t pos, n;
    int i;

    T_ASSERT(buf != NULL, "sem memória para o array sem terminador");
    pos = 0;
    buf[pos++] = '[';
    for (i = 0; i < N; i++)
        pos += (size_t)snprintf(buf + pos, cap - pos, "%s%d", i ? "," : "", i);
    buf[pos++] = ']';

    /* pos bytes válidos, mas o buffer tem lixo (nada de '\0') depois */
    v = json_parse(buf, pos);
    T_ASSERT(v != NULL, "buffer sem terminador é lido pelo tamanho");
    n = json_arr_len(v);
    T_ASSERT(n == (size_t)N, "20.000 elementos sem terminador");
    T_ASSERT(json_num(json_arr_get(v, N - 1)) == (double)(N - 1),
             "o último número sem terminador");
    json_free(v);
    free(buf);
}

/* Objeto com muitos campos no mesmo nível: exercita o crescimento da tabela
 * de campos do objeto. */
static void testa_objeto_com_muitas_chaves(void)
{
    const int N = 5000;
    size_t cap = (size_t)N * 24 + 64;
    char *buf = malloc(cap);
    json_val *v;
    size_t pos;
    int i;

    T_ASSERT(buf != NULL, "sem memória para o objeto grande");
    pos = 0;
    buf[pos++] = '{';
    for (i = 0; i < N; i++)
        pos += (size_t)snprintf(buf + pos, cap - pos, "\"k%d\":%d,", i, i);
    pos--;                          /* tira a vírgula final */
    buf[pos++] = '}';
    buf[pos] = '\0';

    v = parse(buf);
    T_ASSERT(v != NULL, "objeto com 5.000 chaves parseia");
    T_ASSERT(json_num(json_obj_get(v, "k0")) == 0.0, "primeira chave");
    T_ASSERT(json_num(json_obj_get(v, "k4999")) == 4999.0, "última chave");
    T_ASSERT(json_num(json_obj_get(v, "k2500")) == 2500.0, "chave do meio");
    json_free(v);
    free(buf);
}

/* Muitos ciclos de parse/free: sem valgrind isto pega no mínimo uma falha de
 * segmentação, um double free e qualquer crescimento óbvio de tempo. */
static void testa_ida_e_volta(void)
{
    int i;
    for (i = 0; i < 2000; i++) {
        json_val *v = parse("{\"a\":[1,{\"b\":\"ç\"}],\"c\":null}");
        const json_val *a, *segundo, *interno;

        T_ASSERT(v != NULL, "parse repetido devolve raiz");
        a = json_obj_get(v, "a");
        T_ASSERT(json_arr_len(a) == 2, "o array interno tem 2 elementos");
        segundo = json_arr_get(a, 1);
        interno = json_obj_get(segundo, "b");
        T_ASSERT(strcmp(json_str(interno), "ç") == 0,
                 "o acento sobrevive ao ciclo");
        json_free(v);
    }
}

/* ------------------------------------------------------------------ */

void test_json(void)
{
    printf("testa_objeto_basico\n");              testa_objeto_basico();
    printf("testa_escapes\n");                   testa_escapes();
    printf("testa_unicode_utf8\n");              testa_unicode_utf8();
    printf("testa_numeros\n");                   testa_numeros();
    printf("testa_lixo_depois\n");               testa_lixo_depois();
    printf("testa_malformado\n");                testa_malformado();
    printf("testa_acessores_defensivos\n");      testa_acessores_defensivos();
    printf("testa_aninhamento\n");               testa_aninhamento();
    printf("testa_aninhamento_profundo_demais\n");
    testa_aninhamento_profundo_demais();
    printf("testa_entrada_grande\n");            testa_entrada_grande();
    printf("testa_entrada_grande_sem_terminador\n");
    testa_entrada_grande_sem_terminador();
    printf("testa_objeto_com_muitas_chaves\n");  testa_objeto_com_muitas_chaves();
    printf("testa_ida_e_volta\n");              testa_ida_e_volta();
}

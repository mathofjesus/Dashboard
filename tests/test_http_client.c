/* tests/test_http_client.c -- Tarefa 2: cliente HTTP via curl CLI.
 *
 * Esta suíte PRECISA DE INTERNET: ela baixa a API do IBGE. Por isso ela não
 * roda no `make test` padrão e sim no `make test-rede`, que chama o binário
 * com --rede. Motivo: `make test` é o portão de todo mundo e não pode
 * depender da internet nem ficar instável por causa de rede.
 *
 * Ver src/http_client.h para o contrato e para o motivo da URL ir entre
 * aspas simples na linha de comando.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/http_client.h"
#include "../src/json.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

/* Endpoint do passo 11 do brief: lista de municípios, 2,4 MB. */
#define URL_IBGE "https://servicodados.ibge.gov.br/api/v1/localidades/municipios"
/* .invalid é reservado pela RFC 2606 e não resolve em nenhum DNS. */
#define URL_INVALIDA "https://exemplo-inexistente-xyz.invalid/x"

/* ------------------------------------------------------------------ */

/* O brief: > 2.000.000 bytes e começando com [{"id":1100015. */
static void testa_get_ibge(void)
{
    char *body = NULL;
    size_t len = 0;
    int rc;

    rc = http_get(URL_IBGE, &body, &len);
    T_ASSERT(rc == 0, "http_get da URL do IBGE devolve 0 (veja rc acima)");
    T_ASSERT(body != NULL, "corpo não é NULL");
    T_ASSERT(len > 2000000, "o corpo tem mais de 2.000.000 bytes");
    printf("      (%zu bytes, começa com: %.40s)\n", len, body);
    T_ASSERT(memcmp(body, "[{\"id\":1100015", 14) == 0,
             "o corpo começa com [{\"id\":1100015");
    free(body);
}

/* O mesmo download já veio na mão: aproveitar para provar que o parser
 * segura o dado real do IBGE (5570 municípios), que é o que a Tarefa 5 vai
 * fazer com ele. */
static void testa_get_ibge_parseado(void)
{
    char *body = NULL;
    size_t len = 0;
    json_val *v;
    size_t n, i;
    int com_acentos = 0;

    T_ASSERT(http_get(URL_IBGE, &body, &len) == 0, "segundo download do IBGE");
    v = json_parse(body, len);
    T_ASSERT(v != NULL, "o JSON de 2,4 MB do IBGE parseia");
    T_ASSERT(json_tipo_de(v) == JSON_ARR, "a raiz é um array");
    n = json_arr_len(v);
    /* A conta exata de municípios muda quando o IBGE cria um município novo
     * (a spec deste projeto diz 5570; em 2026-10 são 5571), então o teste
     * confere a ordem de grandeza e imprime a contagem do dia. */
    T_ASSERT(n >= 5500, "uns 5.500 municípios, na ordem de grandeza da spec");
    printf("      (%zu municípios nesta resposta)\n", n);
    T_ASSERT(json_num(json_obj_get(json_arr_get(v, 0), "id")) == 1100015,
             "o primeiro município é o id 1100015");
    for (i = 0; i < n; i++) {
        const char *nome = json_str(json_obj_get(json_arr_get(v, i), "nome"));
        if (nome[0] == '\0')
            continue;
        if (strchr(nome, 0xC3) != NULL) {  /* byte de acento em UTF-8 */
            com_acentos++;
            break;
        }
    }
    T_ASSERT(com_acentos > 0,
             "tem município com acento no nome (UTF-8 decodificado)");
    json_free(v);
    free(body);
}

/* O brief: host inexistente devolve != 0. */
static void testa_dominio_inexistente(void)
{
    char *body = (char *)0x1;
    size_t len = 12345;
    int rc = http_get(URL_INVALIDA, &body, &len);

    T_ASSERT(rc != 0, "host inexistente devolve != 0 (curl sai com 6)");
    T_ASSERT(body == NULL, "corpo fica NULL no erro");
    T_ASSERT(len == 0, "len fica 0 no erro");
    printf("      (curl devolveu %d)\n", rc);
}

/* Se a URL entrasse na linha de comando sem aspas, o shell cortaria o
 * comando no '&': o curl baixaria só ".../x?a=1" (falhando com 6) e o
 * "b=2" solto entraria como um comando que devolve 0 — o status sazdo seria
 * 0 e http_get diria que deu certo. Então este teste falha exatamente
 * quando a citação some. */
static void testa_url_com_ampersand(void)
{
    char *body = (char *)0x1;
    size_t len = 12345;
    int rc = http_get("https://exemplo-inexistente-xyz.invalid/x?a=1&b=2",
                      &body, &len);

    T_ASSERT(rc != 0,
             "URL com '&' não pode virar dois comandos (falha sem aspas)");
    T_ASSERT(body == NULL, "corpo fica NULL");
    T_ASSERT(len == 0, "len fica 0");
}

/* Mesma defesa com o resto dos metacaracteres do shell. O arquivo
 * /tmp/opencode/injetado_pelo_teste não pode existir depois disto. */
static void testa_url_com_metacaracteres(void)
{
    char *body = (char *)0x1;
    size_t len = 12345;
    int rc;

    remove("/tmp/opencode/injetado_pelo_teste");
    rc = http_get("https://exemplo.invalid/a;touch /tmp/opencode/injetado_pelo_teste",
                  &body, &len);
    T_ASSERT(rc != 0, "URL com ';' devolve != 0");
    T_ASSERT(access("/tmp/opencode/injetado_pelo_teste", F_OK) != 0,
             "o ';' da URL não virou comando do shell");

    rc = http_get("https://exemplo.invalid/$(touch /tmp/opencode/injetado_pelo_teste)",
                  &body, &len);
    T_ASSERT(rc != 0, "URL com '$()' devolve != 0");
    T_ASSERT(access("/tmp/opencode/injetado_pelo_teste", F_OK) != 0,
             "o '$()' da URL não virou comando do shell");

    rc = http_get("https://exemplo.invalid/a`touch /tmp/opencode/injetado_pelo_teste`",
                  &body, &len);
    T_ASSERT(rc != 0, "URL com crase devolve != 0");
    T_ASSERT(access("/tmp/opencode/injetado_pelo_teste", F_OK) != 0,
             "a crase da URL não virou comando do shell");
    T_ASSERT(body == NULL, "corpo fica NULL");
    T_ASSERT(len == 0, "len fica 0");
}

static void testa_argumentos_invalidos(void)
{
    char *body = (char *)0x1;
    size_t len = 12345;

    T_ASSERT(http_get(NULL, &body, &len) == -1, "url NULL devolve -1");
    T_ASSERT(body == NULL && len == 0, "saída zerada no erro");
    T_ASSERT(http_get("https://exemplo.invalid/x", NULL, &len) == -1,
             "out NULL devolve -1");
    T_ASSERT(http_get("https://exemplo.invalid/x", &body, NULL) == -1,
             "out_len NULL devolve -1");

    /* caractere de controle e barra invertida não entram na linha de comando */
    T_ASSERT(http_get("https://exemplo.invalid/a\nb", &body, &len) == -1,
             "url com newline devolve -1");
    T_ASSERT(http_get("https://exemplo.invalid/a\tb", &body, &len) == -1,
             "url com tab devolve -1");
    T_ASSERT(http_get("", &body, &len) == -1, "url vazia devolve -1");

    /* injeção de opção: entre aspas simples a url continua sendo lida pelo
     * curl como argumento, então uma url que começa com '-' faria o curl
     * ler um -o/--help. Só http:// e https:// passam. */
    T_ASSERT(http_get("-o/tmp/opencode/injetado_pelo_teste", &body, &len) == -1,
             "url começando com '-' devolve -1");
    T_ASSERT(http_get("file:///etc/passwd", &body, &len) == -1,
             "esquema file:// devolve -1");
    T_ASSERT(http_get("ftp://exemplo.invalid/x", &body, &len) == -1,
             "esquema ftp:// devolve -1");
    T_ASSERT(body == NULL && len == 0, "saída zerada no erro");
}

/* ------------------------------------------------------------------ */

void test_http_client(void)
{
    printf("testa_get_ibge\n");                 testa_get_ibge();
    printf("testa_get_ibge_parseado\n");        testa_get_ibge_parseado();
    printf("testa_dominio_inexistente\n");      testa_dominio_inexistente();
    printf("testa_url_com_ampersand\n");        testa_url_com_ampersand();
    printf("testa_url_com_metacaracteres\n");   testa_url_com_metacaracteres();
    printf("testa_argumentos_invalidos\n");     testa_argumentos_invalidos();
}

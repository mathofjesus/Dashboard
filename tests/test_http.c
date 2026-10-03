/* tests/test_http.c -- Tarefa 1: parsing de request, Range e validacao de
 * caminho. Ver http_server.h para o contrato.
 *
 * As fixtures (/tmp/opencode/fake.bin e /tmp/opencode/fixture_web) sao
 * criadas e removidas pelo proprio teste: nada de rede, nada de web/.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/http_server.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

#define FIX_DIR  "/tmp/opencode"
#define FAKE_DIR FIX_DIR
#define FAKE_PATH FAKE_DIR "/fake.bin"
#define FAKE_SIZE 1000
#define WEB_FIX   FIX_DIR "/fixture_web"
#define JSON_FIX  WEB_FIX "/dados.json"
#define JSON_SIZE 2000
#define INDEX_FIX WEB_FIX "/index.html"
#define INDEX_TXT "<!doctype html><title>Dashboard</title><h1>oi</h1>\n"
#define CSS_TXT   "/* fixture */\nbody { color: #000; }\n"

/* ------------------------------------------------------------------ */
/* fixtures                                                            */
/* ------------------------------------------------------------------ */

static void escreve_arquivo(const char *path, const void *dados, size_t n)
{
    FILE *f = fopen(path, "wb");
    T_ASSERT(f != NULL, "nao consegui abrir arquivo de fixture");
    if (n > 0)
        T_ASSERT(fwrite(dados, 1, n, f) == n, "fwrite da fixture falhou");
    fclose(f);
}

/* fake.bin: 1000 bytes 0x00,0x01,...,0xE7 */
static void cria_fake(void)
{
    char buf[FAKE_SIZE];
    int i;
    for (i = 0; i < FAKE_SIZE; i++)
        buf[i] = (char)(i & 0xFF);
    escreve_arquivo(FAKE_PATH, buf, sizeof buf);
}

/* fixture_web/: index.html, styles.css e dados.json (2000 bytes, para o
 * teste de gzip ficar com conteudo grande o bastante). */
static void cria_web_fix(void)
{
    char *json;
    int i, n;

    remove(INDEX_FIX);                  /* sobras de execucao anterior */
    remove(WEB_FIX "/styles.css");
    remove(JSON_FIX);
    mkdir(WEB_FIX, 0777);               /* EEXIST aqui nao e erro */
    T_ASSERT(access(WEB_FIX, F_OK) == 0, "nao consegui criar fixture_web/");

    escreve_arquivo(INDEX_FIX, INDEX_TXT, strlen(INDEX_TXT));
    escreve_arquivo(WEB_FIX "/styles.css", CSS_TXT, strlen(CSS_TXT));

    json = malloc(JSON_SIZE + 1);
    T_ASSERT(json != NULL, "sem memoria para a fixture json");
    n = snprintf(json, JSON_SIZE + 1, "{\"dados\":\"");
    for (i = n; i < JSON_SIZE - 2; i++)
        json[i] = 'a' + (i % 26);
    json[JSON_SIZE - 2] = '"';
    json[JSON_SIZE - 1] = '}';
    json[JSON_SIZE] = '\0';
    escreve_arquivo(JSON_FIX, json, JSON_SIZE);
    free(json);
}

static void apaga_fixtures(void)
{
    remove(FAKE_PATH);
    remove(INDEX_FIX);
    remove(WEB_FIX "/styles.css");
    remove(JSON_FIX);
    rmdir(WEB_FIX);
}

/* ------------------------------------------------------------------ */
/* parsing de request                                                  */
/* ------------------------------------------------------------------ */

static void testa_parse_basico(void)
{
    http_request req;
    int rc = http_parse_request(
        "GET /api/x?a=1 HTTP/1.1\r\nRange: bytes=0-99\r\n\r\n", &req);
    T_ASSERT(rc == 0, "parse de request bem formado devolve 0");
    T_ASSERT(strcmp(req.method, "GET") == 0, "method == GET");
    T_ASSERT(strcmp(req.path, "api/x") == 0, "path == api/x (sem barra inicial)");
    T_ASSERT(strcmp(req.query, "a=1") == 0, "query == a=1");
    T_ASSERT(strcmp(req.range_header, "bytes=0-99") == 0,
             "range_header == bytes=0-99");
    T_ASSERT(req.accept_encoding == NULL,
             "accept_encoding == NULL quando o header nao veio");
}

static void testa_parse_sem_range(void)
{
    http_request req;
    int rc = http_parse_request(
        "GET /api/x HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n\r\n", &req);
    T_ASSERT(rc == 0, "parse sem Range devolve 0");
    T_ASSERT(req.range_header == NULL, "range_header == NULL quando ausente");
    T_ASSERT(strcmp(req.path, "api/x") == 0, "path == api/x");
    T_ASSERT(req.query != NULL, "query nao pode ser NULL");
    T_ASSERT(strcmp(req.query, "") == 0, "query == \"\" (string vazia, nao NULL)");
}

static void testa_parse_accept_encoding(void)
{
    http_request req;
    int rc = http_parse_request(
        "GET /x HTTP/1.1\r\naccept-ENCODING: gzip, deflate\r\n"
        "Range:  bytes=0-1  \r\n\r\n", &req);
    T_ASSERT(rc == 0, "parse com Accept-Encoding devolve 0");
    T_ASSERT(req.accept_encoding != NULL &&
             strcmp(req.accept_encoding, "gzip, deflate") == 0,
             "accept_encoding cru == gzip, deflate (nome case-insensitive)");
    T_ASSERT(strcmp(req.range_header, "bytes=0-1") == 0,
             "range_header sem espacos em volta (trim de OWS)");
}

static void testa_parse_malformado(void)
{
    http_request req;
    T_ASSERT(http_parse_request("GARBAGE", &req) == -1,
             "request sem verbo nem caminho e malformado");
    T_ASSERT(http_parse_request("GET nao-slash HTTP/1.1\r\n\r\n", &req) == -1,
             "alvo que nao comeca com '/' e malformado");
    T_ASSERT(http_parse_request("GET /x\r\n\r\n", &req) == -1,
             "request sem versao HTTP e malformado");
    T_ASSERT(http_parse_request(NULL, &req) == -1, "raw NULL e malformado");
    T_ASSERT(http_parse_request("GET /x HTTP/1.1\r\n\r\n", NULL) == -1,
             "out NULL e malformado");
}

static void testa_status_text(void)
{
    T_ASSERT(strcmp(http_status_text(200), "OK") == 0, "200 == OK");
    T_ASSERT(strcmp(http_status_text(206), "Partial Content") == 0, "206");
    T_ASSERT(strcmp(http_status_text(400), "Bad Request") == 0, "400");
    T_ASSERT(strcmp(http_status_text(403), "Forbidden") == 0, "403");
    T_ASSERT(strcmp(http_status_text(404), "Not Found") == 0, "404");
    T_ASSERT(strcmp(http_status_text(416), "Range Not Satisfiable") == 0, "416");
    T_ASSERT(strcmp(http_status_text(500), "Internal Server Error") == 0, "500");
    T_ASSERT(http_status_text(999) != NULL, "codigo desconhecido devolve texto");
}

static void testa_send_all_em_fd_fake(void)
{
    const char *buf = "0123456789";
    /* -1 nao e um fd valido: http_send_all tem de devolver -1, nao travar
     * nem reportar sucesso. */
    T_ASSERT(http_send_all(-1, buf, 10) == -1,
             "http_send_all em fd invalido devolve -1");
    T_ASSERT(http_send_all(1, "", 0) == 0, "n == 0 devolve 0 sem escrever");
}

/* ------------------------------------------------------------------ */
/* Range                                                               */
/* ------------------------------------------------------------------ */

static void testa_range_simples(void)
{
    http_response res;
    int rc;
    memset(&res, 0, sizeof res);
    rc = http_serve_range(FAKE_DIR, "fake.bin", "bytes=100-199", &res);
    T_ASSERT(rc == 206, "fatia dentro do arquivo devolve 206");
    T_ASSERT(res.status == 206, "res.status == 206");
    T_ASSERT(res.offset == 100, "res.offset == 100");
    T_ASSERT(res.length == 100, "res.length == 100");
    T_ASSERT(res.body == NULL, "res.body fica NULL: quem envia le do disco");
    T_ASSERT(res.filename != NULL && strcmp(res.filename, FAKE_PATH) == 0,
             "res.filename == /tmp/opencode/fake.bin");
    T_ASSERT(res.content_type != NULL && strstr(res.content_type, "charset") != NULL,
             "content_type sempre inclui charset");
}

static void testa_range_clamp_no_eof(void)
{
    http_response res;
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=900-2000", &res) == 206,
             "fatia alem do EOF mas comecando dentro devolve 206");
    T_ASSERT(res.offset == 900, "res.offset == 900");
    T_ASSERT(res.length == 100, "res.length clampeado em 100 (ate o EOF)");
}

static void testa_range_offset_alem_do_eof(void)
{
    http_response res;
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=5000-6000", &res) == 416,
             "offset comecando alem do EOF devolve 416");
    T_ASSERT(res.status == 416, "res.status == 416");
}

static void testa_range_suffix_e_aberto(void)
{
    http_response res;
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=-50", &res) == 206,
             "bytes=-50 (suffix) devolve 206");
    T_ASSERT(res.offset == 950, "suffix de 50 bytes comeca em 950");
    T_ASSERT(res.length == 50, "suffix devolve 50 bytes");

    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=990-", &res) == 206,
             "bytes=990- (aberto) devolve 206");
    T_ASSERT(res.offset == 990, "aberto comeca em 990");
    T_ASSERT(res.length == 10, "aberto devolve os 10 bytes ate o EOF");
}

static void testa_range_malformado_416(void)
{
    http_response res;
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=abc", &res) == 416,
             "Range nao numerico devolve 416");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "itens=1-2", &res) == 416,
             "unidade desconhecida devolve 416");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=200-100", &res) == 416,
             "invertido (200-100) devolve 416");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=0-99999", &res) == 206,
             "bytes=0-99999 clampea em 1000");
    T_ASSERT(res.length == FAKE_SIZE, "bytes=0-99999 devolve o arquivo inteiro");
}

/* ------------------------------------------------------------------ */
/* estatico e validacao de caminho                                     */
/* ------------------------------------------------------------------ */

static void req_de_get(http_request *req, const char *path)
{
    memset(req, 0, sizeof *req);
    req->method = "GET";
    req->path = path;
    req->query = "";
}

static void testa_static_200(void)
{
    http_request req;
    http_response res;

    req_de_get(&req, "index.html");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 200,
             "index.html existente devolve 200");
    T_ASSERT(res.body == NULL, "estatico usa res.filename, res.body == NULL");
    T_ASSERT(res.filename != NULL &&
             strcmp(res.filename, WEB_FIX "/index.html") == 0,
             "res.filename == <root>/index.html");
    T_ASSERT(res.offset == 0, "sem Range, res.offset == 0");
    T_ASSERT(res.length > 0, "res.length > 0 para arquivo com conteudo");
    T_ASSERT(res.content_type != NULL &&
             strncmp(res.content_type, "text/html", 9) == 0,
             "content_type comeca com text/html");
    T_ASSERT(strstr(res.content_type, "charset") != NULL,
             "content_type sempre inclui charset");

    req_de_get(&req, "styles.css");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 200,
             "styles.css existente devolve 200");
    T_ASSERT(res.content_type != NULL &&
             strncmp(res.content_type, "text/css", 8) == 0,
             "content_type comeca com text/css");

    /* caminho vazio (pedido de "/") cai em index.html */
    req_de_get(&req, "");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 200,
             "caminho vazio devolve index.html");
    T_ASSERT(res.filename != NULL &&
             strcmp(res.filename, WEB_FIX "/index.html") == 0,
             "caminho vazio mapeia para <root>/index.html");
}

static void testa_static_403(void)
{
    http_request req;
    http_response res;

    req_de_get(&req, "../Makefile");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 403,
             "caminho com .. devolve 403");

    req_de_get(&req, "../../etc/passwd");
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 403,
             ".. no meio do caminho devolve 403");

    req_de_get(&req, "/etc/passwd");
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 403,
             "caminho com barra inicial devolve 403");

    /* http_serve_range aplica a mesma regra */
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "../Makefile", "bytes=0-9", &res) == 403,
             "serve_range: caminho com .. devolve 403");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "/etc/passwd", "bytes=0-9", &res) == 403,
             "serve_range: barra inicial devolve 403");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "a/../../b", "bytes=0-9", &res) == 403,
             "serve_range: .. no meio devolve 403");
}

static void testa_404(void)
{
    http_request req;
    http_response res;

    req_de_get(&req, "nao-existe.html");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 404,
             "arquivo inexistente devolve 404");

    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "nao-existe.bin", "bytes=0-9", &res) == 404,
             "serve_range: arquivo inexistente devolve 404");
}

/* ------------------------------------------------------------------ */
/* http_handle_client: socketpair local, sem rede                      */
/* ------------------------------------------------------------------ */

#define RESPOSTA_MAX (1 << 20)

/* Manda req cru por um socketpair, roda http_handle_client no outro
 * extremo e devolve a resposta inteira (malloc, caller frees).
 * As fixtures sao minusculas, entao nao ha risco de encher o buffer do
 * socket e travar. */
static char *atende(const char *req)
{
    int sv[2];
    char *resposta = malloc(RESPOSTA_MAX);
    size_t n = 0;

    T_ASSERT(resposta != NULL, "sem memoria para a resposta");
    T_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair falhou");

    T_ASSERT(http_send_all(sv[0], req, strlen(req)) == 0, "escrita do request");
    shutdown(sv[0], SHUT_WR);           /* o servidor ve EOF depois do header */

    http_handle_client(sv[1], WEB_FIX); /* fecha sv[1] ao final */

    for (;;) {
        ssize_t k = read(sv[0], resposta + n, RESPOSTA_MAX - 1 - n);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            T_ASSERT(0, "leitura da resposta falhou");
        }
        if (k == 0)
            break;
        n += (size_t)k;
        T_ASSERT(n < RESPOSTA_MAX, "resposta maior que o buffer do teste");
    }
    resposta[n] = '\0';
    close(sv[0]);
    return resposta;
}

/* Confere a linha de status inteira, sem contar caracteres a mao. */
static void confere_status(const char *resp, const char *status_texto,
                           const char *msg)
{
    char esperado[64];
    snprintf(esperado, sizeof esperado, "HTTP/1.1 %s\r\n", status_texto);
    T_ASSERT(strncmp(resp, esperado, strlen(esperado)) == 0, msg);
}

static void testa_handle_client_200(void)
{
    char esperado[64];
    char *r = atende("GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n");
    snprintf(esperado, sizeof esperado, "Content-Length: %u\r\n",
             (unsigned)strlen(INDEX_TXT));
    confere_status(r, "200 OK", "resposta comeca com HTTP/1.1 200 OK");
    T_ASSERT(strstr(r, "Content-Type: text/html; charset=utf-8\r\n") != NULL,
             "Content-Type text/html com charset");
    T_ASSERT(strstr(r, esperado) != NULL, "Content-Length igual ao arquivo");
    T_ASSERT(strstr(r, "Connection: close\r\n") != NULL,
             "Connection: close (uma requisicao por conexao)");
    T_ASSERT(strstr(r, "<h1>oi</h1>") != NULL, "corpo da resposta");
    free(r);
}

static void testa_handle_client_403(void)
{
    char *r = atende("GET /../Makefile HTTP/1.1\r\nHost: x\r\n\r\n");
    confere_status(r, "403 Forbidden", "caminho com .. responde 403");
    free(r);

    r = atende("GET /a/../../Makefile HTTP/1.1\r\nHost: x\r\n\r\n");
    confere_status(r, "403 Forbidden", ".. no meio do caminho responde 403");
    free(r);
}

static void testa_handle_client_404_e_405(void)
{
    char *r = atende("GET /nao-existe HTTP/1.1\r\nHost: x\r\n\r\n");
    confere_status(r, "404 Not Found", "arquivo inexistente responde 404");
    free(r);

    r = atende("POST /index.html HTTP/1.1\r\nHost: x\r\n\r\n");
    confere_status(r, "405 Method Not Allowed",
                   "metodo diferente de GET responde 405");
    free(r);
}

static void testa_handle_client_206(void)
{
    char esperado[64];
    char *r = atende("GET /index.html HTTP/1.1\r\nHost: x\r\n"
                     "Range: bytes=5-9\r\n\r\n");
    snprintf(esperado, sizeof esperado, "Content-Range: bytes 5-9/%u\r\n",
             (unsigned)strlen(INDEX_TXT));
    confere_status(r, "206 Partial Content", "Range valido responde 206");
    T_ASSERT(strstr(r, esperado) != NULL,
             "Content-Range com posicao e tamanho total");
    T_ASSERT(strstr(r, "Content-Length: 5\r\n") != NULL,
             "Content-Length da fatia");
    free(r);

    r = atende("GET /index.html HTTP/1.1\r\nHost: x\r\n"
               "Range: bytes=9999-10000\r\n\r\n");
    confere_status(r, "416 Range Not Satisfiable",
                   "Range alem do EOF responde 416");
    free(r);
}

static void testa_handle_client_gzip(void)
{
    char *r = atende("GET /dados.json HTTP/1.1\r\nHost: x\r\n\r\n");
    confere_status(r, "200 OK", "json responde 200");
    T_ASSERT(strstr(r, "Content-Encoding: gzip\r\n") == NULL,
             "sem Accept-Encoding nao comprime");
    T_ASSERT(strstr(r, "Content-Length: 2000\r\n") != NULL,
             "json inteiro sem comprimir");
    free(r);

    r = atende("GET /dados.json HTTP/1.1\r\nHost: x\r\n"
               "Accept-Encoding: gzip, deflate\r\n\r\n");
    T_ASSERT(strstr(r, "Content-Encoding: gzip\r\n") != NULL,
             "com Accept-Encoding: gzip a resposta vem comprimida");
    T_ASSERT(strstr(r, "Content-Type: application/json; charset=utf-8\r\n")
             != NULL, "Content-Type do json");
    /* corpo comprimido comeca com a assinatura gzip 1f 8b */
    T_ASSERT(strstr(r, "\r\n\r\n") != NULL, "cabecalho terminado");
    if (strstr(r, "Content-Encoding: gzip\r\n") != NULL) {
        const char *corpo = strstr(r, "\r\n\r\n") + 4;
        T_ASSERT((unsigned char)corpo[0] == 0x1F &&
                 (unsigned char)corpo[1] == 0x8B,
                 "corpo comeca com a assinatura gzip");
    }
    free(r);
}

static void testa_handle_client_head(void)
{
    char *r = atende("HEAD /index.html HTTP/1.1\r\nHost: x\r\n\r\n");
    confere_status(r, "200 OK", "HEAD responde 200");
    T_ASSERT(strstr(r, "\r\n\r\n") != NULL, "HEAD tem cabecalho");
    T_ASSERT(strstr(r, "\r\n\r\n") + 4 == r + strlen(r),
             "HEAD nao manda corpo, so cabecalho");
    free(r);
}

static void testa_handle_client_request_malformado(void)
{
    char *r = atende("lixo\r\n\r\n");
    confere_status(r, "400 Bad Request", "request malformado responde 400");
    free(r);
}

/* ------------------------------------------------------------------ */

void test_http(void)
{
    printf("testa_parse_basico\n");         testa_parse_basico();
    printf("testa_parse_sem_range\n");      testa_parse_sem_range();
    printf("testa_parse_accept_encoding\n"); testa_parse_accept_encoding();
    printf("testa_parse_malformado\n");     testa_parse_malformado();
    printf("testa_status_text\n");           testa_status_text();
    printf("testa_send_all_em_fd_fake\n");   testa_send_all_em_fd_fake();

    cria_fake();
    cria_web_fix();

    printf("testa_range_simples\n");            testa_range_simples();
    printf("testa_range_clamp_no_eof\n");       testa_range_clamp_no_eof();
    printf("testa_range_offset_alem_do_eof\n");  testa_range_offset_alem_do_eof();
    printf("testa_range_suffix_e_aberto\n");    testa_range_suffix_e_aberto();
    printf("testa_range_malformado_416\n");     testa_range_malformado_416();
    printf("testa_static_200\n");               testa_static_200();
    printf("testa_static_403\n");               testa_static_403();
    printf("testa_404\n");                      testa_404();

    printf("testa_handle_client_200\n");             testa_handle_client_200();
    printf("testa_handle_client_403\n");             testa_handle_client_403();
    printf("testa_handle_client_404_e_405\n");       testa_handle_client_404_e_405();
    printf("testa_handle_client_206\n");             testa_handle_client_206();
    printf("testa_handle_client_gzip\n");            testa_handle_client_gzip();
    printf("testa_handle_client_request_malformado\n");
    testa_handle_client_request_malformado();
    printf("testa_handle_client_head\n");    testa_handle_client_head();

    apaga_fixtures();
}
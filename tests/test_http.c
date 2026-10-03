/* tests/test_http.c -- Tarefa 1: parsing de requisição, Range, validação de
 * caminho e atendimento de conexão.
 *
 * Nada de rede externa: os testes de http_handle_client usam socketpair
 * (IPC local) e o servidor real sobe em porta efêmera de loopback, dentro
 * de um processo filho.
 *
 * Ver http_server.h para o contrato das funções testadas.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE   /* getifaddrs() e IFF_LOOPBACK, que são BSD */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

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
#define LINK_FIX  WEB_FIX "/link-fora.html"
#define ALVO_FIX  WEB_FIX "/alvo-dentro.html"

#define RESPOSTA_MAX (1 << 20)

/* ------------------------------------------------------------------ */
/* fixtures                                                            */
/* ------------------------------------------------------------------ */

static void escreve_arquivo(const char *path, const void *dados, size_t n)
{
    FILE *f = fopen(path, "wb");
    T_ASSERT(f != NULL, "não consegui abrir o arquivo de fixture");
    if (n > 0)
        T_ASSERT(fwrite(dados, 1, n, f) == n, "fwrite da fixture falhou");
    fclose(f);
}

/* fake.bin: 1000 bytes, byte i = i & 0xFF */
static void cria_fake(void)
{
    char buf[FAKE_SIZE];
    int i;
    for (i = 0; i < FAKE_SIZE; i++)
        buf[i] = (char)(i & 0xFF);
    escreve_arquivo(FAKE_PATH, buf, sizeof buf);
}

/* fixture_web/: index.html, styles.css e dados.json (2000 bytes, para o
 * teste de gzip ter conteúdo grande bastante). */
static void cria_web_fix(void)
{
    char *json;
    int i, n;

    remove(INDEX_FIX);                  /* sobras de execução anterior */
    remove(WEB_FIX "/styles.css");
    remove(JSON_FIX);
    remove(LINK_FIX);
    remove(WEB_FIX "/link-dentro.html");
    remove(ALVO_FIX);
    mkdir(WEB_FIX, 0777);               /* EEXIST aqui não é erro */
    T_ASSERT(access(WEB_FIX, F_OK) == 0, "não consegui criar fixture_web/");

    escreve_arquivo(INDEX_FIX, INDEX_TXT, strlen(INDEX_TXT));
    escreve_arquivo(WEB_FIX "/styles.css", CSS_TXT, strlen(CSS_TXT));
    /* alvo do symlink que fica dentro do root */
    escreve_arquivo(ALVO_FIX, "dentro do root\n", 16);
    /* symlink para fora do root (achado I5) e symlink para dentro */
    T_ASSERT(symlink(FAKE_PATH, LINK_FIX) == 0, "não consegui criar symlink fora");
    T_ASSERT(symlink(ALVO_FIX, WEB_FIX "/link-dentro.html") == 0,
             "não consegui criar symlink dentro");

    json = malloc(JSON_SIZE + 1);
    T_ASSERT(json != NULL, "sem memória para a fixture json");
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
    remove(LINK_FIX);
    remove(WEB_FIX "/link-dentro.html");
    remove(ALVO_FIX);
    rmdir(WEB_FIX);
}

/* ------------------------------------------------------------------ */
/* parsing de requisição                                               */
/* ------------------------------------------------------------------ */

static void testa_parse_basico(void)
{
    http_request req;
    int rc = http_parse_request(
        "GET /api/x?a=1 HTTP/1.1\r\nRange: bytes=0-99\r\n\r\n", &req);
    T_ASSERT(rc == 0, "parse de requisição bem formada devolve 0");
    T_ASSERT(strcmp(req.method, "GET") == 0, "method == GET");
    T_ASSERT(strcmp(req.path, "api/x") == 0, "path == api/x (sem barra inicial)");
    T_ASSERT(strcmp(req.query, "a=1") == 0, "query == a=1");
    T_ASSERT(strcmp(req.range_header, "bytes=0-99") == 0,
             "range_header == bytes=0-99");
    T_ASSERT(req.accept_encoding == NULL,
             "accept_encoding == NULL quando o cabeçalho não veio");
}

static void testa_parse_sem_range(void)
{
    http_request req;
    int rc = http_parse_request(
        "GET /api/x HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n\r\n", &req);
    T_ASSERT(rc == 0, "parse sem Range devolve 0");
    T_ASSERT(req.range_header == NULL, "range_header == NULL quando ausente");
    T_ASSERT(strcmp(req.path, "api/x") == 0, "path == api/x");
    T_ASSERT(req.query != NULL, "query não pode ser NULL");
    T_ASSERT(strcmp(req.query, "") == 0, "query == \"\" (vazio, não NULL)");
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
             "accept_encoding cru == gzip, deflate (nome sem diferenciar caixa)");
    T_ASSERT(strcmp(req.range_header, "bytes=0-1") == 0,
             "range_header sem espaços em volta");
}

static void testa_parse_malformado(void)
{
    http_request req;
    T_ASSERT(http_parse_request("GARBAGE", &req) == -1,
             "requisição sem verbo nem caminho é malformada");
    T_ASSERT(http_parse_request("GET nao-slash HTTP/1.1\r\n\r\n", &req) == -1,
             "alvo que não começa com '/' é malformado");
    T_ASSERT(http_parse_request("GET /x\r\n\r\n", &req) == -1,
             "requisição sem versão HTTP é malformada");
    T_ASSERT(http_parse_request(NULL, &req) == -1, "raw NULL é malformado");
    T_ASSERT(http_parse_request("GET /x HTTP/1.1\r\n\r\n", NULL) == -1,
             "out NULL é malformado");
}

static void testa_status_text(void)
{
    T_ASSERT(strcmp(http_status_text(200), "OK") == 0, "200 == OK");
    T_ASSERT(strcmp(http_status_text(206), "Partial Content") == 0, "206");
    T_ASSERT(strcmp(http_status_text(400), "Bad Request") == 0, "400");
    T_ASSERT(strcmp(http_status_text(403), "Forbidden") == 0, "403");
    T_ASSERT(strcmp(http_status_text(404), "Not Found") == 0, "404");
    T_ASSERT(strcmp(http_status_text(405), "Method Not Allowed") == 0, "405");
    T_ASSERT(strcmp(http_status_text(408), "Request Timeout") == 0, "408");
    T_ASSERT(strcmp(http_status_text(416), "Range Not Satisfiable") == 0, "416");
    T_ASSERT(strcmp(http_status_text(500), "Internal Server Error") == 0, "500");
    T_ASSERT(http_status_text(999) != NULL, "código desconhecido devolve texto");
}

static void testa_send_all_em_fd_fake(void)
{
    const char *buf = "0123456789";
    /* -1 não é um descritor válido: http_send_all tem de devolver -1, não
     * travar nem reportar sucesso. */
    T_ASSERT(http_send_all(-1, buf, 10) == -1,
             "http_send_all em descritor inválido devolve -1");
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
    T_ASSERT(res.body == NULL, "res.body fica NULL: quem envia lê do disco");
    T_ASSERT(res.filename != NULL && strcmp(res.filename, FAKE_PATH) == 0,
             "res.filename == /tmp/opencode/fake.bin");
    T_ASSERT(res.content_type != NULL &&
             strstr(res.content_type, "charset") != NULL,
             "content_type sempre inclui charset");
}

static void testa_range_clamp_no_eof(void)
{
    http_response res;
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=900-2000", &res) == 206,
             "fatia além do EOF mas começando dentro devolve 206");
    T_ASSERT(res.offset == 900, "res.offset == 900");
    T_ASSERT(res.length == 100, "res.length limitado em 100 (até o EOF)");
}

static void testa_range_offset_alem_do_eof(void)
{
    http_response res;
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=5000-6000", &res)
             == 416, "offset começando além do EOF devolve 416");
    T_ASSERT(res.status == 416, "res.status == 416");
}

static void testa_range_suffix_e_aberto(void)
{
    http_response res;
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=-50", &res) == 206,
             "bytes=-50 (sufixo) devolve 206");
    T_ASSERT(res.offset == 950, "sufixo de 50 bytes começa em 950");
    T_ASSERT(res.length == 50, "sufixo devolve 50 bytes");

    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=990-", &res) == 206,
             "bytes=990- (aberto) devolve 206");
    T_ASSERT(res.offset == 990, "aberto começa em 990");
    T_ASSERT(res.length == 10, "aberto devolve os 10 bytes até o EOF");
}

static void testa_range_malformado_416(void)
{
    http_response res;
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=abc", &res) == 416,
             "Range não numérico devolve 416");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "itens=1-2", &res) == 416,
             "unidade desconhecida devolve 416");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=200-100", &res) == 416,
             "invertido (200-100) devolve 416");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_range(FAKE_DIR, "fake.bin", "bytes=0-99999", &res) == 206,
             "bytes=0-99999 é limitado em 1000");
    T_ASSERT(res.length == FAKE_SIZE, "bytes=0-99999 devolve o arquivo inteiro");
}

/* ------------------------------------------------------------------ */
/* estático e validação de caminho                                     */
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
    T_ASSERT(res.body == NULL, "estático usa res.filename, res.body == NULL");
    T_ASSERT(res.filename != NULL &&
             strcmp(res.filename, INDEX_FIX) == 0,
             "res.filename == <root>/index.html");
    T_ASSERT(res.offset == 0, "sem Range, res.offset == 0");
    T_ASSERT(res.length > 0, "res.length > 0 para arquivo com conteúdo");
    T_ASSERT(res.content_type != NULL &&
             strncmp(res.content_type, "text/html", 9) == 0,
             "content_type começa com text/html");
    T_ASSERT(strstr(res.content_type, "charset") != NULL,
             "content_type sempre inclui charset");

    req_de_get(&req, "styles.css");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 200,
             "styles.css existente devolve 200");
    T_ASSERT(res.content_type != NULL &&
             strncmp(res.content_type, "text/css", 8) == 0,
             "content_type começa com text/css");

    /* caminho vazio (pedido de "/") cai em index.html */
    req_de_get(&req, "");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 200,
             "caminho vazio devolve index.html");
    T_ASSERT(res.filename != NULL && strcmp(res.filename, INDEX_FIX) == 0,
             "caminho vazio vira <root>/index.html");
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
    T_ASSERT(http_serve_range(FAKE_DIR, "nao-existe.bin", "bytes=0-9", &res)
             == 404, "serve_range: arquivo inexistente devolve 404");
}

/* ------------------------------------------------------------------ */
/* symlink que sai do root (achado I5)                                 */
/* ------------------------------------------------------------------ */

static void testa_symlink_fora_do_root(void)
{
    http_request req;
    http_response res;
    http_response res2;

    /* LINK_FIX aponta para FAKE_PATH, que está fora de WEB_FIX */
    req_de_get(&req, "link-fora.html");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 403,
             "symlink apontando para fora do root devolve 403");
    memset(&res2, 0, sizeof res2);
    T_ASSERT(http_serve_range(WEB_FIX, "link-fora.html", "bytes=0-9", &res2)
             == 403, "serve_range: symlink para fora do root devolve 403");

    /* symlink que fica dentro do root continua servindo (200) */
    req_de_get(&req, "link-dentro.html");
    memset(&res, 0, sizeof res);
    T_ASSERT(http_serve_static(WEB_FIX, &req, &res) == 200,
             "symlink apontando para dentro do root devolve 200");
}

/* ------------------------------------------------------------------ */
/* http_handle_client: socketpair local, sem rede                      */
/* ------------------------------------------------------------------ */

static char *le_ate_eof(int fd)
{
    char *buf = malloc(RESPOSTA_MAX);
    size_t n = 0;

    T_ASSERT(buf != NULL, "sem memória para a resposta");
    for (;;) {
        ssize_t k = read(fd, buf + n, RESPOSTA_MAX - 1 - n);
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
    buf[n] = '\0';
    return buf;
}

/* Manda req por um socketpair, roda http_handle_client no outro extremo e
 * devolve a resposta inteira. As fixtures são minúsculas, então não há risco
 * de encher o buffer do socket e travar. */
static char *atende(const char *req)
{
    int sv[2];
    char *resposta;

    T_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
             "socketpair falhou");
    T_ASSERT(http_send_all(sv[0], req, strlen(req)) == 0,
             "escrita da requisição falhou");
    shutdown(sv[0], SHUT_WR);           /* o servidor vê EOF depois do cabeçalho */

    http_handle_client(sv[1], WEB_FIX); /* fecha sv[1] ao final */

    resposta = le_ate_eof(sv[0]);
    close(sv[0]);
    return resposta;
}

/* Confere a linha de status inteira, sem contar caracteres à mão. */
static void confere_status(const char *resp, const char *status_texto,
                           const char *msg)
{
    char esperado[64];
    snprintf(esperado, sizeof esperado, "HTTP/1.1 %s\r\n", status_texto);
    T_ASSERT(strncmp(resp, esperado, strlen(esperado)) == 0, msg);
}

/* Separa cabeçalho e corpo: devolve o ponteiro para o corpo e o tamanho dele. */
static const char *corpo_de(const char *resp, size_t *n_corpo)
{
    const char *p = strstr(resp, "\r\n\r\n");
    T_ASSERT(p != NULL, "resposta sem terminação de cabeçalho");
    p += 4;
    *n_corpo = strlen(p);
    return p;
}

static void testa_handle_client_200(void)
{
    char esperado[64];
    const char *corpo;
    size_t n_corpo = 0;
    char *r = atende("GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n");

    snprintf(esperado, sizeof esperado, "Content-Length: %u\r\n",
             (unsigned)strlen(INDEX_TXT));
    confere_status(r, "200 OK", "resposta começa com HTTP/1.1 200 OK");
    T_ASSERT(strstr(r, "Content-Type: text/html; charset=utf-8\r\n") != NULL,
             "Content-Type text/html com charset");
    T_ASSERT(strstr(r, esperado) != NULL, "Content-Length igual ao arquivo");
    T_ASSERT(strstr(r, "Connection: close\r\n") != NULL,
             "Connection: close (uma requisição por conexão)");
    /* o corpo tem de ser o arquivo inteiro, byte a byte */
    corpo = corpo_de(r, &n_corpo);
    T_ASSERT(n_corpo == strlen(INDEX_TXT), "corpo tem o tamanho do arquivo");
    T_ASSERT(memcmp(corpo, INDEX_TXT, n_corpo) == 0,
             "corpo é byte a byte igual ao arquivo");
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

    r = atende("GET /link-fora.html HTTP/1.1\r\nHost: x\r\n\r\n");
    confere_status(r, "403 Forbidden", "symlink para fora do root responde 403");
    free(r);
}

static void testa_handle_client_404_e_405(void)
{
    char *r = atende("GET /nao-existe HTTP/1.1\r\nHost: x\r\n\r\n");
    confere_status(r, "404 Not Found", "arquivo inexistente responde 404");
    free(r);

    r = atende("POST /index.html HTTP/1.1\r\nHost: x\r\n\r\n");
    confere_status(r, "405 Method Not Allowed",
                   "método diferente de GET responde 405");
    free(r);
}

/* achado I1: esta suíte precisa ler os bytes do corpo, não só o cabeçalho.
 * Sem a comparação byte a byte ela passa com o corpo vazio. */
static void testa_handle_client_206(void)
{
    const char *corpo;
    size_t n_corpo = 0;
    char *r = atende("GET /index.html HTTP/1.1\r\nHost: x\r\n"
                     "Range: bytes=5-9\r\n\r\n");

    confere_status(r, "206 Partial Content", "Range válido responde 206");
    T_ASSERT(strstr(r, "Content-Range: bytes 5-9/") != NULL,
             "Content-Range com posição e tamanho total");
    T_ASSERT(strstr(r, "Content-Length: 5\r\n") != NULL,
             "Content-Length da fatia");

    corpo = corpo_de(r, &n_corpo);
    T_ASSERT(n_corpo == 5,
             "corpo da fatia tem exatamente 5 bytes (não pode vir vazio)");
    T_ASSERT(memcmp(corpo, INDEX_TXT + 5, 5) == 0,
             "corpo da fatia é byte a byte igual ao trecho do arquivo");
    T_ASSERT(strstr(r, "<h1>") == NULL,
             "corpo da fatia não pode vazar o arquivo inteiro");
    free(r);

    r = atende("GET /index.html HTTP/1.1\r\nHost: x\r\n"
               "Range: bytes=9999-10000\r\n\r\n");
    confere_status(r, "416 Range Not Satisfiable",
                   "Range além do EOF responde 416");
    free(r);
}

static void testa_handle_client_gzip(void)
{
    const char *corpo;
    size_t n_corpo = 0;
    char *r = atende("GET /dados.json HTTP/1.1\r\nHost: x\r\n\r\n");

    confere_status(r, "200 OK", "json responde 200");
    T_ASSERT(strstr(r, "Content-Encoding: gzip\r\n") == NULL,
             "sem Accept-Encoding não comprime");
    T_ASSERT(strstr(r, "Content-Length: 2000\r\n") != NULL,
             "json inteiro sem comprimir");
    corpo = corpo_de(r, &n_corpo);
    T_ASSERT(n_corpo == JSON_SIZE, "corpo do json tem 2000 bytes");
    free(r);

    r = atende("GET /dados.json HTTP/1.1\r\nHost: x\r\n"
               "Accept-Encoding: gzip, deflate\r\n\r\n");
    T_ASSERT(strstr(r, "Content-Encoding: gzip\r\n") != NULL,
             "com Accept-Encoding: gzip a resposta vem comprimida");
    T_ASSERT(strstr(r, "Content-Type: application/json; charset=utf-8\r\n")
             != NULL, "Content-Type do json");
    corpo = corpo_de(r, &n_corpo);
    T_ASSERT(n_corpo > 0, "corpo comprimido não vem vazio");
    T_ASSERT((unsigned char)corpo[0] == 0x1F && (unsigned char)corpo[1] == 0x8B,
             "corpo começa com a assinatura gzip");
    free(r);
}

static void testa_handle_client_request_malformado(void)
{
    char *r = atende("lixo\r\n\r\n");
    confere_status(r, "400 Bad Request", "requisição malformada responde 400");
    free(r);
}

static void testa_handle_client_head(void)
{
    const char *corpo;
    size_t n_corpo = 0;
    char *r = atende("HEAD /index.html HTTP/1.1\r\nHost: x\r\n\r\n");
    confere_status(r, "200 OK", "HEAD responde 200");
    corpo = corpo_de(r, &n_corpo);
    (void)corpo;
    T_ASSERT(n_corpo == 0, "HEAD não manda corpo, só cabeçalho");
    free(r);
}

/* achado I3: conexão que fica muda tem de ser abandonada. */
static double agora(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Pausa curta e Portable (usleep sumiu do POSIX de 2008). */
static void dorme_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void testa_timeout_conexao_muda(void)
{
    int sv[2];
    char *resposta;
    double t0, t1;
    int salvo = http_read_timeout_s;

    http_read_timeout_s = 1;            /* 1 s no teste */
    T_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
             "socketpair falhou");

    t0 = agora();
    http_handle_client(sv[1], WEB_FIX); /* nada é enviado: deve desistir */
    t1 = agora();

    resposta = le_ate_eof(sv[0]);
    close(sv[0]);
    http_read_timeout_s = salvo;

    T_ASSERT(t1 - t0 < 4.0,
             "servidor desiste da conexão muda em menos de 4 s");
    T_ASSERT(t1 - t0 >= 0.9,
             "servidor esperou o timeout configurado antes de desistir");
    confere_status(resposta, "408 Request Timeout",
                   "conexão abandonada responde 408 e fecha");
    free(resposta);
}

/* ------------------------------------------------------------------ */
/* servidor real em loopback (achados C1 e I2)                         */
/* ------------------------------------------------------------------ */

/* Reserva uma porta efêmera de loopback e devolve o número. */
static int porta_livre(void)
{
    int s, opt = 1;
    struct sockaddr_in a;
    socklen_t n = sizeof a;
    int porta = 0;

    s = socket(AF_INET, SOCK_STREAM, 0);
    T_ASSERT(s >= 0, "socket para sortear a porta falhou");
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    T_ASSERT(bind(s, (struct sockaddr *)&a, sizeof a) == 0,
             "bind para sortear a porta falhou");
    T_ASSERT(getsockname(s, (struct sockaddr *)&a, &n) == 0,
             "getsockname falhou");
    porta = ntohs(a.sin_port);
    close(s);
    return porta;
}

/* Conecta em ip:porta sem bloquear. Devolve 0 se conectou, -1 se recusou
 * ou expirou (ambos significam "não aceita conexão aqui"). */
static int conecta(const char *ip, int porta)
{
    int s, flags, rc, erro, conectou = 0;
    struct sockaddr_in a;
    struct pollfd pfd;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)porta);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) {
        close(s);
        return -1;
    }

    flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    rc = connect(s, (struct sockaddr *)&a, sizeof a);
    erro = errno;
    if (rc == 0) {
        close(s);
        return 0;
    }
    if (erro != EINPROGRESS) {          /* recusado de cara */
        close(s);
        return -1;
    }
    pfd.fd = s;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    rc = poll(&pfd, 1, 2000);
    if (rc > 0 && (pfd.revents & (POLLOUT | POLLERR | POLLHUP))) {
        int val = 1;
        socklen_t vl = sizeof val;
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, &val, &vl) == 0 && val == 0)
            conectou = 1;
    }
    close(s);
    return conectou ? 0 : -1;
}

/* Primeiro IPv4 que não é loopback, ou NULL se a máquina não tiver um. */
static const char *ip_nao_loopback(void)
{
    static char buf[INET_ADDRSTRLEN];
    struct ifaddrs *ifas = NULL, *ifa;

    if (getifaddrs(&ifas) != 0)
        return NULL;
    for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
        if (inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr,
                      buf, sizeof buf))
            break;
        buf[0] = '\0';
    }
    freeifaddrs(ifas);
    return buf[0] ? buf : NULL;
}

/* Sobe o servidor de verdade num filho e devolve o pid (ou -1). */
/* Pid do servidor real em execução. Uma asserção que falha chama exit(1) no
 * meio do teste, e o filho órfão continuaria escutando para sempre: ele segura
 * a escrita no stdout do make e o build travaria em vez de reportar a falha.
 * Por isso o filho é registrado e morto no atexit. */
static int servidor_vivo = -1;

static void mata_servidor_registrado(void)
{
    int status;
    if (servidor_vivo > 0) {
        kill(servidor_vivo, SIGKILL);
        waitpid(servidor_vivo, &status, 0);
        servidor_vivo = -1;
    }
}

static int sobe_servidor(const char *root, int porta)
{
    pid_t pid = fork();
    int espera;

    if (pid == 0) {
        http_listen_and_serve(porta, (char *)root);
        _exit(0);
    }
    T_ASSERT(pid > 0, "fork do servidor real falhou");
    for (espera = 0; espera < 100; espera++) {
        if (conecta("127.0.0.1", porta) == 0) {
            servidor_vivo = (int)pid;
            return (int)pid;
        }
        dorme_ms(20);                   /* 2 s no total */
    }
    T_ASSERT(0, "servidor real não ficou escutando em 127.0.0.1");
    return -1;
}

static void desce_servidor(int pid)
{
    int status;
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    if (servidor_vivo == pid)
        servidor_vivo = -1;
}

/* Envia uma requisição ao servidor real e devolve a resposta inteira. */
static char *pede_ao_servidor(int porta, const char *req)
{
    int s;
    char *resposta;
    struct sockaddr_in a;

    T_ASSERT(conecta("127.0.0.1", porta) == 0,
             "não consegui conectar no servidor real de loopback");
    s = socket(AF_INET, SOCK_STREAM, 0);
    T_ASSERT(s >= 0, "socket do cliente falhou");
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((unsigned short)porta);
    T_ASSERT(connect(s, (struct sockaddr *)&a, sizeof a) == 0,
             "connect TCP no servidor real falhou");
    T_ASSERT(http_send_all(s, req, strlen(req)) == 0,
             "escrita da requisição no servidor real falhou");
    shutdown(s, SHUT_WR);
    resposta = le_ate_eof(s);
    close(s);
    return resposta;
}

/* achado C1: 206 tem de sair com os bytes da fatia, não só o cabeçalho. */
static void testa_servidor_real_206(void)
{
    int porta = porta_livre();
    int pid = sobe_servidor(WEB_FIX, porta);
    const char *corpo;
    size_t n_corpo = 0;
    char *r = pede_ao_servidor(porta,
                               "GET /index.html HTTP/1.1\r\nHost: x\r\n"
                               "Range: bytes=5-9\r\nConnection: close\r\n\r\n");

    confere_status(r, "206 Partial Content", "servidor real responde 206");
    T_ASSERT(strstr(r, "Content-Length: 5\r\n") != NULL,
             "Content-Length da fatia é 5");
    corpo = corpo_de(r, &n_corpo);
    T_ASSERT(n_corpo == 5,
             "servidor real entregou 5 bytes de corpo (não pode vir vazio)");
    T_ASSERT(memcmp(corpo, INDEX_TXT + 5, 5) == 0,
             "servidor real entregou a fatia certa, byte a byte");
    free(r);
    desce_servidor(pid);
}

/* achado I2: o servidor só pode escutar em loopback. */
static void testa_servidor_so_loopback(void)
{
    int porta = porta_livre();
    int pid = sobe_servidor(WEB_FIX, porta);
    const char *ip = ip_nao_loopback();

    T_ASSERT(conecta("127.0.0.1", porta) == 0,
             "servidor aceita conexão em 127.0.0.1");

    if (ip) {
        T_ASSERT(conecta(ip, porta) != 0,
                 "servidor NÃO pode aceitar conexão pelo endereço não-loopback");
        printf("      (endereço não-loopback testado: %s)\n", ip);
    } else {
        /* Sem endereço não-loopback na máquina não dá para tentar conectar
         * por ele; nesse caso o teste se limita a não haver INADDR_ANY no
         * código (verificado na revisão). */
        printf("      (AVISO: máquina sem IPv4 não-loopback; "
               "teste de conexão pulado)\n");
    }
    desce_servidor(pid);
}

/* ------------------------------------------------------------------ */

void test_http(void)
{
    /* Se qualquer asserção abortar com o servidor real no ar, o filho é
     * morto antes do exit para não segurar o pipe do make. */
    atexit(mata_servidor_registrado);

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
    printf("testa_symlink_fora_do_root\n");     testa_symlink_fora_do_root();

    printf("testa_handle_client_200\n");             testa_handle_client_200();
    printf("testa_handle_client_403\n");             testa_handle_client_403();
    printf("testa_handle_client_404_e_405\n");       testa_handle_client_404_e_405();
    printf("testa_handle_client_206\n");             testa_handle_client_206();
    printf("testa_handle_client_gzip\n");            testa_handle_client_gzip();
    printf("testa_handle_client_request_malformado\n");
    testa_handle_client_request_malformado();
    printf("testa_handle_client_head\n");            testa_handle_client_head();
    printf("testa_timeout_conexao_muda\n");          testa_timeout_conexao_muda();
    printf("testa_servidor_real_206\n");            testa_servidor_real_206();
    printf("testa_servidor_so_loopback\n");          testa_servidor_so_loopback();

    apaga_fixtures();
}
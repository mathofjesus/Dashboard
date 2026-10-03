/* http_server.c -- servidor HTTP estatico minimo: parsing, arquivos
 * estaticos, Range e gzip. Fork por conexao (ver http_listen_and_serve).
 *
 * Cabecalho padrao POSIX precisa vir antes de qualquer outro para os
 * (feature test macros) da glibc.
 */
#define _POSIX_C_SOURCE 200809L

#include "http_server.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <zlib.h>

/* ------------------------------------------------------------------ */
/* utilitarios                                                         */
/* ------------------------------------------------------------------ */

static char *dupn(const char *s, size_t n)
{
    char *p = malloc(n + 1);
    if (!p)
        return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* Compara s[0..n) com b sem diferenciar maiusculas/minusculas. */
static int ci_equal(const char *s, size_t n, const char *b)
{
    size_t i;
    if (n != strlen(b))
        return 0;
    for (i = 0; i < n; i++) {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)b[i]))
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* parsing da requisicao                                               */
/* ------------------------------------------------------------------ */

/* Formato aceito:
 *     METODO /alvo?query HTTP/1.1 CRLF
 *     Nome: valor CRLF
 *     ...
 *     CRLF
 * Devolve 0, ou -1 se raw/out for NULL ou a linha de requisicao nao for
 * entendida. method/path/query sao copias alocadas; query e "" quando nao
 * ha query string; range_header e accept_encoding sao NULL quando ausentes.
 */
int http_parse_request(const char *raw, http_request *out)
{
    const char *linha_fim, *ini, *fim_metodo;
    const char *ini_alvo, *fim_alvo, *fim_path, *ini_query;
    const char *p;
    const char *range_val = NULL, *aenc_val = NULL;

    if (!raw || !out)
        return -1;

    memset(out, 0, sizeof *out);

    /* ---- linha de requisicao ---- */
    linha_fim = strstr(raw, "\r\n");
    if (!linha_fim)
        return -1;

    ini = raw;
    while (ini < linha_fim && *ini == ' ')
        ini++;
    if (ini == linha_fim)
        return -1;                       /* linha so com espacos */

    fim_metodo = ini;
    while (fim_metodo < linha_fim && *fim_metodo != ' ')
        fim_metodo++;
    if (fim_metodo == linha_fim)
        return -1;                       /* sem alvo */
    if (fim_metodo == ini)
        return -1;                       /* verbo vazio */

    ini_alvo = fim_metodo;
    while (ini_alvo < linha_fim && *ini_alvo == ' ')
        ini_alvo++;
    if (ini_alvo == linha_fim || *ini_alvo != '/')
        return -1;                       /* alvo fora de origin-form */

    /* fim_path = fim do path (ate '?' ou espaco) */
    fim_path = ini_alvo + 1;
    while (fim_path < linha_fim && *fim_path != ' ' && *fim_path != '?')
        fim_path++;

    if (fim_path < linha_fim && *fim_path == '?') {
        ini_query = fim_path + 1;
        fim_alvo = ini_query;
        while (fim_alvo < linha_fim && *fim_alvo != ' ')
            fim_alvo++;
    } else {
        ini_query = NULL;
        fim_alvo = fim_path;
    }

    p = fim_alvo;
    while (p < linha_fim && *p == ' ')
        p++;
    if (p == linha_fim)
        return -1;                       /* falta a versao */
    if ((size_t)(linha_fim - p) < 8 || strncmp(p, "HTTP/", 5) != 0)
        return -1;

    out->method = dupn(ini, (size_t)(fim_metodo - ini));
    out->path = dupn(ini_alvo + 1, (size_t)(fim_path - ini_alvo - 1));
    if (!out->method || !out->path)
        return -1;
    if (ini_query) {
        out->query = dupn(ini_query, (size_t)(fim_alvo - ini_query));
        if (!out->query)
            return -1;
    } else {
        out->query = dupn("", 0);        /* "" e nao NULL */
        if (!out->query)
            return -1;
    }

    /* ---- cabecalhos ---- */
    p = linha_fim + 2;
    for (;;) {
        const char *doispontos, *nome, *v_ini, *v_fim, *linha_fim2;
        size_t n_nome;

        if (*p == '\r' && p[1] == '\n')
            break;                       /* fim dos cabecalhos */
        if (*p == '\0')
            return -1;                   /* request truncado */

        linha_fim2 = strstr(p, "\r\n");
        if (!linha_fim2)
            return -1;

        doispontos = p;
        while (doispontos < linha_fim2 && *doispontos != ':')
            doispontos++;
        if (doispontos == linha_fim2) {
            p = linha_fim2 + 2;          /* linha sem ':': ignora */
            continue;
        }

        nome = p;                        /* recua OWS antes do ':' */
        while (nome < doispontos &&
               (nome[doispontos - nome - 1] == ' ' ||
                nome[doispontos - nome - 1] == '\t'))
            nome++;
        n_nome = (size_t)(doispontos - nome);

        v_ini = doispontos + 1;
        v_fim = linha_fim2;
        while (v_ini < v_fim && (*v_ini == ' ' || *v_ini == '\t'))
            v_ini++;
        while (v_fim > v_ini && (v_fim[-1] == ' ' || v_fim[-1] == '\t'))
            v_fim--;

        if (ci_equal(nome, n_nome, "range") && !range_val) {
            range_val = dupn(v_ini, (size_t)(v_fim - v_ini));
            if (!range_val)
                return -1;
        } else if (ci_equal(nome, n_nome, "accept-encoding") && !aenc_val) {
            aenc_val = dupn(v_ini, (size_t)(v_fim - v_ini));
            if (!aenc_val)
                return -1;
        }

        p = linha_fim2 + 2;
    }

    out->range_header = range_val;
    out->accept_encoding = aenc_val;
    return 0;
}

/* ------------------------------------------------------------------ */
/* textos de status                                                    */
/* ------------------------------------------------------------------ */

const char *http_status_text(int status)
{
    switch (status) {
    case 200: return "OK";
    case 206: return "Partial Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 416: return "Range Not Satisfiable";
    case 500: return "Internal Server Error";
    default:  return "Erro Desconhecido";
    }
}

/* ------------------------------------------------------------------ */
/* escrita no socket                                                   */
/* ------------------------------------------------------------------ */

/* write() pode escrever menos que n; repete ate acabar. 0 em sucesso. */
int http_send_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n > 0) {
        ssize_t k = write(fd, p, n);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (k == 0)
            return -1;
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* arquivos estaticos                                                  */
/* ------------------------------------------------------------------ */

#define CAMINHO_MAX 4096

/* Buffer estatico para res->filename (ver contrato em http_server.h). */
static char caminho_buf[CAMINHO_MAX];

/* Caminho e inseguro se contem ".." ou comeca com "/". Vazio e aceito
 * (vira index.html). */
static int caminho_inseguro(const char *p)
{
    if (!p)
        return 1;
    if (p[0] == '/')
        return 1;
    return strstr(p, "..") != NULL;
}

static int join(char *dst, size_t cap, const char *a, const char *b)
{
    int n = snprintf(dst, cap, "%s/%s", a, b);
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

/* Sempre inclui charset, como pede o contrato de http_response. */
static const char *content_type_de(const char *path)
{
    static const struct { const char *ext, *mime; } tabela[] = {
        { "html",    "text/html; charset=utf-8" },
        { "htm",     "text/html; charset=utf-8" },
        { "css",     "text/css; charset=utf-8" },
        { "js",      "text/javascript; charset=utf-8" },
        { "json",    "application/json; charset=utf-8" },
        { "geojson", "application/geo+json; charset=utf-8" },
        { "csv",     "text/csv; charset=utf-8" },
        { "txt",     "text/plain; charset=utf-8" },
        { "svg",     "image/svg+xml; charset=utf-8" },
        { "png",     "image/png; charset=utf-8" },
        { "jpg",     "image/jpeg; charset=utf-8" },
        { "jpeg",    "image/jpeg; charset=utf-8" },
        { "gif",     "image/gif; charset=utf-8" },
        { "ico",     "image/x-icon; charset=utf-8" },
        { "gz",      "application/gzip; charset=utf-8" },
        { "zip",     "application/zip; charset=utf-8" }
    };
    const char *ext = strrchr(path, '.');
    size_t i;

    if (!ext)
        return "application/octet-stream; charset=utf-8";
    ext++;
    for (i = 0; i < sizeof tabela / sizeof tabela[0]; i++) {
        if (ci_equal(ext, strlen(ext), tabela[i].ext))
            return tabela[i].mime;
    }
    return "application/octet-stream; charset=utf-8";
}

static void res_erro(http_response *res, int status)
{
    memset(res, 0, sizeof *res);
    res->status = status;
    res->content_type = "text/plain; charset=utf-8";
    res->body = "";
    res->filename = NULL;
    res->offset = 0;
    res->length = 0;
    res->gzip = 0;
}

/* Enche res para o arquivo req->path dentro de root. Devolve res->status. */
int http_serve_static(const char *root, const http_request *req,
                      http_response *res)
{
    struct stat st;
    char com_index[CAMINHO_MAX];
    const char *rel;

    if (!res)
        return 500;
    res_erro(res, 500);
    if (!root || !req || !req->path)
        return 500;

    rel = req->path;
    if (caminho_inseguro(rel)) {
        res_erro(res, 403);
        return 403;
    }
    if (*rel == '\0')
        rel = "index.html";              /* pedido de "/" */

    if (join(caminho_buf, sizeof caminho_buf, root, rel) != 0) {
        res_erro(res, 404);
        return 404;
    }
    if (stat(caminho_buf, &st) != 0) {
        res_erro(res, 404);
        return 404;
    }
    if (S_ISDIR(st.st_mode)) {
        if (join(com_index, sizeof com_index, caminho_buf, "index.html") != 0) {
            res_erro(res, 404);
            return 404;
        }
        if (stat(com_index, &st) != 0 || !S_ISREG(st.st_mode)) {
            res_erro(res, 404);
            return 404;
        }
        memcpy(caminho_buf, com_index, strlen(com_index) + 1);
    } else if (!S_ISREG(st.st_mode)) {
        res_erro(res, 404);              /* fifo, socket, device... */
        return 404;
    }

    memset(res, 0, sizeof *res);
    res->status = 200;
    res->content_type = content_type_de(caminho_buf);
    res->body = NULL;
    res->filename = caminho_buf;
    res->offset = 0;
    res->length = (long long)st.st_size;
    res->gzip = 0;
    return 200;
}

/* Le um inteiro decimal sem sinal. -1 se nao houver digito. */
static int parse_dec(const char **pp, long long *out)
{
    const char *p = *pp;
    long long v = 0;

    if (*p < '0' || *p > '9')
        return -1;
    while (*p >= '0' && *p <= '9') {
        if (v > (LLONG_MAX - 9) / 10)
            return -1;                  /* overflow: faixa absurda */
        v = v * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    *out = v;
    return 0;
}

/* Enche res com uma fatia de req_path dentro de root, conforme o header
 * Range cru. Aceita "bytes=A-B", "bytes=A-" e "bytes=-N"; a fatia e clampeada
 * no tamanho real do arquivo e um offset comecando alem do EOF da 416.
 * Devolve res->status. */
int http_serve_range(const char *root, const char *req_path,
                     const char *range_header, http_response *res)
{
    struct stat st;
    const char *p;
    long long tamanho, primeiro, ultimo;

    if (!res)
        return 500;
    res_erro(res, 500);
    if (!root || !req_path || !range_header)
        return 500;

    if (caminho_inseguro(req_path)) {
        res_erro(res, 403);
        return 403;
    }
    if (join(caminho_buf, sizeof caminho_buf, root,
             *req_path ? req_path : "index.html") != 0) {
        res_erro(res, 404);
        return 404;
    }
    if (stat(caminho_buf, &st) != 0 || !S_ISREG(st.st_mode)) {
        res_erro(res, 404);
        return 404;
    }
    tamanho = (long long)st.st_size;

    p = range_header;
    while (*p == ' ' || *p == '\t')
        p++;
    if (strlen(p) < 6 || !ci_equal(p, 6, "bytes=")) {
        res_erro(res, 416);              /* unidade desconhecida */
        return 416;
    }
    p += 6;

    if (*p == '-') {                    /* bytes=-N : suffix */
        long long n;
        p++;
        if (parse_dec(&p, &n) != 0 || n == 0) {
            res_erro(res, 416);
            return 416;
        }
        primeiro = (n > tamanho) ? 0 : tamanho - n;
        ultimo = tamanho - 1;
    } else {
        if (parse_dec(&p, &primeiro) != 0 || *p != '-') {
            res_erro(res, 416);
            return 416;
        }
        p++;
        if (*p == '\0')
            ultimo = tamanho - 1;       /* bytes=A- : ate o EOF */
        else if (parse_dec(&p, &ultimo) != 0) {
            res_erro(res, 416);
            return 416;
        }
    }
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '\0') {                   /* lixo, ou multirange */
        res_erro(res, 416);
        return 416;
    }

    if (primeiro >= tamanho || ultimo < primeiro) {
        res_erro(res, 416);
        return 416;
    }
    if (ultimo > tamanho - 1)
        ultimo = tamanho - 1;

    memset(res, 0, sizeof *res);
    res->status = 206;
    res->content_type = content_type_de(caminho_buf);
    res->body = NULL;
    res->filename = caminho_buf;
    res->offset = primeiro;
    res->length = ultimo - primeiro + 1;
    res->gzip = 0;
    return 206;
}

/* ------------------------------------------------------------------ */
/* atendimento de uma conexao                                          */
/* ------------------------------------------------------------------ */

#define REQUEST_MAX 8192

/* Tipos que valem a pena comprimir. */
static int tipo_comprimivel(const char *ct)
{
    return strncmp(ct, "text/", 5) == 0 ||
           strncmp(ct, "application/json", 16) == 0 ||
           strncmp(ct, "application/geo+json", 19) == 0 ||
           strncmp(ct, "application/javascript", 22) == 0 ||
           strncmp(ct, "image/svg+xml", 13) == 0;
}

/* O cliente aceita gzip? Procura o token, sem confundir "gzip;q=0". */
static int aceita_gzip(const char *aenc)
{
    const char *p = aenc;

    if (!aenc)
        return 0;
    while ((p = strstr(p, "gzip")) != NULL) {
        const char *q = p + 4;
        int ok = (p == aenc || !isalnum((unsigned char)p[-1])) &&
                 !isalnum((unsigned char)*q);
        if (ok) {
            while (*q == ' ' || *q == '\t')
                q++;
            if (!(q[0] == ';' && strstr(q, "q=0")))
                return 1;
        }
        p += 4;
    }
    return 0;
}

/* Le o request ate o fim dos cabecalhos. Devolve o numero de bytes, ou -1. */
static ssize_t le_request(int fd, char *buf, size_t cap)
{
    size_t n = 0;

    for (;;) {
        ssize_t k;
        if (n + 1 >= cap)
            return -1;                  /* request grande demais */
        k = read(fd, buf + n, cap - 1 - n);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (k == 0)
            return (ssize_t)n;           /* EOF antes do fim */
        n += (size_t)k;
        buf[n] = '\0';
        if (strstr(buf, "\r\n\r\n"))
            return (ssize_t)n;
    }
}

/* Cabecalho + corpo em memoria (Content-Length). Serve tanto para o corpo
 * normal quanto para o comprimido: nos dois casos o corpo inteiro esta em
 * memoria, entao o tamanho e conhecido. so_cabecalho = 1 e o caso HEAD:
 * mandamos os cabecalhos (com Content-Length real) e nenhum corpo. */
static void envia_resposta(int fd, http_response *res, const char *extra,
                           int so_cabecalho)
{
    char cab[1024];
    int n = snprintf(cab, sizeof cab,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %lld\r\n"
                     "%s"
                     "Connection: close\r\n"
                     "\r\n",
                     res->status, http_status_text(res->status),
                     res->content_type, res->length, extra);
    if (n < 0 || (size_t)n >= sizeof cab)
        return;
    if (http_send_all(fd, cab, (size_t)n) != 0)
        return;
    if (so_cabecalho)
        return;
    if (res->length > 0 && res->body)
        http_send_all(fd, res->body, (size_t)res->length);
}

/* gzip em memoria. Devolve o buffer (malloc) e o tamanho, ou NULL. */
static unsigned char *gzip_mem(const unsigned char *in, size_t n,
                               size_t *out_n)
{
    z_stream zs;
    size_t cap = compressBound((uLong)n) + 64;
    unsigned char *out = malloc(cap);

    if (!out)
        return NULL;
    memset(&zs, 0, sizeof zs);
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + 15, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        free(out);
        return NULL;
    }
    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)n;
    zs.next_out = out;
    zs.avail_out = (uInt)cap;
    if (deflate(&zs, Z_FINISH) != Z_STREAM_END) {
        deflateEnd(&zs);
        free(out);
        return NULL;
    }
    *out_n = cap - zs.avail_out;
    deflateEnd(&zs);
    return out;
}

/* Le length bytes a partir de offset. Devolve buffer malloc, ou NULL. */
static char *le_arquivo(const char *filename, long long offset,
                        long long length, size_t *out_n)
{
    FILE *f = fopen(filename, "rb");
    char *buf;

    *out_n = 0;
    if (!f)
        return NULL;
    if (offset > 0 && fseeko(f, (off_t)offset, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)length + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (length > 0 && fread(buf, 1, (size_t)length, f) != (size_t)length) {
        fclose(f);
        free(buf);
        return NULL;
    }
    buf[length] = '\0';
    fclose(f);
    *out_n = (size_t)length;
    return buf;
}

void http_handle_client(int fd, const char *web_root)
{
    char buf[REQUEST_MAX];
    http_request req;
    http_response res;
    char extra[256];
    char *corpo = NULL;
    size_t n_corpo = 0;
    ssize_t n;
    int so_cabecalho = 0;

    extra[0] = '\0';

    n = le_request(fd, buf, sizeof buf);
    if (n <= 0 || http_parse_request(buf, &req) != 0) {
        res_erro(&res, 400);
        goto envia;
    }
    if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
        res_erro(&res, 405);
        goto envia;
    }
    so_cabecalho = strcmp(req.method, "HEAD") == 0;

    /* O status volta em res.status mesmo quando e erro (403/404/416). */
    if (req.range_header)
        (void)http_serve_range(web_root, req.path, req.range_header, &res);
    else
        (void)http_serve_static(web_root, &req, &res);

    if (res.status == 206) {
        struct stat st;
        long long total = 0;
        if (stat(res.filename, &st) == 0)
            total = (long long)st.st_size;
        snprintf(extra, sizeof extra,
                 "Content-Range: bytes %lld-%lld/%lld\r\n"
                 "Accept-Ranges: bytes\r\n",
                 res.offset, res.offset + res.length - 1, total);
        goto envia;
    }
    if (res.status != 200 || !res.filename)
        goto envia;

    snprintf(extra, sizeof extra, "Accept-Ranges: bytes\r\n");

    corpo = le_arquivo(res.filename, res.offset, res.length, &n_corpo);
    if (!corpo) {
        res_erro(&res, 500);
        extra[0] = '\0';
        goto envia;
    }
    res.body = corpo;
    res.length = (long long)n_corpo;

    if (aceita_gzip(req.accept_encoding) && n_corpo >= 512 &&
        tipo_comprimivel(res.content_type)) {
        unsigned char *gz = gzip_mem((const unsigned char *)corpo, n_corpo,
                                     &n_corpo);
        if (gz) {
            free(corpo);
            corpo = (char *)gz;
            res.body = corpo;
            res.length = (long long)n_corpo;
            res.gzip = 1;
            snprintf(extra, sizeof extra,
                     "Content-Encoding: gzip\r\nAccept-Ranges: bytes\r\n");
        }
    }

envia:
    envia_resposta(fd, &res, extra, so_cabecalho);
    free(corpo);
    close(fd);
}

/* ------------------------------------------------------------------ */
/* loop listen/accept/fork                                             */
/* ------------------------------------------------------------------ */

/* Recolhe todos os filhos que ja terminaram. */
static void recolhe_filhos(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

static void recycle_zumbis(int sig)
{
    int save = errno;
    recolhe_filhos();
    errno = save;
    (void)sig;
}

/* sigaction e nao signal(): neste sistema signal() tem semantica SysV e
 * volta a SIG_DFL depois do primeiro disparo, entao so o primeiro SIGCHLD
 * seria tratado e os filhos ficariam zumbis. */
static void instala_tratadores(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = recycle_zumbis;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;   /* nao canse no stop/fork */
    sigaction(SIGCHLD, &sa, NULL);

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;                    /* cliente que some */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);
}

int http_listen_and_serve(int porta, const char *web_root)
{
    int srv, opt = 1;
    struct sockaddr_in addr;

    srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return -1;
    }
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)porta);

    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0) {
        perror("bind");
        close(srv);
        return -1;
    }
    if (listen(srv, 64) != 0) {
        perror("listen");
        close(srv);
        return -1;
    }

    instala_tratadores();

    for (;;) {
        int cli;
        pid_t pid;

        recolhe_filhos();           /* cunha: nao confia so no sinal */
        cli = accept(srv, NULL, NULL);
        if (cli < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }
        pid = fork();
        if (pid == 0) {
            close(srv);
            http_handle_client(cli, web_root);  /* uma requisicao e sai */
            _exit(0);
        }
        if (pid < 0)
            perror("fork");
        close(cli);                     /* o pai nao fala com o cliente */
    }

    close(srv);
    return -1;
}
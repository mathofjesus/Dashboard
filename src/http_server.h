/* http_server.h -- servidor HTTP mínimo em C puro (fork por conexão).
 *
 * Contrato de memória:
 *   - http_parse_request() devolve, em out->method/path/query, ponteiros
 *     alocados com strdup(). O servidor atende UMA requisição por processo
 *     filho e encerra em seguida, então a memória é liberada pelo SO.
 *   - http_serve_static() e http_serve_range() preenchem res->filename com um
 *     ponteiro para um buffer estático interno (sofre sobrescrita na próxima
 *     chamada). res->body == NULL significa "leia res->filename do disco
 *     a partir de res->offset, res->length bytes".
 *   - gzip é ligado por http_handle_client(), não pelas funções de
 *     preenchimento: quando vale 1, res->body já está comprimido e
 *     res->length é o tamanho comprimido.
 */
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stddef.h>

typedef struct {
    const char *method;      /* "GET", "POST", ...            */
    const char *path;        /* sem query string, sem '/' inicial */
    const char *query;       /* query string, "" se ausente     */
    const char *range_header;/* valor cru de "Range", ou NULL   */
    const char *accept_encoding; /* cru, ou NULL               */
} http_request;

typedef struct {
    int status;
    const char *content_type;   /* sempre inclui charset        */
    const char *body;           /* NULL quando enviar_arquivo  */
    const char *filename;       /* caminho em web/ quando usar */
    long long offset;           /* offset do Range, 0 se nenhum*/
    long long length;           /* bytes a enviar               */
    int gzip;                   /* 1: body ja vem comprimido  */
} http_response;

/* 0 ok, -1 malformado. */
int http_parse_request(const char *raw, http_request *out);

const char *http_status_text(int status);

/* Enche res para GET em res->body (corpo em memória) ou res->filename.
 * Rejeita ("..", "/") com 403. Devolve res->status. */
int http_serve_static(const char *root, const http_request *req,
                      http_response *res);

/* Enche res para uma fatia de req_path dentro de root. Rejeita ("..", "/")
 * com 403. Devolve res->status. */
int http_serve_range(const char *root, const char *req_path,
                     const char *range_header, http_response *res);

/* Escreve n bytes, repetindo até acabar. Devolve 0 ou -1. */
int http_send_all(int fd, const void *buf, size_t n);

/* Faz o loop listen/accept/fork. Só retorna em erro fatal. */
int http_listen_and_serve(int porta, const char *web_root);

/* Atende uma requisição em fd e fecha a conexão. */
void http_handle_client(int fd, const char *web_root);

#endif /* HTTP_SERVER_H */
#ifndef CHARSET_H
#define CHARSET_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Converte bytes em ISO-8859-1 (Latin-1) para UTF-8.
 * in: buffer de entrada (não precisa terminar em NUL)
 * in_len: número de bytes em in
 * out: buffer de saída
 * out_cap: capacidade de out em bytes
 * out_len: (saída) bytes escritos em out
 * Retorno: 0 = OK, 1 = buffer pequeno (out_len = bytes necessários), 2 = inválido
 */
int charset_latin1_para_utf8(const char *in, size_t in_len, char *out, size_t out_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* CHARSET_H */

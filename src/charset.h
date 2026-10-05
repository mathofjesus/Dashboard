/* charset.h — transcodificação e as três utilidades de texto que a API usa.
 *
 * São cinco funções e elas existem por três motivos:
 *
 *   1. text_latin1_para_utf8 — a conversão ISO-8859-1 → UTF-8 que a spec §9
 *      exige, dirigida por dataset: cada fonte declara em `fontes.conf` se o
 *      CSV vem em Latin-1 (CNES, Leitos) ou já em UTF-8 (Dengue).
 *   2. text_eh_valido_utf8 — para decidir se um corpo já vem convertido, em vez
 *      de converter duas vezes e quebrar os acentos que estavam certos.
 *   3. text_json_escape — o envelope de erro e os nomes de município entram no
 *      JSON por este caminho; interpolar sem escopar quebra o documento com
 *      uma aspa no meio do nome.
 *   4. text_query_get — a leitura da query string. `api.c` tinha uma cópia
 *      privada (find_query_param_copy) que não decodificava %XX; as duas
 *      versões da mesma coisa divergem, e divergir em parser de query é bug de
 *      segurança esperando o nome de um município com "&".
 *   5. text_url_decode — o par do item 4.
 *
 * Contrato de memória: as funções com `char **` devolvem malloc'd e quem chama
 * libera com free(). As que devolvem `char` escrevem em buffer do chamador e
 * devolvem o número de bytes escritos (sem o NUL), como snprintf.
 */
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

/* ------------------------------------------------------------------ */
/* as quatro do brief                                                 */
/* ------------------------------------------------------------------ */

/* Converte `in_len` bytes de Latin-1 para UTF-8 e devolve o buffer malloc'd
 * (com '\0' no fim), ou NULL. `*out_len` recebe o tamanho sem o NUL quando
 * out_len não é NULL.
 *
 * Diferente de charset_latin1_para_utf8: não tem capacidade de saída, porque
 * quem chama aqui não está com um buffer de tamanho conhecido e sim com um
 * corpo inteiro — o caminho é medir, converter e devolver. */
char *text_latin1_para_utf8(const char *in, size_t in_len, size_t *out_len);

/* 1 se `s` (len bytes) é UTF-8 válido: sequência completa, sem byte solto
 * acima de 0xF4, sem surrogate (U+D800..U+DFFF, que não existe em UTF-8) e
 * sem U+FFFD já gravado — um texto que passou por uma conversão errada não
 * deve ser convertido de novo. 0 caso contrário. */
int text_eh_valido_utf8(const char *s, size_t len);

/* malloc'd: `s` com aspas, contrabarras e controles escapados, pronto para
 * ir entre aspas de uma string JSON. `s` NULL devolve NULL. */
char *text_json_escape(const char *s);

/* malloc'd: o valor decodificado do parâmetro `key` em `query`, ou NULL se o
 * parâmetro não existir. O valor vem decodificado de %XX e de "+".
 *
 * Devolver NULL para ausente é deliberado: a diferença entre `uf=` (vazio) e
 * `uf` ausente importa, e o chamador trata as duas de formas diferentes. */
char *text_query_get(const char *query, const char *key);

/* malloc'd: `s` com %XX decodificado e "+" virado em espaço. `s` NULL
 * devolve uma string vazia malloc'd (a query de quem não tem nada é ""). */
char *text_url_decode(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* CHARSET_H */
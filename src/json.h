/* json.h -- parser JSON de leitura para as respostas do IBGE.
 *
 * Contrato de memória:
 *   - json_parse() aloca uma árvore inteira a partir de um "arena" (blocos
 *     malloc bump). Só existe um dono: a raiz devolvida. json_free() libera
 *     todos os blocos de uma vez. Ponteiros para nós internos (json_str,
 *     json_arr_get, json_obj_get) morrem junto com a raiz e não devem ser
 *     guardados além dela.
 *   - Os campos da struct são privados. Acessar a árvore é pelas funções
 *     abaixo, que devolvem valores neutros quando o tipo não bate
 *     ("" para string, 0.0 para número, 0 para bool, 0 para tamanho de array).
 *
 * Limites (ver json.c): a profundidade de aninhamento e o número de nós têm
 * teto, para que entrada patológica ou corrompida devolva NULL em vez de
 * estourar a pilha ou a memória. Entrada grande em profundidade rasa — o
 * GeoJSON de 5,78 MB e a lista de localidades de 2,4 MB do IBGE — é o caso
 * normal e não bate em nenhum dos dois.
 */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>

typedef enum { JSON_NULO, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ } json_tipo;

typedef struct json_val json_val;

/* Profundidade de aninhamento aceita. Acima disso json_parse() devolve NULL. */
#define JSON_PROFUNDIDADE_MAX 200

/* Número de nós aceitos numa árvore. Acima disso json_parse() devolve NULL. */
#define JSON_NOS_MAX 4000000

/* Lê len bytes de buf e devolve a raiz do documento, ou NULL se o texto não
 * for JSON válido. O buffer é copiado internamente e não precisa estar
 * terminado em '\0'; mesmo assim, um byte 0 dentro de len encerra a leitura
 * (JSON não admite 0 cru, então isso não perde nada de válido).
 *
 * Depois da raiz, qualquer lixo é ignorado: parsear "{\"a\":1} lixeira"
 * devolve a raiz e não falha. */
json_val *json_parse(const char *buf, size_t len);

/* Libera a árvore inteira. json_free(NULL) não faz nada. */
void json_free(json_val *v);

/* Tipo do nó. json_tipo_de(NULL) devolve JSON_NULO. */
json_tipo json_tipo_de(const json_val *v);

/* Valor do campo "chave" de um objeto, ou NULL se não houver. */
const json_val *json_obj_get(const json_val *v, const char *chave);

/* Quantos elementos tem o array; 0 se v não for array. */
size_t json_arr_len(const json_val *v);

/* Elemento i do array, ou NULL se v não for array ou i estiver fora. */
const json_val *json_arr_get(const json_val *v, size_t i);

/* Texto do nó com aspas e escapes já resolvidos (inclusive \uXXXX, que sai
 * como UTF-8). Devolve "" se v não for JSON_STR. */
const char *json_str(const json_val *v);

/* Valor numérico; 0.0 se v não for JSON_NUM. */
double json_num(const json_val *v);

/* 1 se v for o literal true, 0 nos demais casos. */
long json_bool(const json_val *v);

#endif /* JSON_H */

/* src/json.c -- parser JSON de leitura (ver json.h).
 *
 * Descida recursiva sobre uma cópia NUL-terminada do texto, com alocação em
 * "arena": blocos malloc de 64 KiB liberados de uma vez por json_free(). Com
 * arena não há free por nó, não há lista de blocos a varrer na liberação e
 * parsear megabytes custa uma alocação a cada 64 KiB em vez de uma por nó.
 *
 * Três defesas contra entrada patológica (o IBGE manda dados bem formados,
 * mas um arquivo em cache pode vir truncado ou corrompido):
 *   - cópia do texto na arena, com '\0' no fim: nenhum salto pode passar do
 *     fim, mesmo com o buffer do chamador sem terminador;
 *   - teto de profundidade (JSON_PROFUNDIDADE_MAX): 200.000 '[' seguidos
 *     devolvem NULL em vez de estourar a pilha;
 *   - teto de nós (JSON_NOS_MAX): entrada grande em profundidade rasa — o caso
 *     normal do GeoJSON de 5,78 MB e da lista de localidades de 2,4 MB —
 *     continua passando.
 */
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

/* ------------------------------------------------------------------ */
/* arena                                                               */
/* ------------------------------------------------------------------ */

#define ARENA_BLOCO (64u * 1024u)

typedef struct arena_bloco {
    struct arena_bloco *prox;
    size_t usado, tam;
    max_align_t alinhar;   /* existe só para empurrar o payload p/ frente */
} arena_bloco;

typedef struct {
    arena_bloco *raiz;
    size_t nos;
} arena;

/* O payload começa logo depois do cabeçalho, então o cabeçalho precisa ter
 * tamanho múltiplo do alinhamento máximo — senão um double sai torto. */
_Static_assert(sizeof(arena_bloco) % _Alignof(max_align_t) == 0,
               "cabeçalho do bloco precisa sair alinhado");

static void *arena_aloca(arena *ar, size_t n)
{
    arena_bloco *b;
    void *p;
    size_t al = _Alignof(max_align_t);

    n = (n + al - 1) & ~(al - 1);
    if (n == 0)
        n = al;

    if (!ar->raiz || n > ar->raiz->tam - ar->raiz->usado) {
        size_t tam = n > ARENA_BLOCO ? n : ARENA_BLOCO;
        b = malloc(sizeof(*b) + tam);
        if (!b)
            return NULL;
        b->prox = ar->raiz;
        b->usado = 0;
        b->tam = tam;
        ar->raiz = b;
    }
    b = ar->raiz;
    p = (unsigned char *)b + sizeof(*b) + b->usado;
    b->usado += n;
    return p;
}

static void arena_libera(arena *ar)
{
    arena_bloco *b = ar->raiz, *prox;

    while (b) {
        prox = b->prox;
        free(b);
        b = prox;
    }
    ar->raiz = NULL;
    ar->nos = 0;
}

/* Devolve um vetor com espaço para pelo menos `quantas` itens, copiando os
 * `*cap` que já existiam. Devolve NULL só em falta de memória. */
static void *cresce(arena *ar, void *atual, size_t *cap, size_t tam_item,
                    size_t quantas)
{
    size_t velho = *cap, nova;
    void *novo;

    if (quantas <= velho)
        return atual;                   /* já cabe */
    nova = velho ? velho : 8;
    while (nova < quantas) {
        if (nova > SIZE_MAX / 2)
            return NULL;                /* pediria um vetor impossível */
        nova *= 2;
    }
    if (nova > SIZE_MAX / tam_item)
        return NULL;
    novo = arena_aloca(ar, nova * tam_item);
    if (!novo)
        return NULL;
    if (velho)
        memcpy(novo, atual, velho * tam_item);
    *cap = nova;
    return novo;
}

/* ------------------------------------------------------------------ */
/* nós                                                                 */
/* ------------------------------------------------------------------ */

struct json_val {
    json_tipo tipo;
    arena *ar;                 /* dono da árvore; json_free usa este ponteiro */
    union {
        double num;
        long booleano;
        char *str;
        struct {
            json_val **itens;
            size_t n, cap;
        } arr;
        struct {
            char **chaves;
            size_t n, cap_chaves;
            json_val **valores;
            size_t cap_valores;
        } obj;
    } u;
};

static json_val *novo_val(arena *ar, json_tipo tipo)
{
    json_val *v;

    if (ar->nos >= JSON_NOS_MAX)
        return NULL;
    v = arena_aloca(ar, sizeof(*v));
    if (!v)
        return NULL;
    memset(v, 0, sizeof(*v));
    v->tipo = tipo;
    v->ar = ar;
    ar->nos++;
    return v;
}

/* ------------------------------------------------------------------ */
/* auxiliares de texto                                                 */
/* ------------------------------------------------------------------ */

static const char *pula_branco(const char *p, const char *fim)
{
    while (p < fim) {
        unsigned char c = (unsigned char)*p;
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        p++;
    }
    return p;
}

static int hex4(const char *p, unsigned *out)
{
    unsigned v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)p[i];
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= (unsigned)(c - 'A' + 10);
        else
            return -1;                  /* não é hex */
    }
    *out = v;
    return 0;
}

/* Escreve cp (ponto de código) em UTF-8 e devolve o próximo byte livre. */
static unsigned char *escreve_utf8(unsigned char *d, unsigned cp)
{
    if (cp < 0x80) {
        *d++ = (unsigned char)cp;
    } else if (cp < 0x800) {
        *d++ = (unsigned char)(0xC0 | (cp >> 6));
        *d++ = (unsigned char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *d++ = (unsigned char)(0xE0 | (cp >> 12));
        *d++ = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        *d++ = (unsigned char)(0x80 | (cp & 0x3F));
    } else {
        *d++ = (unsigned char)(0xF0 | (cp >> 18));
        *d++ = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        *d++ = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        *d++ = (unsigned char)(0x80 | (cp & 0x3F));
    }
    return d;
}

/* ------------------------------------------------------------------ */
/* parser                                                              */
/* ------------------------------------------------------------------ */

static json_val *parse_valor(arena *ar, const char **pp, const char *fim,
                             int prof);

/* Lê a string que começa em p (p precisa estar na aspa de abertura) e
 * escreve o texto decodificado em *saida. O maior tamanho possível é o que
 * resta até o fim (escape nunca amplia), então a reserva é exata no caso
 * normal, sem string com escape. */
static const char *parse_str(arena *ar, const char *p, const char *fim,
                             char **saida)
{
    unsigned char *buf;
    unsigned char *d;
    size_t sobra;

    if (p >= fim || *p != '"')
        return NULL;
    p++;
    sobra = (size_t)(fim - p) + 1;
    buf = arena_aloca(ar, sobra);
    if (!buf)
        return NULL;
    d = buf;

    while (p < fim) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') {
            *d = '\0';
            *saida = (char *)buf;
            return p + 1;
        }
        if (c < 0x20)
            return NULL;              /* controle cru não é JSON válido */
        if (c != '\\') {
            *d++ = c;
            p++;
            continue;
        }
        p++;
        if (p >= fim)
            return NULL;              /* barra invertida no fim */
        switch (*p) {
        case '"':  *d++ = '"';  p++; break;
        case '\\': *d++ = '\\'; p++; break;
        case '/':  *d++ = '/';  p++; break;
        case 'b':  *d++ = '\b'; p++; break;
        case 'f':  *d++ = '\f'; p++; break;
        case 'n':  *d++ = '\n'; p++; break;
        case 'r':  *d++ = '\r'; p++; break;
        case 't':  *d++ = '\t'; p++; break;
        case 'u': {
            unsigned cp;
            p++;
            if (fim - p < 4 || hex4(p, &cp) != 0)
                return NULL;
            p += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                /* par substituto: o par baixo tem de vir logo em seguida */
                unsigned baixo;
                if (fim - p >= 6 && p[0] == '\\' && p[1] == 'u' &&
                    hex4(p + 2, &baixo) == 0 && baixo >= 0xDC00 && baixo <= 0xDFFF) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (baixo - 0xDC00u);
                    p += 6;
                } else {
                    cp = 0xFFFDu;     /* substituto sozinho vira U+FFFD */
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                cp = 0xFFFDu;         /* substituto baixo sozinho idem */
            }
            d = escreve_utf8(d, cp);
            break;
        }
        default:
            return NULL;              /* escape desconhecido */
        }
    }
    return NULL;                      /* string sem aspa de fechamento */
}

/* Número JSON estrito: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
 * A validação é feita antes do strtod para não aceitar "nan", "+1" nem
 * "01"; o strtod devolve o valor. O programa não troca o locale, então o
 * ponto é o separador decimal. */
static const char *parse_num(const char *p, const char *fim, json_val *v)
{
    const char *inicio = p;
    char *depois;

    if (p < fim && *p == '-')
        p++;
    if (p >= fim || !isdigit((unsigned char)*p))
        return NULL;
    if (*p == '0') {
        p++;
    } else {
        while (p < fim && isdigit((unsigned char)*p))
            p++;
    }
    if (p < fim && *p == '.') {
        p++;
        if (p >= fim || !isdigit((unsigned char)*p))
            return NULL;
        while (p < fim && isdigit((unsigned char)*p))
            p++;
    }
    if (p < fim && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < fim && (*p == '+' || *p == '-'))
            p++;
        if (p >= fim || !isdigit((unsigned char)*p))
            return NULL;
        while (p < fim && isdigit((unsigned char)*p))
            p++;
    }
    v->tipo = JSON_NUM;
    v->u.num = strtod(inicio, &depois);
    if (depois != p)
        return NULL;                  /* strtod parou antes: algo estranho */
    return p;
}

static const char *parse_array(arena *ar, const char *p, const char *fim,
                               int prof, json_val **saida)
{
    json_val *v = novo_val(ar, JSON_ARR);

    if (!v)
        return NULL;
    p = pula_branco(p + 1, fim);      /* pula o '[' */
    if (p < fim && *p == ']') {
        *saida = v;
        return p + 1;
    }
    for (;;) {
        json_val *item;
        json_val **itens;

        item = parse_valor(ar, &p, fim, prof + 1);
        if (!item)
            return NULL;
        itens = cresce(ar, v->u.arr.itens, &v->u.arr.cap,
                       sizeof(*v->u.arr.itens), v->u.arr.n + 1);
        if (!itens)
            return NULL;
        v->u.arr.itens = itens;
        v->u.arr.itens[v->u.arr.n++] = item;

        p = pula_branco(p, fim);
        if (p >= fim)
            return NULL;              /* array cortado */
        if (*p == ',') {
            p = pula_branco(p + 1, fim);
            continue;
        }
        if (*p == ']') {
            *saida = v;
            return p + 1;
        }
        return NULL;                  /* separador inválido */
    }
}

static const char *parse_objeto(arena *ar, const char *p, const char *fim,
                                int prof, json_val **saida)
{
    json_val *v = novo_val(ar, JSON_OBJ);

    if (!v)
        return NULL;
    p = pula_branco(p + 1, fim);      /* pula o '{' */
    if (p < fim && *p == '}') {
        *saida = v;
        return p + 1;
    }
    for (;;) {
        char *chave;
        json_val *valor;
        char **chaves;
        json_val **valores;

        p = pula_branco(p, fim);
        chave = NULL;
        p = parse_str(ar, p, fim, &chave);
        if (!p)
            return NULL;
        p = pula_branco(p, fim);
        if (p >= fim || *p != ':')
            return NULL;              /* dois pontos faltando */
        p = pula_branco(p + 1, fim);
        valor = parse_valor(ar, &p, fim, prof + 1);
        if (!valor)
            return NULL;

        chaves = cresce(ar, v->u.obj.chaves, &v->u.obj.cap_chaves,
                        sizeof(*v->u.obj.chaves), v->u.obj.n + 1);
        if (!chaves)
            return NULL;
        v->u.obj.chaves = chaves;
        valores = cresce(ar, v->u.obj.valores, &v->u.obj.cap_valores,
                         sizeof(*v->u.obj.valores), v->u.obj.n + 1);
        if (!valores)
            return NULL;
        v->u.obj.valores = valores;
        v->u.obj.chaves[v->u.obj.n] = chave;
        v->u.obj.valores[v->u.obj.n] = valor;
        v->u.obj.n++;

        p = pula_branco(p, fim);
        if (p >= fim)
            return NULL;              /* objeto cortado */
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            *saida = v;
            return p + 1;
        }
        return NULL;                  /* separador inválido */
    }
}

static json_val *parse_valor(arena *ar, const char **pp, const char *fim,
                             int prof)
{
    const char *p = pula_branco(*pp, fim);
    json_val *v = NULL;

    if (p >= fim || prof > JSON_PROFUNDIDADE_MAX)
        return NULL;

    switch (*p) {
    case '{':
        p = parse_objeto(ar, p, fim, prof, &v);
        if (!p)
            return NULL;
        break;
    case '[':
        p = parse_array(ar, p, fim, prof, &v);
        if (!p)
            return NULL;
        break;
    case '"': {
        char *s;
        const char *fim_str = parse_str(ar, p, fim, &s);
        if (!fim_str)
            return NULL;
        v = novo_val(ar, JSON_STR);
        if (!v)
            return NULL;
        v->u.str = s;
        p = fim_str;
        break;
    }
    case 't':
        if (fim - p < 4 || memcmp(p, "true", 4) != 0)
            return NULL;
        v = novo_val(ar, JSON_BOOL);
        if (!v)
            return NULL;
        v->u.booleano = 1;
        p += 4;
        break;
    case 'f':
        if (fim - p < 5 || memcmp(p, "false", 5) != 0)
            return NULL;
        v = novo_val(ar, JSON_BOOL);
        if (!v)
            return NULL;
        v->u.booleano = 0;
        p += 5;
        break;
    case 'n':
        if (fim - p < 4 || memcmp(p, "null", 4) != 0)
            return NULL;
        v = novo_val(ar, JSON_NULO);
        if (!v)
            return NULL;
        p += 4;
        break;
    default:
        if (*p != '-' && !isdigit((unsigned char)*p))
            return NULL;
        v = novo_val(ar, JSON_NUM);
        if (!v)
            return NULL;
        p = parse_num(p, fim, v);
        if (!p)
            return NULL;
        break;
    }
    *pp = p;
    return v;
}

/* ------------------------------------------------------------------ */
/* interface                                                           */
/* ------------------------------------------------------------------ */

json_val *json_parse(const char *buf, size_t len)
{
    arena *ar;
    json_val *raiz;
    char *copia;
    const char *p, *fim;

    if (!buf || len == 0)
        return NULL;

    /* BOM UTF-8 no começo do arquivo: o IBGE não manda, mas alguns
     * exportadores de JSON acrescentam e ele não é parte do documento. */
    if (len >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        buf += 3;
        len -= 3;
        if (len == 0)
            return NULL;
    }

    /* O descritor do arena vai na heap, não na pilha: cada nó guarda o
     * ponteiro para ele e a árvore vive depois de json_parse() voltar. */
    ar = malloc(sizeof(*ar));
    if (!ar)
        return NULL;
    ar->raiz = NULL;
    ar->nos = 0;

    copia = arena_aloca(ar, len + 1);
    if (!copia) {
        free(ar);
        return NULL;
    }
    memcpy(copia, buf, len);
    copia[len] = '\0';

    p = copia;
    fim = copia + len;                 /* exclusive */
    raiz = parse_valor(ar, &p, fim, 1);
    if (!raiz) {
        arena_libera(ar);
        free(ar);
        return NULL;
    }
    /* Lixo depois do documento é ignorado de propósito: o corpo vem de um
     * arquivo em cache que pode ter lixo anexado. */
    return raiz;
}

void json_free(json_val *v)
{
    arena *ar;

    if (!v)
        return;
    ar = v->ar;
    arena_libera(ar);
    free(ar);
}

json_tipo json_tipo_de(const json_val *v)
{
    return v ? v->tipo : JSON_NULO;
}

const json_val *json_obj_get(const json_val *v, const char *chave)
{
    size_t i;

    if (!v || v->tipo != JSON_OBJ || !chave)
        return NULL;
    for (i = 0; i < v->u.obj.n; i++) {
        if (strcmp(v->u.obj.chaves[i], chave) == 0)
            return v->u.obj.valores[i];
    }
    return NULL;
}

size_t json_arr_len(const json_val *v)
{
    return (v && v->tipo == JSON_ARR) ? v->u.arr.n : 0;
}

const json_val *json_arr_get(const json_val *v, size_t i)
{
    if (!v || v->tipo != JSON_ARR || i >= v->u.arr.n)
        return NULL;
    return v->u.arr.itens[i];
}

const char *json_str(const json_val *v)
{
    return (v && v->tipo == JSON_STR) ? v->u.str : "";
}

double json_num(const json_val *v)
{
    return (v && v->tipo == JSON_NUM) ? v->u.num : 0.0;
}

long json_bool(const json_val *v)
{
    return (v && v->tipo == JSON_BOOL) ? v->u.booleano : 0;
}

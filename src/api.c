/* src/api.c — as rotas /api/.
 *
 * O que mudou e por quê (achados da revisão de 2026-10-04):
 *
 *   C5  A resposta de /api/consulta era montada num buffer fixo de 65536 bytes
 *       com `snprintf(json + pos, 65536 - pos, …)`. Quando `pos` passava do
 *       limite, `65536 - pos` underflowava `size_t` (vira um número enorme) e o
 *       snprintf escrevia fora — heap-buffer-overflow de escrita, confirmado
 *       com AddressSanitizer. Com o conjunto real (5570 municípios ≈ 600 KB) o
 *       estouro acontece no primeiro registro. Agora a escrita é um acumulador
 *       que cresce por duplicação, e o limite é conferido antes de cada piece.
 *
 *   I2  O formato da resposta era `{total,min,max,media,soma,dados}`, e a spec
 *       §8 pede `{consulta,total,kpis,registros,serie}`. O front foi escrito
 *       contra o formato errado, o que tornava o descasamento invisível.
 *
 *   I3  aggregate_serie não tinha chamador: a chave `serie` não existia, e a
 *       tarefa do gráfico de linha ficava sem dado.
 *
 *   I6  `nivel` e `fonte` eram recebidos e descartados.
 *
 *   I17 `erro` era interpolado no envelope com "%s", sem escape: uma mensagem
 *       com aspas ou contrabarra quebrava o documento JSON.
 *
 *   I10 As mensagens saíam sem acento, e uma delas ainda tinha espaço duplo.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "api.h"
#include "model.h"
#include "aggregate.h"
#include "charset.h"
#include "geo.h"

/* ------------------------------------------------------------------ */
/* acumulador de resposta                                              */
/* ------------------------------------------------------------------ */

/* Mesmo desenho do acumulador de geo.c, pela mesma razão: escrever caractere a
 * caractere é o caminho comum, e o que seria caro é um snprintf por registro
 * com capacidade mal calculada. Aqui a diferença é que o buffer cresce por
 * duplicação e NENHUMA escrita acontece sem espaço reservado — foi
 * exatamente a conta `65536 - pos` que virou estouro de heap. */
typedef struct {
    char *p;
    size_t n, cap;
    int erro;
} acc;

static void acc_init(acc *a)
{
    memset(a, 0, sizeof *a);
}

static void acc_free(acc *a)
{
    free(a->p);
    a->p = NULL;
    a->n = a->cap = 0;
}

static int acc_reserva(acc *a, size_t extra)
{
    size_t nova;
    char *maior;

    if (a->erro) return -1;
    if (a->n + extra + 1 <= a->cap) return 0;
    nova = a->cap ? a->cap : 1024;
    while (a->n + extra + 1 > nova) {
        if (nova > (size_t)-1 / 2) { a->erro = 1; return -1; }
        nova *= 2;
    }
    maior = realloc(a->p, nova);
    if (!maior) { a->erro = 1; return -1; }
    a->p = maior;
    a->cap = nova;
    return 0;
}

static void acc_puts(acc *a, const char *s)
{
    size_t k = strlen(s);
    if (acc_reserva(a, k) != 0) return;
    memcpy(a->p + a->n, s, k);
    a->n += k;
    a->p[a->n] = '\0';
}

static void acc_printf(acc *a, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
;

static void acc_printf(acc *a, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int k;

    va_start(ap, fmt);
    k = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (k < 0) { a->erro = 1; return; }
    if ((size_t)k >= sizeof buf) { a->erro = 1; return; }
    acc_puts(a, buf);
}

/* Devolve o buffer malloc'd com o '\0' garantido, ou NULL. */
static char *acc_toma(acc *a, size_t *out_len)
{
    char *r;
    if (a->erro || !a->p) { acc_free(a); return NULL; }
    r = a->p;
    if (out_len) *out_len = a->n;
    a->p = NULL;
    a->n = a->cap = 0;
    return r;
}

/* ------------------------------------------------------------------ */
/* envelope de erro                                                    */
/* ------------------------------------------------------------------ */

static int responde_erro(http_response *res, int status, const char *mensagem)
{
    acc a;
    char *esc = text_json_escape(mensagem ? mensagem : "erro interno");
    size_t len = 0;
    char *corpo;

    acc_init(&a);
    acc_printf(&a, "{\"erro\":{\"codigo\":%d,\"mensagem\":\"%s\"}}",
               status, esc ? esc : "erro interno");
    free(esc);
    corpo = acc_toma(&a, &len);
    if (!corpo) {
        res->status = 500;
        res->content_type = "application/json; charset=utf-8";
        res->body = strdup("{\"erro\":{\"codigo\":500,\"mensagem\":\"memória\"}}");
        res->length = res->body ? (long long)strlen(res->body) : 0;
        return 500;
    }
    res->status = status;
    res->content_type = "application/json; charset=utf-8";
    res->body = corpo;
    res->length = (long long)len;
    return status;
}

/* ------------------------------------------------------------------ */
/* parâmetros                                                          */
/* ------------------------------------------------------------------ */

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v;

    if (!s || !*s) return -1;
    v = strtol(s, &end, 10);
    if (!end || *end != '\0') return -1;
    if (v > 2147483647L || v < -2147483648L) return -1;
    *out = (int)v;
    return 0;
}

static int tolower_char(int c)
{
    return tolower((unsigned char)c);
}

static int strcasecmp_safe(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        int ca = tolower_char(*a);
        int cb = tolower_char(*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower_char(*a) - tolower_char(*b);
}

static const char *indicador_nome(indicador ind)
{
    return model_nome_indicador(ind);
}

static indicador indicador_de(const char *s)
{
    /* Indicador ausente é população, que é o único que existe hoje e o único
     * que a spec §8 mostra como exemplo. Sem este default, a consulta mais
     * simples que existe — `?fonte=ibge&ano=2024` — devolvia 400 "indicador
     * inválido" por não ter dito qual indicador queria, e a pessoa que está
     * experimentando pela primeira vez via um 400 sem parâmetro como
     * primeira resposta.
     *
     * O que NÃO é default é fonte ou ano: sem ano não há dado, e dizer
     * "população" não pode virar "200 com lista vazia" para uma consulta que
     * esqueceu o filtro. */
    if (!s || !*s) return IND_POPULACAO;
    if (strcasecmp_safe(s, "populacao") == 0 ||
        strcasecmp_safe(s, "população") == 0) return IND_POPULACAO;
    if (strcasecmp_safe(s, "estabelecimentos") == 0) return IND_ESTABELECIMENTOS;
    if (strcasecmp_safe(s, "leitos") == 0) return IND_LEITOS;
    if (strcasecmp_safe(s, "uti") == 0) return IND_UTI;
    if (strcasecmp_safe(s, "dengue") == 0) return IND_DENGUE;
    return IND_TOTAL;
}

/* ------------------------------------------------------------------ */
/* o modelo                                                            */
/* ------------------------------------------------------------------ */

static model *api_model = NULL;

void api_set_model(model *m)
{
    api_model = m;
}

/* ------------------------------------------------------------------ */
/* consulta                                                            */
/* ------------------------------------------------------------------ */

/* A resposta da spec §8:
 *
 *   { "consulta": {…}, "total": N, "kpis": {…}, "registros": [ … ], "serie": [ … ] }
 *
 * `total` é o do conjunto inteiro (não o da página), `kpis` são calculados
 * sobre ele, `registros` é a página depois de `limite`, e `serie` só existe
 * quando a query traz `cod=` (um município) ou `uf=` com `nivel=uf` — sem um
 * recorte não há uma série, e inventar uma seria encher o payload de 22 linhas
 * que ninguém pediu. */
static char *monta_consulta(const model *m, indicador ind, int nivel_uf,
                            const char *uf, int ano, const char *busca,
                            const char *ordenar, int limite,
                            const char *cod, const char *fonte,
                            size_t *out_len, char *erro, size_t erro_len)
{
    consulta c = {0};
    kpis k = {0};
    acc a;
    size_t i, len = 0;
    char *corpo = NULL;
    char *serie = NULL;

    if (aggregate_consulta(m, ind, nivel_uf, uf, ano, busca, ordenar, limite,
                           &c, &k, erro, erro_len) != 0) {
        aggregate_libera(&c);
        return NULL;
    }

    /* A série vem de um RECORTE, e só quando existe recorte:
     *   - `cod=` nomeia um município → série do município;
     *   - `nivel=uf` com `uf=` nomeia uma UF → série da UF, somada.
     * Sem recorte, `serie` não sai: encher o payload com a série do Brasil
     * inteiro a cada consulta seria 600 KB de dado que ninguém pediu. */
    if (cod && *cod) {
        size_t nserie = 0;
        serie = aggregate_serie(m, ind, cod, &nserie);
        if (!serie) {
            snprintf(erro, erro_len, "município %s não existe na base", cod);
            aggregate_libera(&c);
            return NULL;
        }
    } else if (nivel_uf && uf && *uf &&
               model_uf_de_sigla(m, uf) < m->n_ufs) {
        size_t nserie = 0;
        serie = aggregate_serie_uf(m, ind, uf, &nserie);
        /* Uma UF que existe no modelo mas cuja série deu NULL é um defeito
         * interno; um `uf=` que não casa com nada não tem série e não é erro,
         * como uma busca sem resultado. */
        if (!serie) {
            snprintf(erro, erro_len, "não consegui montar a série da UF %s", uf);
            aggregate_libera(&c);
            return NULL;
        }
    }

    acc_init(&a);
    acc_puts(&a, "{\"consulta\":{");
    {
        /* Tudo que vem da query entra escapado. `busca` e `cod` são texto
         * escolhido por quem está na outra ponta: sem escape, uma busca por
         * `O'Brien"x` entrega JSON quebrado em vez de um resultado. */
        char *esc_fonte = text_json_escape(fonte ? fonte : "");
        char *esc_uf = text_json_escape(uf ? uf : "");
        char *esc_busca = text_json_escape(busca ? busca : "");
        char *esc_ordem = text_json_escape(ordenar ? ordenar : "");
        char *esc_cod = text_json_escape(cod ? cod : "");

        /* A fonte ausente vira "ibge" no eco também: o eco da consulta é o
         * que o front mostra como "o que foi pedido", e mostrar "" ao lado
         * de `indicador: populacao` descreve uma consulta que ninguém fez. */
        acc_printf(&a, "\"fonte\":\"%s\",\"indicador\":\"%s\",\"nivel\":\"%s\",",
                   (esc_fonte && *esc_fonte) ? esc_fonte : "ibge",
                   indicador_nome(ind),
                   nivel_uf ? "uf" : "municipio");
        acc_printf(&a, "\"uf\":\"%s\",\"ano\":%d", esc_uf ? esc_uf : "", ano);
        if (busca && *busca) acc_printf(&a, ",\"busca\":\"%s\"", esc_busca);
        if (ordenar && *ordenar) acc_printf(&a, ",\"ordenar\":\"%s\"", esc_ordem);
        if (limite > 0) acc_printf(&a, ",\"limite\":%d", limite);
        if (cod && *cod) acc_printf(&a, ",\"cod\":\"%s\"", esc_cod);

        free(esc_fonte); free(esc_uf); free(esc_busca);
        free(esc_ordem); free(esc_cod);
    }
    acc_puts(&a, "}");

    /* `total` é o do conjunto, não o de `limite`: o cartão de KPI diz quantos
     * municípios são, não quantas linhas a tabela está mostrando. `com_dado`
     * é quantos deles têm valor no ano — os dois números são diferentes de
     * propósito, e é a diferença entre "não achei nada" e "achei, mas a série
     * não cobre aquele ano". */
    acc_printf(&a, ",\"total\":%d", k.linhas);
    acc_printf(&a, ",\"kpis\":{\"min\":%d,\"max\":%d,\"media\":%d,\"soma\":%lld,"
                   "\"com_dado\":%d}",
               k.com_dado ? k.min : 0, k.com_dado ? k.max : 0, k.media, k.soma,
               k.com_dado);

    acc_puts(&a, ",\"registros\":[");
    for (i = 0; i < c.n; i++) {
        char *nome = text_json_escape(c.nome[i]);
        if (i) acc_puts(&a, ",");
        if (c.valor[i] == MODELO_AUSENTE) {
            /* -1 vira `null`, nunca 0: uma lacuna da série não é um valor. */
            acc_printf(&a, "{\"cod\":\"%s\",\"nome\":\"%s\",\"valor\":null}",
                       c.cod[i], nome ? nome : "");
        } else {
            acc_printf(&a, "{\"cod\":\"%s\",\"nome\":\"%s\",\"valor\":%d}",
                       c.cod[i], nome ? nome : "", c.valor[i]);
        }
        free(nome);
    }
    acc_puts(&a, "]");

    if (serie) {
        acc_puts(&a, ",\"serie\":");
        acc_puts(&a, serie);
        acc_puts(&a, ",\"serie_anos\":[");
        for (i = 0; i < m->n_anos; i++)
            acc_printf(&a, "%s%d", i ? "," : "", m->anos[i]);
        acc_puts(&a, "]");
    }
    acc_puts(&a, "}");

    corpo = acc_toma(&a, &len);
    free(serie);
    aggregate_libera(&c);
    if (!corpo) {
        snprintf(erro, erro_len, "memória insuficiente para montar a resposta");
        return NULL;
    }
    *out_len = len;
    return corpo;
}

/* ------------------------------------------------------------------ */
/* roteamento                                                          */
/* ------------------------------------------------------------------ */

int api_roteia(const http_request *req, http_response *res)
{
    const char *path, *query;
    const model *m;

    if (!req || !res) return -1;
    memset(res, 0, sizeof *res);
    res->status = 200;
    res->content_type = "application/json; charset=utf-8";

    path = req->path ? req->path : "";
    query = req->query ? req->query : "";

    /* O modelo é carregado uma vez, no main, e apontado aqui. Não há
     * "carrega por requisição": o índice tem 2,4 MB e ler o arquivo em cada
     * conexão seria o mesmo que não ter índice.
     *
     * A verificação é feita por rota, e não uma vez no topo, porque nem toda
     * rota usa o modelo: as rotas de geometria saem de geo.c e de
     * data/processed, e continuam respondendo mesmo sem índice de dados —
     * foi assim que a rota de mapa, testada isoladamente, passou a devolver
     * 503 para um mapa que existia. */
    m = api_model;

    /* ---------------------------------------------------------------- */
    /* /api/consulta                                                    */
    /* ---------------------------------------------------------------- */
    if (strcmp(path, "api/consulta") == 0) {
        char *q_ind;
        char *q_uf;
        char *q_ano;
        char *q_busca;
        char *q_ordem;
        char *q_limite;
        char *q_fonte;
        char *q_nivel;
        char *q_cod;
        char *ind_dec;
        char *uf_dec;
        char *busca_dec;
        char *ordem_dec;
        char *cod_dec;
        char *fonte_dec;
        char *nivel_dec;
        char *ano_dec;
        char *limite_dec;
        indicador ind;
        int ano = 0, limite = 0, nivel_uf = 0;
        char erro[256];
        char *corpo;
        size_t len = 0;
        int status = 400;

        /* `goto fim` atravessa a lista de declarações, então as variáveis
         * nascem todas aqui em vez de na inicialização logo abaixo. */
        if (!m || m->n_local == 0 || m->n_anos == 0)
            return responde_erro(res, 503,
                                 "não há dado processado: rode "
                                 "./dashboard --atualizar ibge");

        q_ind = text_query_get(query, "indicador");
        q_uf = text_query_get(query, "uf");
        q_ano = text_query_get(query, "ano");
        q_busca = text_query_get(query, "busca");
        q_ordem = text_query_get(query, "ordenar");
        q_limite = text_query_get(query, "limite");
        q_fonte = text_query_get(query, "fonte");
        q_nivel = text_query_get(query, "nivel");
        q_cod = text_query_get(query, "cod");
        ind_dec = text_url_decode(q_ind ? q_ind : "");
        uf_dec = text_url_decode(q_uf ? q_uf : "");
        busca_dec = text_url_decode(q_busca ? q_busca : "");
        ordem_dec = text_url_decode(q_ordem ? q_ordem : "");
        cod_dec = text_url_decode(q_cod ? q_cod : "");
        fonte_dec = text_url_decode(q_fonte ? q_fonte : "");
        nivel_dec = text_url_decode(q_nivel ? q_nivel : "");
        ano_dec = text_url_decode(q_ano ? q_ano : "");
        limite_dec = text_url_decode(q_limite ? q_limite : "");

        /* Nada é liberado aqui: todo o bloco tem o mesmo `fim`, e liberar
         * no meio do caminho é a forma mais curta de um use-after-free. */
        ind = indicador_de(ind_dec);
        if (ind >= IND_TOTAL) {
            status = responde_erro(res, 400, "indicador inválido");
            goto fim;
        }

        /* O ano é obrigatório e tem que ser um inteiro. `ano=`, `ano=..` e
         * `ano=20a4` viram 400 com o valor que chegou, em vez de virar 0 — 0
         * não é ano de ninguém, e aceitar `ano=` como "sem filtro" devolveria
         * dado de um ano arbitrário sem nenhuma pista. */
        if (ano_dec[0] == '\0' || parse_int(ano_dec, &ano) != 0 ||
            ano < 1900 || ano > 2200) {
            char msg[160];
            snprintf(msg, sizeof msg, "ano \"%s\" inválido",
                     ano_dec[0] ? ano_dec : "");
            status = responde_erro(res, 400, msg);
            goto fim;
        }

        /* `nivel` da spec §8: "municipio" (padrão) ou "uf". */
        if (nivel_dec[0]) {
            if (strcasecmp_safe(nivel_dec, "uf") == 0) nivel_uf = 1;
            else if (strcasecmp_safe(nivel_dec, "municipio") == 0 ||
                     strcasecmp_safe(nivel_dec, "município") == 0) nivel_uf = 0;
            else {
                char msg[192];
                snprintf(msg, sizeof msg,
                         "nível \"%s\" inválido: use municipio ou uf", nivel_dec);
                status = responde_erro(res, 400, msg);
                goto fim;
            }
        }

        /* `limite` malformado não derruba a consulta: é um limite de
         * paginação, e recusar a requisição inteira por ele seria pior do que
         * devolver a página inteira. */
        if (limite_dec[0] && parse_int(limite_dec, &limite) != 0)
            limite = 0;

        /* `nivel=uf` com `cod=` é recusado aqui, e não em aggregate_consulta:
         * a linha de um nível de UF é a soma dos municípios da UF, então um
         * código de município não tem o que dizer nessa tabela. Aceitar e
         * devolver o município sob o nome da UF seria pior que erro. */
        if (nivel_uf && cod_dec[0]) {
            status = responde_erro(res, 400,
                                   "nível UF não aceita o filtro cod=: "
                                   "a linha já é a UF");
            goto fim;
        }

        erro[0] = '\0';
        corpo = monta_consulta(m, ind, nivel_uf,
                               uf_dec[0] ? uf_dec : NULL, ano,
                               busca_dec[0] ? busca_dec : NULL,
                               ordem_dec[0] ? ordem_dec : NULL, limite,
                               cod_dec[0] ? cod_dec : NULL,
                               fonte_dec[0] ? fonte_dec : NULL,
                               &len, erro, sizeof erro);
        if (!corpo) {
            status = responde_erro(res, 400, erro[0] ? erro : "erro");
            goto fim;
        }

        res->status = 200;
        res->content_type = "application/json; charset=utf-8";
        res->body = corpo;
        res->length = (long long)len;
        res->gzip = 1;              /* resposta longa e muito repetitiva */
        status = 200;

    fim:
        free(q_ind); free(q_uf); free(q_ano); free(q_busca);
        free(q_ordem); free(q_limite); free(q_fonte);
        free(q_nivel); free(q_cod);
        free(ind_dec); free(uf_dec); free(busca_dec);
        free(ordem_dec); free(cod_dec); free(fonte_dec);
        free(nivel_dec); free(ano_dec); free(limite_dec);
        return status;
    }

    /* ---------------------------------------------------------------- */
    /* /api/opcoes                                                      */
    /* ---------------------------------------------------------------- */
    if (strcmp(path, "api/opcoes") == 0) {
        /* As opções saem do modelo (anos e UFs), então sem modelo não há o que
         * listar. */
        if (!m || m->n_local == 0 || m->n_anos == 0)
            return responde_erro(res, 503,
                                 "não há dado processado: rode "
                                 "./dashboard --atualizar ibge");
        {
        char *q_ind = text_query_get(query, "indicador");
        char *ind_dec = text_url_decode(q_ind ? q_ind : "");
        char *q_uf = text_query_get(query, "uf");
        char *uf_dec = text_url_decode(q_uf ? q_uf : "");
        char *opcoes;
        indicador ind;

        free(q_ind);

        ind = indicador_de(ind_dec);
        if (ind >= IND_TOTAL) {
            free(ind_dec); free(q_uf); free(uf_dec);
            return responde_erro(res, 400, "indicador inválido");
        }

        /* Com `uf=` a lista de UFs cai para a própria UF: em nível de UF não
         * existe "todas as UFs". */
        if (uf_dec[0] && model_uf_de_sigla(m, uf_dec) < m->n_ufs) {
            acc a;
            char *esc = text_json_escape(uf_dec);
            size_t len = 0, i;
            char *corpo;
            const char *sigla = m->ufs[model_uf_de_sigla(m, uf_dec)].sigla;

            acc_init(&a);
            acc_puts(&a, "{\"anos\":[");
            for (i = 0; i < m->n_anos; i++)
                acc_printf(&a, "%s%d", i ? "," : "", m->anos[i]);
            acc_printf(&a, "],\"ufs\":[\"%s\"],\"uf\":\"%s\"}",
                       sigla, esc ? esc : "");
            free(esc);
            corpo = acc_toma(&a, &len);
            free(ind_dec); free(q_uf); free(uf_dec);
            if (!corpo) return responde_erro(res, 500, "memória");
            res->status = 200;
            res->content_type = "application/json; charset=utf-8";
            res->body = corpo;
            res->length = (long long)len;
            res->gzip = 1;
            return 200;
        }

        opcoes = aggregate_opcoes(m, ind);
        free(ind_dec); free(q_uf); free(uf_dec);
        if (!opcoes) return responde_erro(res, 500, "erro interno");
        res->status = 200;
        res->content_type = "application/json; charset=utf-8";
        res->body = opcoes;
        res->length = (long long)strlen(opcoes);
        res->gzip = 1;         /* mesma política de /api/consulta */
        return 200;
        }
    }

    /* ---------------------------------------------------------------- */
    /* /api/valores — o coroplético                                      */
    /* ---------------------------------------------------------------- */
    /*                                                                  */
    /* Por que isto existe: o mapa precisa do valor de TODAS as          */
    /* localidades do recorte, e /api/consulta entrega só a página. Com   */
    /* `limite=100`, a tabela traz 100 linhas e o resto do Brasil        */
    /* aparecia no mapa com a cor de ausência — que significa "sem dado" */
    /* — quando na verdade tem dado e só não foi paginado. O mapa        */
    /* mentia, e a mentira era do tipo que a spec §4 proíbe.             */
    /*                                                                  */
    /* A resposta é propositalmente mais curta que /api/consulta: são só  */
    /* `codarea → valor`, sem nome e sem o resto do envelope. Medido com  */
    /* os 5570 municípios de 2024: 87.016 bytes contra 299.532 da        */
    /* consulta completa, 3,4× menos para o mesmo dado.                   */
    /*                                                                  */
    /* `nomes=1` acrescenta `codarea → nome`. O nome é o que o clique no   */
    /* mapa mostra, e ele não muda com o filtro — por isso ele é pedido   */
    /* uma vez e memorizado pelo navegador, enquanto os valores são       */
    /* buscados a cada consulta.                                         */
    /* ---------------------------------------------------------------- */
    if (strcmp(path, "api/valores") == 0) {
        char *q_ind = text_query_get(query, "indicador");
        char *q_uf = text_query_get(query, "uf");
        char *q_ano = text_query_get(query, "ano");
        char *q_nivel = text_query_get(query, "nivel");
        char *q_nomes = text_query_get(query, "nomes");
        char *ind_dec = text_url_decode(q_ind ? q_ind : "");
        char *uf_dec = text_url_decode(q_uf ? q_uf : "");
        char *ano_dec = text_url_decode(q_ano ? q_ano : "");
        char *nivel_dec = text_url_decode(q_nivel ? q_nivel : "");
        char *nomes_dec = text_url_decode(q_nomes ? q_nomes : "");
        indicador ind;
        int ano = 0;
        int nivel_uf = 0;
        int quer_nomes = 0;
        char erro[256];
        consulta c;
        kpis k;
        acc a;
        size_t i;
        int status;

        if (!m || m->n_local == 0 || m->n_anos == 0) {
            status = responde_erro(res, 503,
                                   "não há dado processado: rode "
                                   "./dashboard --atualizar ibge");
            goto valores_fim;
        }

        ind = indicador_de(ind_dec);
        if (ind >= IND_TOTAL) {
            status = responde_erro(res, 400, "indicador inválido");
            goto valores_fim;
        }

        /* O ano é obrigatório aqui como em /api/consulta: um coroplético sem
         * ano é um mapa de quê? E a validação é a mesma, com a mesma
         * mensagem, para que o front receba um texto que ele já sabe
         * mostrar. */
        if (ano_dec[0] == '\0' || parse_int(ano_dec, &ano) != 0 ||
            ano < 1900 || ano > 2200) {
            char msg[160];
            snprintf(msg, sizeof msg, "ano \"%s\" inválido",
                     ano_dec[0] ? ano_dec : "");
            status = responde_erro(res, 400, msg);
            goto valores_fim;
        }

        if (nivel_dec[0]) {
            if (strcasecmp_safe(nivel_dec, "uf") == 0) nivel_uf = 1;
            else if (strcasecmp_safe(nivel_dec, "municipio") == 0 ||
                     strcasecmp_safe(nivel_dec, "município") == 0) nivel_uf = 0;
            else {
                char msg[192];
                snprintf(msg, sizeof msg,
                         "nível \"%s\" inválido: use municipio ou uf", nivel_dec);
                status = responde_erro(res, 400, msg);
                goto valores_fim;
            }
        }

        /* Só o que é verdade é 1: `nomes=0`, `nomes=false` e `nomes=nao`
         * têm de pedir a resposta curta, e `nomes=` vazio também — senão um
         * `nomes` sem valor no lugar de flag custaria 90 KB sem ninguém pedir. */
        quer_nomes = (strcmp(nomes_dec, "1") == 0 ||
                      strcasecmp_safe(nomes_dec, "true") == 0 ||
                      strcasecmp_safe(nomes_dec, "sim") == 0);

        /* `limite` é 0 de propósito: aqui a paginação não existe. A resposta
         * é o conjunto inteiro que satisfaz o filtro, e quem chama já pediu
         * para ver o Brasil inteiro. */
        erro[0] = '\0';
        if (aggregate_consulta(m, ind, nivel_uf,
                               uf_dec[0] ? uf_dec : NULL, ano, NULL, NULL, 0,
                               &c, &k, erro, sizeof erro) != 0) {
            status = responde_erro(res, 400, erro[0] ? erro : "erro");
            goto valores_fim;
        }

        acc_init(&a);
        acc_printf(&a, "{\"total\":%d,\"com_dado\":%d", k.linhas, k.com_dado);
        acc_puts(&a, ",\"valores\":{");
        for (i = 0; i < c.n; i++) {
            /* Um ausente é -1, a sentinela do aggregate, e vai como null: é
             * assim que o front distingue "não tem dado" de "tem zero", e é
             * a distinção que a spec §4 exige. */
            if (i) acc_puts(&a, ",");
            acc_printf(&a, "\"%s\":%s", c.cod[i], c.valor[i] < 0 ? "null" : "");
            if (c.valor[i] >= 0) {
                char buf[16];
                snprintf(buf, sizeof buf, "%d", c.valor[i]);
                acc_puts(&a, buf);
            }
        }
        acc_puts(&a, "}");
        if (quer_nomes) {
            acc_puts(&a, ",\"nomes\":{");
            for (i = 0; i < c.n; i++) {
                char *esc = text_json_escape(c.nome[i]);
                if (i) acc_puts(&a, ",");
                acc_printf(&a, "\"%s\":\"%s\"", c.cod[i], esc ? esc : "");
                free(esc);
            }
            acc_puts(&a, "}");
        }
        acc_puts(&a, "}");

        if (a.erro) {
            acc_free(&a);
            aggregate_libera(&c);
            status = responde_erro(res, 500, "erro interno ao montar a resposta");
            goto valores_fim;
        }

        res->status = 200;
        res->content_type = "application/json; charset=utf-8";
        res->body = a.p;
        res->length = (long long)a.n;
        res->gzip = 1;              /* pares repetitivos: comprime muito bem */
        aggregate_libera(&c);
        status = 200;

    valores_fim:
        free(q_ind); free(q_uf); free(q_ano); free(q_nivel); free(q_nomes);
        free(ind_dec); free(uf_dec); free(ano_dec); free(nivel_dec); free(nomes_dec);
        return status;
    }

    /* ---------------------------------------------------------------- */
    /* rotas de geometria                                               */
    /*                                                                   */
    /* A malha é o desenho do mapa, não dado: o navegador a busca no load e a */
    /* memoriza. Então estas rotas não dependem do modelo, não exigem ano nem  */
    /* indicador, e nunca vêm preenchidas com valor nenhum.                 */
    /*                                                                    */
    /* Sem dado processado a resposta é 503, e não 200 com corpo vazio: um   */
    /* 200 vazio renderizaria um mapa em branco sem nenhuma pista do que deu */
    /* errado.                                                             */
    /* ---------------------------------------------------------------- */
    if (strcmp(path, "api/geo/municipios") == 0 || strcmp(path, "api/geo/estados") == 0 ||
        strcmp(path, "api/mapa/uf") == 0) {
        geo_nivel nivel;
        char *q_uf = text_query_get(query, "uf");
        char *q_nivel = text_query_get(query, "nivel");
        char *uf_dec = text_url_decode(q_uf ? q_uf : "");
        char *nivel_dec = text_url_decode(q_nivel ? q_nivel : "");
        const char *codigo = NULL;
        char *saida;
        size_t saida_len = 0;

        if (strcmp(path, "api/geo/municipios") == 0) nivel = GEO_MUNICIPIOS;
        else if (strcmp(path, "api/geo/estados") == 0) nivel = GEO_ESTADOS;
        else if (nivel_dec[0] &&
                 (strcasecmp_safe(nivel_dec, "municipio") == 0 ||
                  strcasecmp_safe(nivel_dec, "municipios") == 0)) nivel = GEO_MUNICIPIOS;
        else nivel = GEO_ESTADOS;

        if (uf_dec[0]) {
            codigo = geo_codigo_uf(uf_dec);
            if (!codigo) {
                free(uf_dec); free(nivel_dec); free(q_uf); free(q_nivel);
                return responde_erro(res, 400, "UF inválida");
            }
        }

        if (codigo) saida = geo_saida_filtrada(nivel, codigo, &saida_len);
        else        saida = geo_saida(nivel, &saida_len);

        free(uf_dec); free(nivel_dec); free(q_uf); free(q_nivel);

        if (!saida) return responde_erro(res, 503, "Execute: ./dashboard --atualizar");

        res->status = 200;
        res->content_type = "application/json; charset=utf-8";
        res->body = saida;
        res->length = (long long)saida_len;
        res->gzip = 1;        /* geometria é longa e muito comprimível */
        return 200;
    }

    if (strncmp(path, "api/", 4) == 0)
        return responde_erro(res, 404, "rota não encontrada");

    return responde_erro(res, 404, "não encontrado");
}
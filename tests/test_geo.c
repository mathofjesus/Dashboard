/* tests/test_geo.c -- Tarefa 8: malhas do IBGE simplificadas e unidas.
 *
 * A suíte roda SEM REDE e SEM DADO REAL. O motivo é prático e também é o
 * contrato: data/cache/ e data/processed/ estão no .gitignore, então num clone
 * novo não existe malha nenhuma — um teste que exigisse o download de 5,78 MB
 * só passaria na máquina de quem desenvolveu, e `make test` deixaria de ser um
 * portão.
 *
 * Então o teste semeia as FONTES CRUAS no cache, com fixtures sintéticos no
 * formato exato que a API do IBGE devolve (formato confirmado contra a API:
 * chaves de topo `type`/`crs`/`features`; feature com `properties.codarea`
 * string e `properties.centroide` com 2 números; `geometry` Polygon ou
 * MultiPolygon) e então exercita geo_prepara/geo_saida de verdade, do cache
 * bruto até o GeoJSON final. O que se testa é o pipeline, não a malha real.
 *
 * A cobertura `codarea` <-> modelo fica de fora de propósito: o modelo real só
 * existe depois de `--atualizar`, e um teste que passasse em silêncio com o
 * modelo vazio seria pior do que nenhum.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>          /* getcwd, chdir */

#include "../src/geo.h"
#include "../src/cache.h"
#include "../src/json.h"
#include "../src/api.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

/* `http_response.body` e `const char *` porque o servidor tambem o preenche com
 * ponteiro para buffer estatico (res->filename). O corpo em memoria, esse, e
 * malloc'd e precisa ser devolvido — e `free()` de um `const char *` e um
 * `-Wdiscarded-qualifiers` em cada chamador, que e o jeito do compilador
 * avisar que a distincao existe. O helper e a distincao. */
static void libera_body(http_response *res)
{
    free((char *)res->body);
    res->body = NULL;
    res->length = 0;
}

/* ------------------------------------------------------------------ */
/* chaves e caminhos                                                   */
/* ------------------------------------------------------------------ */

#define K_MUN  "ibge_malhas_municipios"
#define K_EST1 "ibge_malhas_estados_1"
#define K_EST2 "ibge_malhas_estados_2"
#define K_EST3 "ibge_malhas_estados_3"
#define K_EST4 "ibge_malhas_estados_4"
#define K_EST5 "ibge_malhas_estados_5"

#define P_MUN "data/processed/geo_municipios.json"
#define P_EST "data/processed/geo_estados.json"

/* ------------------------------------------------------------------ */
/* fixtures sintéticos                                                 */
/* ------------------------------------------------------------------ */

/* Buffer de texto que cresce, para montar as fixtures. */
typedef struct {
    char *p;
    size_t n, cap;
} txt;

static void txt_init(txt *t)
{
    t->cap = 1024;
    t->p = malloc(t->cap);
    t->n = 0;
    if (t->p) t->p[0] = '\0';
}

static void txt_free(txt *t)
{
    free(t->p);
    t->p = NULL;
    t->n = t->cap = 0;
}

static void txt_puts(txt *t, const char *s)
{
    size_t k = strlen(s);
    if (!t->p || t->n + k + 1 > t->cap) {
        size_t nova = t->cap ? t->cap : 1024;
        char *maior;
        while (t->n + k + 1 > nova) nova *= 2;
        maior = realloc(t->p, nova);
        if (!maior) return;
        t->p = maior;
        t->cap = nova;
    }
    memcpy(t->p + t->n, s, k);
    t->n += k;
    t->p[t->n] = '\0';
}

static void txt_printf(txt *t, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    txt_puts(t, buf);
}

/* Anel fechado de n pontos, com oscilação de 0.01 grau em torno de (lon, lat):
 * ruído pequeno o bastante para a simplificação ter o que descartar, e grande
 * o bastante para que o teste perceba se ela não rodou. O primeiro e o último
 * ponto coincidem, como manda um anel. */
static void txt_anel(txt *t, double lon, double lat, int n)
{
    int i;
    txt_puts(t, "[");
    for (i = 0; i < n; i++) {
        double f = (double)i / (double)(n - 1);
        double x = lon + 0.01 * (f - 0.5);
        double y = lat + 0.01 * (i % 2 ? 1.0 : -1.0) * 0.5 * (1.0 - 2.0 * f * f);
        if (i) txt_puts(t, ",");
        /* 7 casas: as mesmas do IBGE, para o arredondamento a inteiro×1000
         * dar o mesmo resultado aqui e no dado real. */
        txt_printf(t, "[%.7f,%.7f]", x, y);
    }
    txt_puts(t, "]");
}

/* Abre uma FeatureCollection no formato do IBGE. */
static void txt_abre_fc(txt *t)
{
    txt_puts(t, "{\"type\":\"FeatureCollection\",\"crs\":{\"type\":\"name\","
                "\"properties\":{\"name\":\"urn:ogc:def:crs:OGC:1.3:CRS84\"}},\"features\":[");
}

/* Fecha a FeatureCollection. */
static void txt_fecha_fc(txt *t)
{
    txt_puts(t, "]}");
}

/* Uma feature completa: properties com codarea e centroide, geometry Polygon
 * com um anel. Separada em etapas porque o format string do C não lê bem com
 * esse volume de aspas e \\. */
static void txt_feature(txt *t, const char *codarea, double clon, double clat, int npts)
{
    txt_puts(t, "{\"type\":\"Feature\",\"properties\":{\"codarea\":\"");
    txt_puts(t, codarea);
    txt_puts(t, "\",\"centroide\":[");
    txt_printf(t, "%.15f,%.15f", clon, clat);
    txt_puts(t, "]},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[");
    txt_anel(t, clon, clat, npts);
    txt_puts(t, "]}}");
}

/* ------------------------------------------------------------------ */
/* helpers de verificação                                              */
/* ------------------------------------------------------------------ */

/* Conta ocorrências de uma substring. */
static int conta(const char *hay, const char *needle)
{
    int n = 0;
    size_t k = strlen(needle);
    const char *p = hay;
    if (!hay || !needle || !*needle) return 0;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += k;
    }
    return n;
}

/* Conta features procurando "\"type\":\"Feature\"" — o prefixo que o geo.c
 * emite logo antes de cada objeto de feature. */
static int conta_features(const char *s)
{
    return conta(s, "\"type\":\"Feature\"");
}

/* Verifica que toda ocorrência de "centroide":[...] tem exatamente dois
 * números dentro do colchete. É o que o brief pede: "toda feature deve ter
 * centroide com 2 números". */
static void checa_centroides(const char *s)
{
    const char *p = s;
    int n = 0;
    while ((p = strstr(p, "\"centroide\":")) != NULL) {
        const char *a = strchr(p, '[');
        const char *b;
        int nnums = 0;
        if (!a) {
            fprintf(stderr, "FALHOU: centroide sem '['\n");
            exit(1);
        }
        b = a + 1;
        while (*b && *b != ']') {
            if ((*b >= '0' && *b <= '9') || *b == '-') {
                nnums++;
                while (*b && *b != ',' && *b != ']') b++;
            } else if (*b == ',') {
                b++;
            } else {
                break;
            }
        }
        if (*b != ']') {
            fprintf(stderr, "FALHOU: centroide sem ']'\n");
            exit(1);
        }
        if (nnums != 2) {
            fprintf(stderr, "FALHOU: centroide com %d numeros, esperado 2\n", nnums);
            exit(1);
        }
        n++;
        p = b;
    }
    T_ASSERT(n > 0, "deve haver pelo menos um centroide");
}

/* Confere que a saída é JSON de verdade e que a estrutura é a que o
 * consumidor (web/map.js) espera.
 *
 * Este é o teste que pegaria um `]}` no lugar de `]}}`: os testes baseados em
 * strstr() contam features e Procuram o começo, mas um objeto de feature
 * faltando uma chave ainda passa nos dois. Aqui o documento volta a ser
 * parseado, e cada feature é conferida campo a campo — inclusive o
 * aninhamento do Polygon contra o do MultiPolygon, que é onde erro de
 * colchete se esconde. */
static void confere_geojson(const char *s, size_t len, size_t n_esperado)
{
    json_val *raiz = json_parse(s, len);
    const json_val *feats;
    size_t i;

    T_ASSERT(raiz != NULL, "a saida tem de ser JSON valido");
    T_ASSERT(strcmp(json_str(json_obj_get(raiz, "type")), "FeatureCollection") == 0,
             "o type da raiz tem de ser FeatureCollection");

    feats = json_obj_get(raiz, "features");
    T_ASSERT(json_tipo_de(feats) == JSON_ARR, "features tem de ser um array");
    T_ASSERT(json_arr_len(feats) == n_esperado, "contagem de features diferente do esperado");

    for (i = 0; i < n_esperado; i++) {
        const json_val *f = json_arr_get(feats, i);
        const json_val *props = json_obj_get(f, "properties");
        const json_val *cod = json_obj_get(props, "codarea");
        const json_val *cent = json_obj_get(props, "centroide");
        const json_val *geom = json_obj_get(f, "geometry");
        const char *gt = json_str(json_obj_get(geom, "type"));
        const json_val *coords = json_obj_get(geom, "coordinates");
        size_t j, k;

        T_ASSERT(json_str(cod)[0] != '\0', "codarea nao pode ser vazio");
        T_ASSERT(json_arr_len(cent) == 2, "centroide tem 2 elementos");

        T_ASSERT(json_tipo_de(coords) == JSON_ARR, "coordinates tem de ser array");
        if (strcmp(gt, "Polygon") == 0) {
            /* coordinates = [ anel, ... ] */
            T_ASSERT(json_arr_len(coords) >= 1, "Polygon precisa de ao menos um anel");
            for (j = 0; j < json_arr_len(coords); j++) {
                T_ASSERT(json_arr_len(json_arr_get(coords, j)) >= 4, "anel com menos de 4 pontos");
            }
        } else {
            T_ASSERT(strcmp(gt, "MultiPolygon") == 0, "tipo de geometria desconhecido");
            /* coordinates = [ [ anel, ... ], ... ] */
            T_ASSERT(json_arr_len(coords) >= 1, "MultiPolygon precisa de ao menos um poligono");
            for (j = 0; j < json_arr_len(coords); j++) {
                const json_val *poly = json_arr_get(coords, j);
                T_ASSERT(json_arr_len(poly) >= 1, "poligono sem anel");
                for (k = 0; k < json_arr_len(poly); k++) {
                    T_ASSERT(json_arr_len(json_arr_get(poly, k)) >= 4,
                             "anel com menos de 4 pontos");
                }
            }
        }
    }
    json_free(raiz);
}

/* ------------------------------------------------------------------ */
/* seeding e limpeza                                                   */
/* ------------------------------------------------------------------ */

/* Malha municipal: 3 municípios, um deles Porto Velho (1100015), que é o que
 * o brief pede para provar que `codarea` sobreviveu ao parse e à reescrita. */
static void semeia_municipios(void)
{
    txt t;
    txt_init(&t);
    txt_abre_fc(&t);
    txt_feature(&t, "1100015", -63.9004, -8.7612, 40);     /* Porto Velho */
    txt_puts(&t, ",");
    txt_feature(&t, "3509502", -46.6333, -23.5505, 40);    /* São Paulo */
    txt_puts(&t, ",");
    txt_feature(&t, "3106200", -43.1729, -19.9167, 40);    /* Rio de Janeiro */
    txt_fecha_fc(&t);
    T_ASSERT(t.p != NULL, "fixture municipal deve ser alocada");
    T_ASSERT(cache_grava(K_MUN, t.p, t.n) == 0, "semeia cache municipal");
    txt_free(&t);
}

/* Malhas estaduais: 27 UFs em 5 macrorregiões, como a API exige. Cada região
 * é uma chamada separada e geo.c tem de mesclar as cinco. */
static void semeia_estados(void)
{
    /* 5 + 4 + 4 + 3 + 11 = 27 */
    static const int por_regiao[5] = { 5, 4, 4, 3, 11 };
    static const char *const chaves[5] = { K_EST1, K_EST2, K_EST3, K_EST4, K_EST5 };
    int nn = 11;
    int r, i;

    for (r = 0; r < 5; r++) {
        txt t;
        txt_init(&t);
        txt_abre_fc(&t);
        for (i = 0; i < por_regiao[r]; i++, nn++) {
            double lon = -60.0 + nn * 0.5;
            double lat = -10.0 + nn * 0.25;
            if (i) txt_puts(&t, ",");
            txt_puts(&t, "{\"type\":\"Feature\",\"properties\":{\"codarea\":\"");
            txt_printf(&t, "%d", nn);
            txt_puts(&t, "\",\"centroide\":[");
            txt_printf(&t, "%.15f,%.15f", lon, lat);
            txt_puts(&t, "]},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[");
            txt_anel(&t, lon, lat, 24);
            txt_puts(&t, "]}}");
        }
        txt_fecha_fc(&t);
        T_ASSERT(t.p != NULL, "fixture estadual deve ser alocada");
        T_ASSERT(cache_grava(chaves[r], t.p, t.n) == 0, "semeia cache estadual");
        txt_free(&t);
    }
}

/* Apaga o payload E o .ttl irmão. Deixar o .ttl para trás é o que fazia a
 * limpeza a seguir deixar lixo no diretório temporário: `cache_grava` grava os
 * dois (cache.h:50), e `cache_path` só devolve o caminho do payload, então um
 * remove() sozinho deixa metade do cache para trás. */
static void apaga_chave(const char *chave)
{
    char *p = cache_path(chave);
    if (p) {
        size_t n = strlen(p);
        char *ttl = malloc(n + 5);
        if (ttl) {
            memcpy(ttl, p, n);
            memcpy(ttl + n, ".ttl", 5);   /* o \0 vem junto nos 5 bytes */
            remove(ttl);
            free(ttl);
        }
        remove(p);
        free(p);
    }
}

static void limpa_tudo(void)
{
    remove(P_MUN);
    remove(P_EST);
    apaga_chave(K_MUN);
    apaga_chave(K_EST1);
    apaga_chave(K_EST2);
    apaga_chave(K_EST3);
    apaga_chave(K_EST4);
    apaga_chave(K_EST5);
}

/* ------------------------------------------------------------------ */
/* casos                                                               */
/* ------------------------------------------------------------------ */

/* O brief pede que geo_prepara(GEO_MUNICIPIOS) devolva 0, que a saída comece
 * com {"type":"FeatureCollection" e que contenha "codarea" com o valor
 * "1100015" — a prova de que o campo sobreviveu ao parse e à reescrita. */
static void testa_municipios(void)
{
    static const char prefixo[] = "{\"type\":\"FeatureCollection\"";
    size_t len = 0, k;
    char *s;

    remove(P_MUN);
    T_ASSERT(geo_prepara(GEO_MUNICIPIOS) == 0, "geo_prepara municipios deve retornar 0");
    s = geo_saida(GEO_MUNICIPIOS, &len);
    T_ASSERT(s != NULL, "geo_saida municipios nao deve devolver NULL");
    T_ASSERT(len > 0, "geo_saida municipios deve ter conteudo");

    k = strlen(prefixo);
    T_ASSERT(len >= k && memcmp(s, prefixo, k) == 0,
             "a saida deve comecar com {\\\"type\\\":\\\"FeatureCollection\\\"");

    /* `codarea` sobreviveu ao parse e à reescrita: é a chave de junção com o
     * modelo, então perder o tipo (virar número) já quebraria o coroplético. */
    T_ASSERT(strstr(s, "\"codarea\"") != NULL, "deve conter a chave codarea");
    T_ASSERT(strstr(s, "\"codarea\":\"1100015\"") != NULL,
             "codarea de Porto Velho (1100015) deve sobreviver como string");

    checa_centroides(s);

    /* As três features da fixture, cada uma com uma geometria. */
    T_ASSERT(conta_features(s) == 3, "a fixture municipal tem 3 features");

    /* A saida tem de ser GeoJSON parseavel, com o aninhamento certo. */
    confere_geojson(s, len, 3);

    /* Simplificação com efeito — e este limite não é arbitrário.
     *
     * A fixture tem 3 anéis de 40 pontos. Sem simplificação o documento teria
     * 132 colchetes (3 anéis × [1 do anel + 40 dos pontos] + 3 centroides +
     * 3 de "coordinates" + 1 de "features"). Com Douglas-Peucker a 0,05° cada
     * anel vira 3 vértices distintos mais o de fechamento, e o documento tem
     * 22. O limite de 60 fica longe dos dois: passa com folga pelo valor real
     * e estoura se a simplificação virar pass-through.
     *
     * O teste pegaria também o caso em que o anel colapsa para 2 pontos e uma
     * guarda "devolve o anel original" devolve o anel inteiro: o documento
     * voltaria a 132 e este limite falharia. */ 
    T_ASSERT(conta(s, "[") < 60, "a geometria deve ter sido simplificada");

    /* Nivel inválido: contrato defensivo, devolve NULL em vez de estado meio
     * inicializado. */
    T_ASSERT(geo_saida((geo_nivel)99, &len) == NULL, "nivel invalido devolve NULL");

    free(s);
}

/* A ordem estável é o requisito mais fácil de quebrar por acidente e o mais
 * difícil de ver: um qsort instável, um hash com ordem de iteração, ou uma
 * simplifier que decide olhando o índice do vizinho fazem a saída mudar entre
 * execuções sem erro nenhum. Por isso a comparação é byte a byte. */
static void testa_ordem_estavel(void)
{
    size_t la = 0, lb = 0;
    char *a, *b;

    /* O arquivo processado é apagado antes de CADA build: se geo_prepora
     * reusasse o resultado anterior, a comparação seria circular e não
     * provaria nada sobre o pipeline. */
    remove(P_MUN);
    T_ASSERT(geo_prepara(GEO_MUNICIPIOS) == 0, "geo_prepara municipios deve retornar 0");
    a = geo_saida(GEO_MUNICIPIOS, &la);
    T_ASSERT(a != NULL, "geo_saida municipios nao deve devolver NULL");
    T_ASSERT(la > 0, "geo_saida municipios deve ter conteudo");

    remove(P_MUN);
    T_ASSERT(geo_prepara(GEO_MUNICIPIOS) == 0, "segundo geo_prepara municipios deve retornar 0");
    b = geo_saida(GEO_MUNICIPIOS, &lb);
    T_ASSERT(b != NULL, "segundo geo_saida municipios nao deve devolver NULL");

    T_ASSERT(la == lb, "as duas saidas devem ter o mesmo tamanho");
    if (la != lb) {
        fprintf(stderr, "  (primeira: %zu bytes, segunda: %zu bytes)\n", la, lb);
    }
    T_ASSERT(memcmp(a, b, la) == 0, "as duas saidas devem ser identicas byte a byte");

    free(a);
    free(b);
}

/* Os estados vêm de 5 chamadas regionais que precisam ser mescladas, e o
 * resultado tem de ser 27 UFs. */
static void testa_estados(void)
{
    static const char prefixo[] = "{\"type\":\"FeatureCollection\"";
    size_t len = 0, k;
    char *s;

    remove(P_EST);
    T_ASSERT(geo_prepara(GEO_ESTADOS) == 0, "geo_prepara estados deve retornar 0");
    s = geo_saida(GEO_ESTADOS, &len);
    T_ASSERT(s != NULL, "geo_saida estados nao deve devolver NULL");
    T_ASSERT(len > 0, "geo_saida estados deve ter conteudo");

    k = strlen(prefixo);
    T_ASSERT(len >= k && memcmp(s, prefixo, k) == 0,
             "estados deve comecar com FeatureCollection");

    T_ASSERT(conta_features(s) == 27,
             "a mesclagem das 5 macorregioes deve dar 27 features");

    checa_centroides(s);
    confere_geojson(s, len, 27);

    free(s);
}

/* Sem dado processado, geo_saida devolve NULL — é o que permite à rota
 * responder 503 com "Execute: ./dashboard --atualizar" em vez de servir corpo
 * vazio com status 200. */
static void testa_sem_dado(void)
{
    remove(P_MUN);
    remove(P_EST);
    T_ASSERT(geo_saida(GEO_MUNICIPIOS, NULL) == NULL,
             "sem arquivo processado, geo_saida devolve NULL");
    T_ASSERT(geo_saida(GEO_ESTADOS, NULL) == NULL,
             "sem arquivo processado, geo_saida devolve NULL");
}

/* geo_prepara com fonte crua ausente devolve != 0 em vez de inventar dado ou
 * travar: é o caminho de quem nunca rodou --atualizar.
 *
 * O teste roda dentro de um diretório temporário, com `chdir`, porque tanto
 * data/cache/ quanto fontes.conf são caminhos RELATIVOS. Era este o teste que
 * dependia da máquina: ele apagava o cache e esperava que o download falhasse,
 * o que só acontece sem rede — numa máquina com rede ele baixava 5,78 MB das
 * malhas e falhava a asserção. Dentro do temporário não há cache e não há .conf,
 * então a falha é a mesma em qualquer
 * máquina, com rede ou sem ela. */
static void testa_fonte_ausente(void)
{
    size_t len = 0;
    char antes[4096];
    char tmp[] = "/tmp/teste-geo-XXXXXX";
    char *dir;

    limpa_tudo();

    if (!getcwd(antes, sizeof antes)) antes[0] = '\0';
    dir = mkdtemp(tmp);
    T_ASSERT(dir != NULL, "cria diretório temporário");
    T_ASSERT(chdir(dir) == 0, "entra no diretório temporário");

    T_ASSERT(geo_prepara(GEO_MUNICIPIOS) != 0,
             "sem fonte no cache e sem .conf, geo_prepara deve falhar");
    T_ASSERT(geo_saida(GEO_MUNICIPIOS, &len) == NULL,
             "prepare falho não deve deixar saída pela metade");
    T_ASSERT(geo_saida(GEO_ESTADOS, &len) == NULL,
             "e nem de outro nível");

    T_ASSERT(chdir(antes[0] ? antes : "..") == 0, "volta do temporário");
    remove(dir);
}

/* geo_codigo_uf: o mapa aceita a sigla ou o código, em qualquer caixa. */
static void testa_codigo_uf(void)
{
    T_ASSERT(strcmp(geo_codigo_uf("RO"), "11") == 0, "RO -> 11");
    T_ASSERT(strcmp(geo_codigo_uf("ro"), "11") == 0, "ro -> 11 (minuscula)");
    T_ASSERT(strcmp(geo_codigo_uf("sp"), "35") == 0, "sp -> 35 (minuscula)");
    T_ASSERT(strcmp(geo_codigo_uf("DF"), "53") == 0, "DF -> 53");
    T_ASSERT(strcmp(geo_codigo_uf("11"), "11") == 0, "11 -> 11 (codigo)");
    T_ASSERT(geo_codigo_uf("XX") == NULL, "XX nao e uma UF");
    T_ASSERT(geo_codigo_uf("99") == NULL, "99 nao e um codigo de UF");
    T_ASSERT(geo_codigo_uf("") == NULL, "string vazia nao e uma UF");
    T_ASSERT(geo_codigo_uf(NULL) == NULL, "NULL nao e uma UF");
}

/* O filtro por UF tem de devolver GeoJSON válido e só as features da UF. */
static void testa_filtro_uf(void)
{
    size_t len = 0, len_completa = 0;
    char *s, *todo;

    /* GEO_MUNICIPIOS: a fixture tem Porto Velho (RO=11), São Paulo (SP=35) e
     * Rio de Janeiro (RJ=33). Filtrar por SP tem de trazer só São Paulo. */
    todo = geo_saida(GEO_MUNICIPIOS, &len_completa);
    T_ASSERT(todo != NULL, "precisa da colecao completa");
    T_ASSERT(conta_features(todo) == 3, "a colecao completa tem 3 features");

    s = geo_saida_filtrada(GEO_MUNICIPIOS, "35", &len);
    T_ASSERT(s != NULL, "o filtro por UF nao devolve NULL");
    T_ASSERT(conta_features(s) == 1, "SP tem de trazer 1 feature");
    T_ASSERT(strstr(s, "\"3509502\"") != NULL, "o filtro por SP traz Sao Paulo");
    T_ASSERT(strstr(s, "1100015") == NULL, "o filtro por SP nao traz Porto Velho");
    confere_geojson(s, len, 1);
    free(s);

    s = geo_saida_filtrada(GEO_MUNICIPIOS, "11", &len);
    T_ASSERT(s != NULL, "filtro por RO nao devolve NULL");
    T_ASSERT(conta_features(s) == 1, "RO tem de trazer 1 feature");
    T_ASSERT(strstr(s, "\"1100015\"") != NULL, "o filtro por RO traz Porto Velho");
    free(s);

    s = geo_saida_filtrada(GEO_MUNICIPIOS, "99", &len);
    T_ASSERT(s != NULL, "UF sem municipio nao devolve NULL");
    T_ASSERT(conta_features(s) == 0, "UF sem municipio devolve colecao vazia");
    confere_geojson(s, len, 0);
    free(s);

    /* Sem filtro, é a coleção inteira. */
    s = geo_saida_filtrada(GEO_MUNICIPIOS, NULL, &len);
    T_ASSERT(s != NULL, "filtro NULL devolve a colecao");
    T_ASSERT(len == len_completa, "filtro NULL tem o mesmo tamanho");
    T_ASSERT(conta_features(s) == 3, "filtro NULL traz as 3 features");
    free(s);
    free(todo);

    /* GEO_ESTADOS: a fixture tem os códigos 11..37, um por UF. */
    todo = geo_saida(GEO_ESTADOS, &len_completa);
    T_ASSERT(todo != NULL, "precisa da colecao de estados");
    s = geo_saida_filtrada(GEO_ESTADOS, "11", &len);
    T_ASSERT(s != NULL, "filtro estadual nao devolve NULL");
    T_ASSERT(conta_features(s) == 1, "o codigo 11 traz 1 estado");
    T_ASSERT(strstr(s, "\"codarea\":\"11\"") != NULL, "o filtro traz o codigo 11");
    confere_geojson(s, len, 1);
    free(s);
    free(todo);
}

/* As rotas de geometria: 503 sem dado processado, 200 com dado, e a UF
 * inválida rejeitada com 400. É o contrato que o front usa para decidir
 * entre "sem malha ainda" e "filtro ruim". */
static void testa_rotas(void)
{
    http_request req;
    http_response res;
    char *saida;
    size_t len = 0;

    memset(&req, 0, sizeof req);
    memset(&res, 0, sizeof res);

    /* Sem dado processado: 503, não 200 com corpo vazio. */
    remove(P_MUN);
    remove(P_EST);
    req.path = (char *)"api/geo/municipios";
    req.query = (char *)"";
    T_ASSERT(api_roteia(&req, &res) == 503, "api/geo/municipios sem malha -> 503");
    T_ASSERT(res.body && strstr(res.body, "--atualizar") != NULL,
             "o 503 diz o comando de atualizacao");
    libera_body(&res);

    req.path = (char *)"api/geo/estados";
    T_ASSERT(api_roteia(&req, &res) == 503, "api/geo/estados sem malha -> 503");
    libera_body(&res);

    /* Com dado processado: 200, gzip ligado, GeoJSON no corpo. */
    T_ASSERT(geo_prepara(GEO_MUNICIPIOS) == 0, "prepara para o teste de rota");
    saida = geo_saida(GEO_MUNICIPIOS, &len);
    T_ASSERT(saida != NULL, "a colecao existe");
    free(saida);

    req.path = (char *)"api/geo/municipios";
    T_ASSERT(api_roteia(&req, &res) == 200, "api/geo/municipios com malha -> 200");
    T_ASSERT(res.body != NULL, "o 200 tem corpo");
    T_ASSERT(strncmp(res.body, "{\"type\":\"FeatureCollection\"", 27) == 0,
             "o corpo e a FeatureCollection");
    T_ASSERT(res.length > 0, "o 200 tem comprimento");
    T_ASSERT(res.gzip == 1, "geometria vai comprimida");
    libera_body(&res);

    /* api/mapa/uf é a rota do mapa: mesma coisa, filtrada. */
    req.path = (char *)"api/mapa/uf";
    req.query = (char *)"uf=sp&nivel=municipio";
    T_ASSERT(api_roteia(&req, &res) == 200, "api/mapa/uf com SP -> 200");
    T_ASSERT(res.body && strstr(res.body, "\"3509502\"") != NULL,
             "api/mapa/uf?uf=sp traz Sao Paulo");
    libera_body(&res);

    req.query = (char *)"uf=ro&nivel=municipio";
    T_ASSERT(api_roteia(&req, &res) == 200, "api/mapa/uf com RO -> 200");
    T_ASSERT(res.body && strstr(res.body, "\"1100015\"") != NULL,
             "api/mapa/uf?uf=ro traz Porto Velho");
    libera_body(&res);

    req.query = (char *)"uf=zz";
    T_ASSERT(api_roteia(&req, &res) == 400, "api/mapa/uf com UF invalida -> 400");
    libera_body(&res);

    /* Caminho desconhecido é 404, não "qualquer coisa serve os estados".
     * A versão anterior escolhia o nível com strstr(path,"municipios"), então
     * api/geo/naoexiste respondia 200 com a coleção de estados — e o teste de
     * 200 adiante não pegaria, porque continua sendo um 200 válido. */
    req.path = (char *)"api/geo/naoexiste";
    req.query = (char *)"";
    T_ASSERT(api_roteia(&req, &res) == 404, "api/geo/naoexiste -> 404");
    T_ASSERT(res.body && strstr(res.body, "FeatureCollection") == NULL,
             "a resposta de 404 nao pode ser uma colecao");
    libera_body(&res);

    req.path = (char *)"api/mapa/qualquer-coisa";
    T_ASSERT(api_roteia(&req, &res) == 404, "api/mapa/qualquer-coisa -> 404");
    libera_body(&res);

    /* E os dois caminhos válidos continuam servindo o nível certo. */
    T_ASSERT(geo_prepara(GEO_ESTADOS) == 0, "prepara estados para o teste de rota");
    req.path = (char *)"api/geo/estados";
    T_ASSERT(api_roteia(&req, &res) == 200, "api/geo/estados -> 200");
    T_ASSERT(res.body && strstr(res.body, "\"codarea\":\"11\"") != NULL,
             "api/geo/estados traz a colecao de estados");
    libera_body(&res);
    req.path = (char *)"api/geo/municipios";
    T_ASSERT(api_roteia(&req, &res) == 200, "api/geo/municipios -> 200");
    libera_body(&res);

    remove(P_MUN);
    remove(P_EST);
}

void test_geo(void)
{
    /* `make test` roda a partir da raiz do repositório, e tanto DIR_CACHE
     * quanto DIR_PROCESSED são caminhos RELATIVOS (cache.c, geo.c:58). Sem o
     * chdir abaixo, semeia_municipios()/semeia_estados() escreviam POR CIMA do
     * cache real do IBGE e — este é o dano — limpa_tudo() APAGAVA a malha de
     * verdade: os 5,78 MB de data/cache/ibge_malhas_municipios, os cinco
     * estados, e os data/processed/geo_*.json já processados.
     *
     * Foi assim que a suíte destruiu o dado que o front precisa: depois de um
     * `make test` o /api/geo/municipios respondia 503 "Execute: ./dashboard
     * --atualizar", e recuperar exigia as malhas da rede de novo. Os testes
     * nunca precisaram do dado real — eles semeiam fixtures sintéticos — então
     * a remoção não comprava nada: era só colateral. O cabeçalho deste arquivo
     * já dizia "roda SEM DADO REAL"; agora o código cumpre.
     *
     * O padrão é o mesmo de testa_fonte_ausente() acima, e o da C2 em
     * tests/test_web.sh: testes que tocam o disco rodam num diretório próprio. */
    char anterior[4096];
    char base[] = "/tmp/opencode/geo_XXXXXX";
    char *dir;

    if (!getcwd(anterior, sizeof anterior)) anterior[0] = '\0';
    dir = mkdtemp(base);
    T_ASSERT(dir != NULL, "cria diretório temporário para o grupo geo");
    T_ASSERT(chdir(dir) == 0, "entra no diretório temporário");

    printf("testa_fonte_ausente\n");
    testa_fonte_ausente();

    printf("testa_sem_dado\n");
    testa_sem_dado();

    printf("testa_codigo_uf\n");
    testa_codigo_uf();

    semeia_municipios();
    semeia_estados();

    printf("testa_municipios\n");
    testa_municipios();

    printf("testa_ordem_estavel\n");
    testa_ordem_estavel();

    printf("testa_estados\n");
    testa_estados();

    printf("testa_filtro_uf\n");
    testa_filtro_uf();

    printf("testa_rotas\n");
    testa_rotas();

    limpa_tudo();

    /* Ainda DENTRO do temporário: rmdir só remove diretório vazio, e é isso
     * que faz a limpeza ser uma asserção e não uma promessa — se sobrar
     * qualquer arquivo, o grupo acusa em vez de deixar lixo. E tem de ser aqui:
     * depois do chdir de volta, "data/cache" é o diretório de verdade. */
    T_ASSERT(rmdir("data/cache") == 0, "data/cache do temporário ficou vazio");
    T_ASSERT(rmdir("data/processed") == 0, "data/processed do temporário ficou vazio");
    T_ASSERT(rmdir("data") == 0, "data/ do temporário ficou vazio");

    T_ASSERT(chdir(anterior[0] ? anterior : "..") == 0,
             "volta para o diretório original");
    T_ASSERT(rmdir(dir) == 0, "o temporário do grupo geo ficou vazio");
}
/* src/geo.c -- malhas do IBGE: une, simplifica e cacheia (ver geo.h).
 *
 * O caminho é: ler cada FeatureCollection do cache de download, extrair
 * codarea + centroide + geometria, simplificar cada anel com
 * Douglas-Peucker, ordenar as features por codarea e reescrever o
 * FeatureCollection inteiro em coordenadas inteiras de graus×1000.
 *
 * Três decisões que explicam o resto do arquivo:
 *
 * 1. POR QUE UM ACÚMULO EM VEZ DE EMITIR DIRETO. geo_saida() tem de devolver
 *    o mesmo buffer byte a byte em chamadas diferentes, e a ordem das features
 *    só fica definida depois que as cinco coleções regionais foram lidas (elas
 *    vêm por região, não por código). Então as features são acumuladas,
 *    ordenadas por codarea, e só então escritas.
 *
 * 2. POR QUE ISSO NÃO É SÓ UM CACHE. O buffer devolvido por geo_saida()
 *    sobrevive a um json_free() e a um free() das entradas: as únicas cópias
 *    que existem são o arquivo em disco e este malloc. Um /api/geo/municipios
 *    por conexão não paga parse de 5,78 MB por requisição.
 *
 * 3. POR QUE data/processed/ E NÃO O CACHE DE DOWNLOAD. O arquivo
 *    processado é DERIVADO, não é byte que veio da rede: ele muda quando a
 *    simplificação muda, e a chave de cache tem TTL, o que faria um derivado
 *    expirar junto com o bruto. Por isso ele é gravado direto no diretório
 *    dele, com a mesma troca por rename.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "geo.h"
#include "cache.h"
#include "fontes.h"
#include "http_client.h"
#include "json.h"

/* ------------------------------------------------------------------ */
/* chaves das fontes e do resultado                                    */
/* ------------------------------------------------------------------ */

/* O nome da chave de cache é o id da fonte em fontes.conf: o módulo de cache
 * transforma o que sobrar em nome de arquivo, e o id é o mesmo identificador
 * que aparece no .conf, então o vínculo fica legível de um lado e do outro. */
static const char *const CHAVE_MUN[] = { "ibge_malhas_municipios" };

static const char *const CHAVE_EST[5] = {
    "ibge_malhas_estados_1", "ibge_malhas_estados_2", "ibge_malhas_estados_3",
    "ibge_malhas_estados_4", "ibge_malhas_estados_5"
};

#define DIR_PROCESSED "data/processed"
#define PATH_MUN DIR_PROCESSED "/geo_municipios.json"
#define PATH_EST DIR_PROCESSED "/geo_estados.json"

/* ------------------------------------------------------------------ */
/* geometria                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    long long *x, *y;               /* graus×1000, já arredondado */
    size_t n, cap;
    size_t poligono;                /* a qual polígono do MultiPolygon pertence */
} anel;

typedef struct {
    char *codarea;                  /* como veio do IBGE: string */
    long long cx, cy;               /* centroide, graus×1000 */
    char tipo;                      /* 'P' Polygon, 'M' MultiPolygon */
    size_t n_poligonos;             /* só para 'M' */
    anel *aneis;
    size_t n_aneis, cap_aneis;
    size_t ordem;                   /* índice de chegada: desempata o qsort */
} feature;

static void anel_libera(anel *a)
{
    free(a->x);
    free(a->y);
    a->x = NULL;
    a->y = NULL;
    a->n = a->cap = 0;
}

static void feature_libera(feature *f)
{
    size_t i;
    free(f->codarea);
    for (i = 0; i < f->n_aneis; i++) anel_libera(&f->aneis[i]);
    free(f->aneis);
    f->codarea = NULL;
    f->aneis = NULL;
    f->n_aneis = f->cap_aneis = 0;
}

static void features_libera(feature *fs, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) feature_libera(&fs[i]);
    free(fs);
}

/* Acrescenta um ponto, dobrando a capacidade. 0 ou -1. */
static int anel_add(anel *a, long long x, long long y)
{
    if (a->n == a->cap) {
        size_t nova = a->cap ? a->cap * 2 : 32;
        long long *nx = realloc(a->x, nova * sizeof *nx);
        long long *ny;
        if (!nx) return -1;
        a->x = nx;
        ny = realloc(a->y, nova * sizeof *ny);
        if (!ny) return -1;
        a->y = ny;
        a->cap = nova;
    }
    a->x[a->n] = x;
    a->y[a->n] = y;
    a->n++;
    return 0;
}

/* Copia um anel para a feature e transfere a posse dos buffers (o anel
 * recebido não deve ser liberado por quem chama depois disso). 0 ou -1. */
static int feature_add_anel(feature *f, const anel *a, size_t poligono)
{
    anel *na;
    if (f->n_aneis == f->cap_aneis) {
        size_t nova = f->cap_aneis ? f->cap_aneis * 2 : 4;
        na = realloc(f->aneis, nova * sizeof *na);
        if (!na) return -1;
        f->aneis = na;
        f->cap_aneis = nova;
    }
    na = &f->aneis[f->n_aneis];
    *na = *a;
    na->poligono = poligono;
    f->n_aneis++;
    if (f->n_poligonos < poligono + 1) f->n_poligonos = poligono + 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* texto de saída                                                      */
/* ------------------------------------------------------------------ */

/* Acumulador de saída com folga. Escrever caractere a caractere num buffer de
 * 1,2 MB já é o caminho comum; o que seria caro é um snprintf por vértice
 * (252 mil chamadas), que é o que este acumulador evita. */
typedef struct {
    char *p;
    size_t n, cap;
} out;

static void out_free(out *o)
{
    free(o->p);
    o->p = NULL;
    o->n = o->cap = 0;
}

static int out_reserva(out *o, size_t extra)
{
    size_t nova;
    char *maior;
    if (o->n + extra + 1 <= o->cap) return 0;
    nova = o->cap ? o->cap : 4096;
    while (o->n + extra + 1 > nova) nova *= 2;
    maior = realloc(o->p, nova);
    if (!maior) return -1;
    o->p = maior;
    o->cap = nova;
    return 0;
}

static int out_puts(out *o, const char *s)
{
    size_t k = strlen(s);
    if (out_reserva(o, k) != 0) return -1;
    memcpy(o->p + o->n, s, k);
    o->n += k;
    o->p[o->n] = '\0';
    return 0;
}

static int out_printf(out *o, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int k;
    va_start(ap, fmt);
    k = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t)k >= sizeof buf) return -1;
    if (out_reserva(o, (size_t)k) != 0) return -1;
    memcpy(o->p + o->n, buf, (size_t)k);
    o->n += (size_t)k;
    o->p[o->n] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* ordenação                                                          */
/* ------------------------------------------------------------------ */

/* qsort não é estável. Dois codarea iguais não deveriam existir, mas se
 * existirem a ordem entre eles viraria dependente da implementação — e a
 * saída precisa ser reproduzível byte a byte. O índice de chegada desempata e
 * torna a ordem total.
 *
 * qsort recebe um array de structs feature, não de ponteiros: o que chega
 * aqui são as structs em si. */
static int cmp_feature(const void *pa, const void *pb)
{
    const feature *a = pa;
    const feature *b = pb;
    int c = strcmp(a->codarea, b->codarea);
    if (c != 0) return c;
    if (a->ordem < b->ordem) return -1;
    if (a->ordem > b->ordem) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* simplificação                                                       */
/* ------------------------------------------------------------------ */

/* Distância² do ponto ao segmento AB (não à reta: o ponto mais próximo do
 * segmento, senão a ponta do anel esticaria). */
static double dist2_ao_segmento(long long px, long long py,
                                long long ax, long long ay,
                                long long bx, long long by)
{
    /* Coordenadas em graus×1000 (inteiros), então tudo cabe em long long com
     * folga: |x| < 1e5 => diferença < 2e5 => quadrado < 4e10. */
    long long dx = bx - ax, dy = by - ay;
    long long px1 = px - ax, py1 = py - ay;
    long long len2 = dx * dx + dy * dy;
    long long t;

    if (len2 == 0) return (double)(px1 * px1 + py1 * py1);
    t = px1 * dx + py1 * dy;
    if (t <= 0) return (double)(px1 * px1 + py1 * py1);
    if (t >= len2) {
        long long qx = px - bx, qy = py - by;
        return (double)(qx * qx + qy * qy);
    }
    {
        /* cross² / len2: o quadrado da área do paralelogramo sobre |AB|² dá a
         * distância² perpendicular, sem divisão inteira. */
        long long cross = px1 * dy - py1 * dx;
        return (double)cross * (double)cross / (double)len2;
    }
}

/* Douglas-Peucker iterativo, com pilha explícita. Um anel do IBGE tem até 888
 * pontos; a recursão caberia na pilha, mas a versão iterativa não depende do
 * tamanho da entrada e não precisa de limite de profundidade.
 *
 * O ANEL É FECHADO, E ISSO MUDA TUDO. O ponto n-1 é igual ao ponto 0, então
 * rodar o DP de 0 a n-1 faz o primeiro segmento ter comprimento zero: todo
 * ponto é medido contra p[0], a distância vira "quanto longe do início" e o
 * critério perde o sentido. O resultado é que anéis pequenos (todos os pontos
 * perto um do outro, o caso comum em município pequeno) colapsam para 2 pontos
 * e a guarda de tamanho mínimo abaixo devolvia o anel INTEIRO — a simplificação
 * era mascarada, não aplicada.
 *
 * A correção é tratar o anel como polilinha aberta de 0 a n-2, que tem
 * primeiro e último pontos distintos e segmenta de verdade, e fechar o
 * resultado reemitindo o primeiro ponto no fim. */
static void simplifica(const anel *in, anel *resultado, double tol_graus)
{
    double tol2 = tol_graus * tol_graus * 1000.0 * 1000.0;
    size_t *pilha = NULL;
    size_t npilha = 0, cap_pilha = 0;
    unsigned char *marcas = NULL;
    size_t ultimo, i, n_marcados = 0;

    resultado->n = 0;
    if (in->n < 4) return;

    marcas = malloc(in->n);
    if (!marcas) return;
    memset(marcas, 0, in->n);

    /* Polilinha aberta: o último índice é a cópia do primeiro, e é ele que
     * vai ser recriado no fechamento. */
    ultimo = in->n - 2;
    marcas[0] = 1;
    marcas[ultimo] = 1;
    n_marcados = 2;

    cap_pilha = 64;
    pilha = malloc(cap_pilha * 2 * sizeof *pilha);
    if (!pilha) { free(marcas); return; }
    pilha[0] = 0;
    pilha[1] = ultimo;
    npilha = 1;

    while (npilha > 0) {
        size_t ini, fim, pior = 0;
        double pior_d = -1.0;

        npilha--;
        ini = pilha[npilha * 2];
        fim = pilha[npilha * 2 + 1];
        if (fim <= ini + 1) continue;

        for (i = ini + 1; i < fim; i++) {
            double d = dist2_ao_segmento(in->x[i], in->y[i],
                                         in->x[ini], in->y[ini],
                                         in->x[fim], in->y[fim]);
            if (d > pior_d) { pior_d = d; pior = i; }
        }
        /* A comparação com NaN é sempre falsa, então `!(pior_d > tol2)` trata
         * NaN como "não é para descartar" e mantém o ponto — o comportamento
         * seguro para geometria. */
        if (!(pior_d > tol2)) continue;
        if (pior <= ini || pior >= fim) continue;

        marcas[pior] = 1;
        n_marcados++;
        if (npilha + 2 > cap_pilha) {
            size_t nova = cap_pilha * 2;
            size_t *np = realloc(pilha, nova * 2 * sizeof *np);
            if (!np) break;
            pilha = np;
            cap_pilha = nova;
        }
        pilha[npilha * 2] = ini;
        pilha[npilha * 2 + 1] = pior;
        npilha++;
        pilha[npilha * 2] = pior;
        pilha[npilha * 2 + 1] = fim;
        npilha++;
    }

    /* Um anel GeoJSON válido tem no mínimo 4 posições: 3 vértices distintos e
     * o primeiro repetido no fim. Se o DP comeu até 2, promotes os pontos que
     * mais se afastam das pontas até ter vértice suficiente — devolve o anel
     * original inteiro, que é o que mascarava a simplificação e o que deixava
     * um anel pequeno continuar grande. */
    while (n_marcados < 3 && ultimo >= 2) {
        size_t melhor = 0;
        double melhor_d = -1.0;
        for (i = 1; i <= ultimo; i++) {
            double d;
            if (marcas[i]) continue;
            /* mede contra a ponta mais próxima já preservada */
            d = dist2_ao_segmento(in->x[i], in->y[i], in->x[0], in->y[0],
                                  in->x[ultimo], in->y[ultimo]);
            if (d > melhor_d) { melhor_d = d; melhor = i; }
        }
        if (melhor == 0) break;
        marcas[melhor] = 1;
        n_marcados++;
    }

    for (i = 0; i <= ultimo; i++) {
        if (!marcas[i]) continue;
        if (anel_add(resultado, in->x[i], in->y[i]) != 0) break;
    }
    /* Fecha o anel repetindo o primeiro vértice. */
    if (resultado->n >= 3 && anel_add(resultado, in->x[0], in->y[0]) != 0) {
        anel_libera(resultado);
    }

    free(pilha);
    free(marcas);
}

/* ------------------------------------------------------------------ */
/* leitura de uma FeatureCollection                                    */
/* ------------------------------------------------------------------ */

/* Arredonda graus para graus×1000 em aritmética inteira.
 *
 * Deliberadamente sem llround()/round(): quem chama está em -180..180, então
 * *1000 não passa de 180000 e o double é exato nessa faixa. Converter com
 * (long long)(v * 1000.0 + (v < 0 ? -0.5 : 0.5)) evita puxar -lm para o
 * projeto inteiro por causa de uma função de arredondamento. */
static long long arredonda(double v)
{
    double x = v * 1000.0;
    if (x < 0) x -= 0.5;
    else x += 0.5;
    return (long long)x;
}

/* Lê um par [lon, lat] e escreve o vértice. 0 ou -1. */
static int le_ponto(const json_val *pt, anel *a)
{
    double lon, lat;
    if (json_tipo_de(pt) != JSON_ARR || json_arr_len(pt) < 2) return -1;
    lon = json_num(json_arr_get(pt, 0));
    lat = json_num(json_arr_get(pt, 1));
    return anel_add(a, arredonda(lon), arredonda(lat));
}

/* Lê um anel, simplifica e devolve. 0 ou -1. */
static int processa_anel(const json_val *anel_json, feature *f, size_t poligono)
{
    anel cru = {NULL, NULL, 0, 0, 0};
    anel simp = {NULL, NULL, 0, 0, 0};
    size_t i, n = json_arr_len(anel_json);

    for (i = 0; i < n; i++) {
        if (le_ponto(json_arr_get(anel_json, i), &cru) != 0) {
            anel_libera(&cru);
            return -1;
        }
    }
    if (cru.n < 4) {
        anel_libera(&cru);
        return -1;                  /* anel degenerado não é geometria */
    }
    simplifica(&cru, &simp, TOLERANCIA_SIMPLIFICACAO);
    anel_libera(&cru);
    if (simp.n < 4) {
        anel_libera(&simp);
        return -1;
    }
    /* A feature fica DONA do anel: feature_add_anel copia a struct e
     * transfere a posse dos buffers, então não se libera `simp` aqui. */
    if (feature_add_anel(f, &simp, poligono) != 0) {
        anel_libera(&simp);
        return -1;
    }
    return 0;
}

/* Lê a geometria de uma feature, preservando o aninhamento do MultiPolygon.
 * 0 ou -1. */
static int processa_geometria(const json_val *geom, feature *f)
{
    const char *tipo;
    const json_val *coords;
    size_t i, j, np;

    if (json_tipo_de(geom) != JSON_OBJ) return -1;
    tipo = json_str(json_obj_get(geom, "type"));
    coords = json_obj_get(geom, "coordinates");
    if (!coords || json_tipo_de(coords) != JSON_ARR) return -1;
    np = json_arr_len(coords);

    if (strcmp(tipo, "Polygon") == 0) {
        /* coordinates: [ anel, ... ] */
        f->tipo = 'P';
        for (i = 0; i < np; i++) {
            processa_anel(json_arr_get(coords, i), f, 0);
        }
        return f->n_aneis > 0 ? 0 : -1;
    }
    if (strcmp(tipo, "MultiPolygon") == 0) {
        /* coordinates: [ [ anel, ... ], ... ] */
        f->tipo = 'M';
        for (i = 0; i < np; i++) {
            const json_val *poly = json_arr_get(coords, i);
            size_t n_aneis = json_arr_len(poly);
            for (j = 0; j < n_aneis; j++) {
                processa_anel(json_arr_get(poly, j), f, i);
            }
        }
        return f->n_aneis > 0 ? 0 : -1;
    }
    /* Um tipo que a API do IBGE não documenta (GeometryCollection, por
     * exemplo) é descartado em vez de virar geometria inventada. */
    return -1;
}

/* Lê uma feature e a acrescenta ao vetor. 0 ou -1. */
static int le_feature(const json_val *feat, feature **v, size_t *n, size_t *cap)
{
    const json_val *props = json_obj_get(feat, "properties");
    const json_val *cod = json_obj_get(props, "codarea");
    const json_val *cent = json_obj_get(props, "centroide");
    const json_val *geom = json_obj_get(feat, "geometry");
    const char *codarea = json_str(cod);
    feature f;

    memset(&f, 0, sizeof f);
    /* Sem codarea não há chave de junção com o modelo, então a feature não
     * serve para nada no coroplético: descarta em vez de emitir meia. */
    if (!codarea || !*codarea) return -1;
    f.codarea = strdup(codarea);
    if (!f.codarea) return -1;
    f.ordem = *n;
    f.cx = arredonda(json_num(json_arr_get(cent, 0)));
    f.cy = arredonda(json_num(json_arr_get(cent, 1)));

    if (processa_geometria(geom, &f) != 0) {
        feature_libera(&f);
        return -1;
    }
    if (*n == *cap) {
        size_t nova = *cap ? *cap * 2 : 256;
        feature *nf = realloc(*v, nova * sizeof *nf);
        if (!nf) { feature_libera(&f); return -1; }
        *v = nf;
        *cap = nova;
    }
    (*v)[*n] = f;
    (*n)++;
    return 0;
}

/* Lê uma FeatureCollection inteira. 0 ou -1. */
static int le_colecao(const char *buf, size_t len,
                      feature **v, size_t *n, size_t *cap)
{
    json_val *raiz = json_parse(buf, len);
    const json_val *feats;
    size_t i, nf;

    if (!raiz) return -1;
    feats = json_obj_get(raiz, "features");
    nf = json_arr_len(feats);
    for (i = 0; i < nf; i++) {
        le_feature(json_arr_get(feats, i), v, n, cap);
    }
    json_free(raiz);
    return 0;
}

/* ------------------------------------------------------------------ */
/* escrita                                                             */
/* ------------------------------------------------------------------ */

static int escreve_anel(out *o, const anel *a)
{
    size_t j;
    out_puts(o, "[");
    for (j = 0; j < a->n; j++) {
        if (j) out_puts(o, ",");
        if (out_printf(o, "[%lld,%lld]", a->x[j], a->y[j]) != 0) return -1;
    }
    out_puts(o, "]");
    return 0;
}

static int escreve_feature(out *o, const feature *f)
{
    size_t i, j;

    if (out_printf(o, "{\"type\":\"Feature\",\"properties\":{\"codarea\":\"%s\","
                       "\"centroide\":[%lld,%lld]},\"geometry\":{\"type\":\"%s\","
                       "\"coordinates\":[",
                   f->codarea, f->cx, f->cy,
                   f->tipo == 'M' ? "MultiPolygon" : "Polygon") != 0) return -1;

    if (f->tipo == 'M') {
        /* MultiPolygon tem um nível de aninhamento a mais que Polygon:
         * coordinates é uma lista de polígonos, cada um uma lista de anéis. */
        for (i = 0; i < f->n_poligonos; i++) {
            int primeiro = 1;
            if (i) out_puts(o, ",");
            out_puts(o, "[");
            for (j = 0; j < f->n_aneis; j++) {
                if (f->aneis[j].poligono != i) continue;
                if (!primeiro) out_puts(o, ",");
                primeiro = 0;
                if (escreve_anel(o, &f->aneis[j]) != 0) return -1;
            }
            out_puts(o, "]");
        }
    } else {
        for (i = 0; i < f->n_aneis; i++) {
            if (i) out_puts(o, ",");
            if (escreve_anel(o, &f->aneis[i]) != 0) return -1;
        }
    }
    /* Fecha geometry e feature: dois colchetes de fecha e duas chaves. */
    out_puts(o, "]}}");
    return 0;
}

static char *monta_saida(feature *fs, size_t n, size_t *out_len)
{
    out o;
    size_t i;

    memset(&o, 0, sizeof o);
    out_puts(&o, "{\"type\":\"FeatureCollection\",\"features\":[");
    for (i = 0; i < n; i++) {
        if (i) out_puts(&o, ",");
        if (escreve_feature(&o, &fs[i]) != 0) {
            out_free(&o);
            return NULL;
        }
    }
    out_puts(&o, "]}");
    if (!o.p) return NULL;
    *out_len = o.n;
    return o.p;
}

/* ------------------------------------------------------------------ */
/* fonte: cache de download, ou download se faltar                    */
/* ------------------------------------------------------------------ */

/* Devolve o corpo cru de uma fonte, malloc'd, e o tamanho em *out_len. Se já
 * está no cache, lê de lá — é o caminho normal depois do primeiro
 * --atualizar e o caminho que a suíte de teste exercita sem rede. Se não
 * está, baixa pela URL da fonte e grava no cache. */
static char *obtem_fonte(const char *chave, const char *url, size_t *out_len)
{
    char *buf = NULL;
    size_t len = 0;

    *out_len = 0;
    if (cache_le(chave, &buf, &len) == 0) {
        if (len > 0) { *out_len = len; return buf; }
        free(buf);
        buf = NULL;
    }
    if (!url) return NULL;
    if (http_get(url, &buf, &len) != 0 || len == 0) {
        free(buf);
        return NULL;
    }
    if (cache_grava(chave, buf, len) != 0) {
        /* O download deu certo mas não deu para gravar. Ainda entregamos o
         * buffer: perder o cache é mais barato que perder o build inteiro. */
        fprintf(stderr, "aviso: nao gravei a fonte %s no cache\n", chave);
    }
    *out_len = len;
    return buf;
}

/* ------------------------------------------------------------------ */
/* gravação do resultado                                               */
/* ------------------------------------------------------------------ */

static void cria_dir(const char *path)
{
    char tmp[512];
    size_t n = strlen(path);
    if (n >= sizeof tmp) return;
    memcpy(tmp, path, n + 1);
    mkdir(tmp, 0755);
}

/* Grava com troca por rename. Um leitor concorrente — outro --atualizar, ou
 * um filho do fork atendendo /api/geo/... — nunca vê um arquivo pela metade,
 * que é o que faria o json_parse() do servidor devolver NULL em cima de
 * geometricamente bom dado. */
static int grava_processado(const char *path, const void *buf, size_t len)
{
    char tmp[512];
    FILE *f;
    size_t n;

    cria_dir(DIR_PROCESSED);
    n = (size_t)snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n >= sizeof tmp) return -1;
    f = fopen(tmp, "wb");
    if (!f) return -1;
    if (len > 0 && fwrite(buf, 1, len, f) != len) {
        fclose(f);
        remove(tmp);
        return -1;
    }
    if (fflush(f) != 0) { fclose(f); remove(tmp); return -1; }
    fclose(f);
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 0;
}

/* ------------------------------------------------------------------ */
/* filtro por UF                                                       */
/* ------------------------------------------------------------------ */

/* As 27 UFs, sigla -> código. A malha não traz a sigla (properties só tem
 * codarea e centroide), então a tabela é a fonte do mapeamento. */
static const struct { const char *sigla, *codigo; } UFS[] = {
    { "RO", "11" }, { "AC", "12" }, { "AM", "13" }, { "RR", "14" },
    { "PA", "15" }, { "AP", "16" }, { "TO", "17" }, { "MA", "21" },
    { "PI", "22" }, { "CE", "23" }, { "RN", "24" }, { "PB", "25" },
    { "PE", "26" }, { "AL", "27" }, { "SE", "28" }, { "BA", "29" },
    { "MG", "31" }, { "ES", "32" }, { "RJ", "33" }, { "SP", "35" },
    { "PR", "41" }, { "SC", "42" }, { "RS", "43" }, { "MS", "50" },
    { "MT", "51" }, { "GO", "52" }, { "DF", "53" }
};

/* Existe uma UF com esse código de dois dígitos? */
static int UFS_existe(const char *codigo)
{
    size_t i;
    for (i = 0; i < sizeof UFS / sizeof UFS[0]; i++) {
        if (UFS[i].codigo[0] == codigo[0] && UFS[i].codigo[1] == codigo[1])
            return 1;
    }
    return 0;
}

const char *geo_codigo_uf(const char *sigla)
{
    size_t i;
    if (!sigla || strlen(sigla) < 2) return NULL;

    /* Aceita o código numérico direto ("11"), que é a forma que a malha usa,
     * além da sigla ("RO", "ro", "rO"). */
    if (sigla[0] >= '0' && sigla[0] <= '9' && sigla[1] >= '0' && sigla[1] <= '9')
        return (strlen(sigla) == 2 && UFS_existe(sigla)) ? sigla : NULL;

    for (i = 0; i < sizeof UFS / sizeof UFS[0]; i++) {
        if (toupper((unsigned char)sigla[0]) == UFS[i].sigla[0] &&
            toupper((unsigned char)sigla[1]) == UFS[i].sigla[1])
            return UFS[i].codigo;
    }
    return NULL;
}

/* Localiza o valor de "codarea":"..." dentro de um trecho de feature e
 * escreve em `out` (com espaço para o '\0'). Devolve 0 se não achar. */
static int extrai_codarea(const char *ini, const char *fim, char *out, size_t out_max)
{
    const char *p = ini;
    const char *inicio;
    size_t k = 0;
    while ((p = strstr(p, "\"codarea\":\"")) != NULL && p < fim) {
        p += 11;
        inicio = p;
        while (p < fim && *p != '"') p++;
        if (p >= fim) return 0;
        k = (size_t)(p - inicio);
        if (k >= out_max) k = out_max - 1;
        memcpy(out, inicio, k);
        out[k] = '\0';
        return 1;
    }
    return 0;
}

/* O codarea da feature pertence à UF? Estados: o codarea é o próprio código da
 * UF. Municípios: o codarea começa com o código da UF. */
static int codarea_da_uf(const char *codarea, const char *uf_codigo, geo_nivel nivel)
{
    size_t k = strlen(uf_codigo);
    if (nivel == GEO_ESTADOS) return strncmp(codarea, uf_codigo, k) == 0;
    return strncmp(codarea, uf_codigo, k) == 0;
}

char *geo_saida_filtrada(geo_nivel nivel, const char *uf_codigo, size_t *out_len)
{
    static const char cabeca[] = "{\"type\":\"FeatureCollection\",\"features\":[";
    char *todo, *saida;
    size_t len = 0, n = 0;
    const char *p, *fim;

    if (out_len) *out_len = 0;
    if (nivel != GEO_ESTADOS && nivel != GEO_MUNICIPIOS) return NULL;
    if (uf_codigo && !*uf_codigo) return geo_saida(nivel, out_len);
    if (!uf_codigo) return geo_saida(nivel, out_len);

    todo = geo_saida(nivel, &len);
    if (!todo) return NULL;

    saida = malloc(len + 64);
    if (!saida) { free(todo); return NULL; }
    memcpy(saida, cabeca, sizeof cabeca - 1);
    n = sizeof cabeca - 1;

    /* Anda objeto a objeto contando chaves, respeitando strings: um '{'
     * dentro de um nome de município não fecha nada, e errar isso gera
     * GeoJSON truncado no meio de uma feature. */
    p = strstr(todo, "\"features\":[");
    if (!p) { free(todo); free(saida); return NULL; }
    p += 12;
    fim = todo + len;

    while (p < fim) {
        const char *obj_ini, *obj_fim;
        int profundidade = 0, dentro_string = 0, escapes = 0;
        char codarea[16];

        while (p < fim && *p != '{') p++;
        if (p >= fim) break;
        obj_ini = p;
        for (; p < fim; p++) {
            char c = *p;
            if (dentro_string) {
                if (escapes) escapes = 0;
                else if (c == '\\') escapes = 1;
                else if (c == '"') dentro_string = 0;
                continue;
            }
            if (c == '"') dentro_string = 1;
            else if (c == '{') profundidade++;
            else if (c == '}') {
                profundidade--;
                if (profundidade == 0) { p++; break; }
            }
        }
        obj_fim = p;
        if (extrai_codarea(obj_ini, obj_fim, codarea, sizeof codarea) &&
            codarea_da_uf(codarea, uf_codigo, nivel)) {
            size_t tam = (size_t)(obj_fim - obj_ini);
            if (n > sizeof cabeca - 1) saida[n++] = ',';
            if (n + tam + 4 > len + 64) { free(todo); free(saida); return NULL; }
            memcpy(saida + n, obj_ini, tam);
            n += tam;
        }
    }
    /* As features copiadas já vêm fechadas e balanceadas; falta só fechar o
     * array "features" e o objeto raiz. */
    saida[n++] = ']';
    saida[n++] = '}';
    saida[n] = '\0';
    free(todo);

    if (out_len) *out_len = n;
    return saida;
}

static const char *const *chaves_do_nivel(geo_nivel nivel, size_t *n)
{
    if (nivel == GEO_MUNICIPIOS) { *n = 1; return CHAVE_MUN; }
    if (nivel == GEO_ESTADOS)   { *n = 5; return CHAVE_EST; }
    return NULL;
}

static const char *path_do_nivel(geo_nivel nivel)
{
    return nivel == GEO_MUNICIPIOS ? PATH_MUN : PATH_EST;
}

int geo_prepara(geo_nivel nivel)
{
    const char *const *chaves;
    size_t nchaves = 0, i, presentes = 0;
    char **urls = NULL;
    char **buf = NULL;
    size_t *len = NULL;
    fonte_dados *fontes = NULL;
    size_t nfontes = 0;
    feature *acum = NULL;
    size_t n_acum = 0, cap_acum = 0;
    char *saida = NULL;
    size_t saida_len = 0;
    int rc = -1;

    chaves = chaves_do_nivel(nivel, &nchaves);
    if (!chaves) return -1;

    urls  = calloc(nchaves, sizeof *urls);
    buf   = calloc(nchaves, sizeof *buf);
    len   = calloc(nchaves, sizeof *len);
    if (!urls || !buf || !len) goto fim;

    /* Primeiro tenta tudo no cache. É o caso comum depois do primeiro
     * --atualizar, e é o que permite rodar sem rede. */
    for (i = 0; i < nchaves; i++) {
        buf[i] = obtem_fonte(chaves[i], NULL, &len[i]);
        if (buf[i]) presentes++;
    }

    /* Só o que faltou no cache pede rede, e só então vale a pena abrir o
     * .conf: um build 100% servido do cache funciona com fontes.conf
     * ausente, o que é o estado de um servidor que já atualizou. */
    if (presentes < nchaves) {
        if (fontes_ler("fontes.conf", &fontes, &nfontes) != 0) goto fim;
        for (i = 0; i < nchaves; i++) {
            fonte_dados *f = NULL;
            if (buf[i]) continue;
            if (fontes_garente(fontes, nfontes, &f, chaves[i]) == 0 && f)
                urls[i] = f->url_zip;     /* pertence ao bloco de fontes */
        }
    }
    for (i = 0; i < nchaves; i++) {
        if (buf[i]) continue;
        if (!urls[i]) goto fim;
        buf[i] = obtem_fonte(chaves[i], urls[i], &len[i]);
        if (!buf[i]) goto fim;
    }

    for (i = 0; i < nchaves; i++) {
        if (le_colecao(buf[i], len[i], &acum, &n_acum, &cap_acum) != 0) goto fim;
    }
    if (n_acum == 0) goto fim;

    qsort(acum, n_acum, sizeof *acum, cmp_feature);

    saida = monta_saida(acum, n_acum, &saida_len);
    if (!saida) goto fim;
    if (grava_processado(path_do_nivel(nivel), saida, saida_len) != 0) goto fim;

    rc = 0;

fim:
    free(saida);
    features_libera(acum, n_acum);
    for (i = 0; i < nchaves; i++) free(buf[i]);
    fontes_libera(fontes, nfontes);
    free(urls);
    free(buf);
    free(len);
    return rc;
}

char *geo_saida(geo_nivel nivel, size_t *out_len)
{
    FILE *f;
    char *buf;
    long sz;
    size_t lidos;

    if (out_len) *out_len = 0;
    if (nivel != GEO_ESTADOS && nivel != GEO_MUNICIPIOS) return NULL;

    f = fopen(path_do_nivel(nivel), "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f);
    if (sz <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    lidos = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (lidos != (size_t)sz) { free(buf); return NULL; }
    buf[sz] = '\0';
    if (out_len) *out_len = lidos;
    return buf;
}
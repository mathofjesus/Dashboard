#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/fontes.h"
#include "../src/cache.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

/* Conf mínimo, escrito num arquivo temporário: exercita o parser e o
 * downloads sem depender do fontes.conf do projeto nem da rede. */
static void escreve_conf(const char *caminho)
{
    FILE *f = fopen(caminho, "wb");
    T_ASSERT(f != NULL, "criou .conf temporário");
    fputs("# comentário\n"
          "linha sem igual\n"
          "\n"
          "[bruta]\n"
          "  url     =   https://exemplo.invalid/x.json  \n"
          "  dataset = algo.csv\n"
          "  ttl     = 90\n"
          "\n"
          "[so_prefixo]\n"
          "url = https://exemplo.invalid/base\n"
          "ttl = 45\n",
          f);
    fclose(f);
}

void test_fontes(void)
{
    fonte_dados *fontes = NULL;
    size_t n = 0;
    const fonte_dados *f = NULL;
    char *corpo = NULL;
    size_t len = 0;
    const char *conf = "/tmp/opencode/test_fontes.conf";
    int r;

    /* --- leitura do .conf do projeto --- */
    r = fontes_ler("fontes.conf", &fontes, &n);
    T_ASSERT(r == 0, "fontes_ler deve retornar 0");
    T_ASSERT(n >= 5, "deve haver ao menos 5 fontes");
    T_ASSERT(fontes != NULL, "fontes não deve ser NULL");

    f = fontes_obtem(fontes, n, "cnes");
    T_ASSERT(f != NULL, "cnes existe");
    T_ASSERT(f->url_zip && strstr(f->url_zip, "cnes_estabelecimentos") != NULL,
             "url do cnes");
    /* A url não pode vir com espaço à esquerda: http_client.c:url_aceita()
     * só aceita o que começa por "http", e um espaço faz o download inteiro
     * falhar em silêncio. Foi o bug do `if (s != s)` em trim(). */
    T_ASSERT(strncmp(f->url_zip, "https://", 8) == 0,
             "a url não pode começar com espaço");
    T_ASSERT(strchr(f->url_zip, ' ') == NULL,
             "a url não pode ter espaço dentro");

    /* dataset e ttl são lidos, não descartados. */
    T_ASSERT(f->dataset && strcmp(f->dataset, "cnes_estabelecimentos.csv") == 0,
             "dataset do cnes");
    T_ASSERT(f->ttl == 604800, "ttl do cnes");

    f = fontes_obtem(fontes, n, "leitos");
    T_ASSERT(f != NULL && f->ttl == 604800, "ttl dos leitos");
    f = fontes_obtem(fontes, n, "ibge_localidades");
    T_ASSERT(f != NULL && strstr(f->url_zip, "localidades/municipios") != NULL,
             "url das localidades");
    f = fontes_obtem(fontes, n, "ibge_sidra_municipios");
    T_ASSERT(f != NULL, "fonte do SIDRA municipal existe");
    T_ASSERT(f->ttl == 2592000, "ttl do SIDRA");

    /* Chave inexistente → NULL */
    T_ASSERT(fontes_obtem(fontes, n, "chave_inexistente") == NULL,
             "chave inexistente não é encontrada");
    T_ASSERT(fontes_obtem(fontes, n, NULL) == NULL, "id NULL não é encontrada");
    fontes_libera(fontes, n);
    fontes = NULL;
    n = 0;

    /* Arquivo inexistente → != 0 */
    T_ASSERT(fontes_ler("arquivo_inexistente.conf", &fontes, &n) != 0,
             "arquivo inexistente deve falhar");
    T_ASSERT(fontes == NULL && n == 0, "saída zerada no erro");

    /* --- parser: espaços em volta do "=" e linha sem "=" --- */
    escreve_conf(conf);
    r = fontes_ler(conf, &fontes, &n);
    T_ASSERT(r == 0, "leu o .conf temporário");
    T_ASSERT(n == 2, "duas seções");
    f = fontes_obtem(fontes, n, "bruta");
    T_ASSERT(f != NULL, "seção bruta");
    T_ASSERT(strcmp(f->url_zip, "https://exemplo.invalid/x.json") == 0,
             "espaços em volta do = foram aparados");
    T_ASSERT(strcmp(f->dataset, "algo.csv") == 0, "dataset");
    T_ASSERT(f->ttl == 90, "ttl");
    fontes_libera(fontes, n);

    /* --- garantia de download: sem rede, um id inexistente falha --- */
    T_ASSERT(fontes_garente(conf, "inexistente", &corpo, &len) != 0,
             "id inexistente falha mesmo com .conf legível");
    T_ASSERT(corpo == NULL && len == 0, "saída zerada no erro");

    /* --- a garantia respeita o cache: sem conf e com o corpo no cache,
     *     devolve o que está em disco. É o caminho que faz o servidor
     *     funcionar depois do primeiro --atualizar. --- */
    T_ASSERT(cache_grava("bruta", "conteudo-de-teste",
                         sizeof "conteudo-de-teste" - 1) == 0,
             "gravou no cache");
    cache_set_ttl("bruta", CACHE_TTL_NENHUM);
    T_ASSERT(fontes_garente(NULL, "bruta", &corpo, &len) == 0,
             "devolve do cache sem abrir o .conf");
    T_ASSERT(corpo != NULL && len == sizeof "conteudo-de-teste" - 1
             && strcmp(corpo, "conteudo-de-teste") == 0,
             "corpo veio do cache");
    free(corpo);
    corpo = NULL;

    /* TTL expirado: o cache não vale, e sem conf não há de onde baixar.
     * Um .ttl com número negativo é lido como prazo corrompido, e prazo
     * corrompido é expirado — fail-open aqui serviria dado velho sem erro. */
    {
        FILE *tf = fopen("data/cache/bruta.ttl", "wb");
        T_ASSERT(tf != NULL, "abriu o .ttl");
        fputs("-1\n", tf);
        fclose(tf);
    }
    T_ASSERT(fontes_garente(NULL, "bruta", &corpo, &len) != 0,
             "TTL expirado não vale");
    T_ASSERT(corpo == NULL, "nada devolvido com TTL expirado");

    /* --- sufixo e chave de cache próprios (o caso do SIDRA) --- */
    T_ASSERT(cache_grava("sidra_parte_1", "anos-2001-2003",
                         sizeof "anos-2001-2003" - 1) == 0, "cache 2");
    cache_set_ttl("sidra_parte_1", CACHE_TTL_NENHUM);
    T_ASSERT(fontes_baixa(NULL, "so_prefixo", "/2001,2002,2003",
                          "sidra_parte_1", &corpo, &len) == 0,
             "fontes_baixa lê da chave de cache informada");
    T_ASSERT(corpo != NULL && strcmp(corpo, "anos-2001-2003") == 0,
             "corpo do lote");
    free(corpo);
    corpo = NULL;

    remove(conf);
    remove("data/cache/bruta");
    remove("data/cache/bruta.ttl");
    remove("data/cache/sidra_parte_1");
    remove("data/cache/sidra_parte_1.ttl");
}

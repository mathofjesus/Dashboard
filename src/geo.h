/* geo.h -- malhas do IBGE: une, simplifica e cacheia as geometrias.
 *
 * O que entra e o que sai
 * -----------------------
 * Entrada: as FeatureCollections que a API de malhas do IBGE devolve, lidas do
 * cache de download (data/cache/). Um endpoint só para o Brasil inteiro em
 * nível municipal (`ibge_malhas_municipios`); para os estados são cinco
 * chamadas regionais (`ibge_malhas_estados_1`..`5`), porque não existe
 * endpoint único de estados nesse desenho. As cinco são mescladas numa.
 *
 * Saída: um GeoJSON FeatureCollection por nível, em
 * data/processed/geo_municipios.json e data/processed/geo_estados.json.
 *
 * Unidade de coordenada
 * ---------------------
 * TODA coordenada emitida — vértice de geometria e centroide — está em
 * graus multiplicados por 1000, e é sempre inteira. Não é arredondamento
 * decorativo: é o que corta o payload pela metade sem perda visível (1 unidade
 * = ~111 m), e inteiros têm representação textual única, o que torna a saída
 * reproduzível byte a byte. Quem consome (web/map.js) divide por 1000 antes de
 * projetar.
 *
 * Ordenação
 * ---------
 * As features saem em ordem crescente de codarea, não na ordem em que as
 * fontes responderam. Só depois de ler as cinco coleções regionais a ordem
 * fica definida, e é ela que faz duas execuções produzirem bytes idênticos.
 *
 * Contrato de memória
 * -------------------
 * geo_prepara não aloca para quem chama. geo_saida devolve um buffer malloc'd
 * com '\0' no fim e *out_len com o tamanho; quem chama libera com free().
 * Devolve NULL (e *out_len = 0, se out_len não for NULL) quando não há dado
 * processado para o nível — o servidor responde 503 com o comando de
 * atualização em vez de servir um corpo vazio com status 200.
 */
#ifndef GEO_H
#define GEO_H

#include <stddef.h>

typedef enum {
    GEO_ESTADOS,
    GEO_MUNICIPIOS
} geo_nivel;

/* Tolerância da simplificação Douglas-Peucker, em graus (não em unidades de
 * graus×1000: a conversão fica dentro de geo.c).
 *
 * 0,05 é o valor que o plano prescreve para este passo, e é o que está aqui.
 * Medido na malha real de 5.781.544 bytes / 5570 features:
 *
 *   tol    bytes crus   gzip        vertices
 *   0,02   2.086.544   467.041     85.903
 *   0,03   1.749.826   365.906     64.344
 *   0,05   1.485.243   286.450     47.384    <- este
 *   0,07   1.371.672   252.482     40.099
 *   0,10   1.285.199   226.488     34.548
 *   0,15   1.215.928   205.088     30.103
 *
 * O teto de 1.200.000 bytes que o plano pede NÃO é alcançável aqui, e a
 * tabela mostra por quê: 796 KB dos 1.485.243 (54%) são o andaime de JSON
 * repetido 5570 vezes — {"type":"Feature","properties":{…},"geometry":{…} —
 * que nenhuma simplificação de geometria toca. Chegar a 1,2 MB exigiria
 * tol ≈ 0,16°, ou seja, distortionar o litoral e os limites internos por
 * ~17 km para ganhar 285 KB que já vão comprimidos.
 *
 * O que realmente atravessa a rede é a coluna gzip: 286.450 bytes a 0,05°,
 * 4,2× abaixo do teto, porque o servidor já comprime (src/http_server.c) e o
 * navegador guarda isso em cache. O alvo de 1,2 MB foi medido no arquivo em
 * disco; ver task-8-report.md. */
#define TOLERANCIA_SIMPLIFICACAO 0.05

int geo_prepara(geo_nivel nivel);
char *geo_saida(geo_nivel nivel, size_t *out_len);

/* Como geo_saida, mas só com as features cujo codarea pertence à UF indicada.
 *
 * `uf_codigo` é o código numérico do IBGE, como string: "11" para os estados
 * (o codarea de uma UF é o próprio código) ou "11" para filtrar municípios
 * (o codarea de um município começa com o da UF). NULL ou "" devolve tudo,
 * igual a geo_saida. Serve à rota /api/mapa/uf, que quer uma UF só em vez de
 * 1,4 MB de geometria que o navegador já tem em cache.
 *
 * Devolve buffer malloc'd (mesmo contrato de geo_saida) ou NULL.
 */
char *geo_saida_filtrada(geo_nivel nivel, const char *uf_codigo,
                         size_t *out_len);

/* Converte a sigla de uma UF ("ro", "RO", "Rj") no código de duas dígitos do
 * IBGE ("11"). Devolve NULL se não for uma UF válida. */
const char *geo_codigo_uf(const char *sigla);

#endif /* GEO_H */
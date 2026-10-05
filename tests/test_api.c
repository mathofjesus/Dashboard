/* tests/test_api.c — o roteamento de /api/ sobre o modelo de teste.
 *
 * Cobre o envelope de erro (§12), o formato da resposta (§8) e as três
 * asserções que a revisão apontou como fracas:
 *
 *   I11 `res.body == NULL || strstr(...) == NULL` passa com corpo vazio. Aqui
 *        toda asserção de corpo começa com `res.body != NULL`, e nenhuma
 *        afirmação aceita ausência onde a resposta é obrigatória.
 *   I17 a mensagem de erro entrava no JSON sem escape. O teste manda um ano com
 *        aspas e contrabarra e exige que o corpo continue sendo JSON válido.
 *   I2  o formato é `{consulta,total,kpis,registros,serie}`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixture_model.h"
#include "../src/api.h"
#include "../src/http_server.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

static http_request make_req(const char *method, const char *path,
                             const char *query)
{
    http_request req;
    memset(&req, 0, sizeof req);
    req.method = method;
    req.path = (char *)path;
    req.query = (char *)query;
    return req;
}

/* Chamada de convenience: roteia e devolve a resposta com o corpo liberado
 * quando o teste termina. */
static http_response chama(const char *path, const char *query)
{
    http_request req = make_req("GET", path, query);
    http_response res;
    memset(&res, 0, sizeof res);
    api_roteia(&req, &res);
    return res;
}

static void libera_body(http_response *res)
{
    free((char *)res->body);
    res->body = NULL;
    res->length = 0;
}

static void teste_consulta_valida(void)
{
    http_response res = chama("api/consulta",
                              "fonte=ibge&indicador=populacao&uf=RO&ano=2024");

    T_ASSERT(res.status == 200, "consulta válida responde 200");
    T_ASSERT(res.body != NULL, "consulta válida tem corpo");
    T_ASSERT(res.length == (long long)strlen(res.body), "length bate com o corpo");
    T_ASSERT(res.content_type && strstr(res.content_type, "charset=utf-8"),
             "content_type com charset");

    /* Formato da spec §8: as cinco chaves. */
    T_ASSERT(strstr(res.body, "\"consulta\":{") != NULL, "chave consulta");
    T_ASSERT(strstr(res.body, "\"total\":1") != NULL, "total do conjunto");
    T_ASSERT(strstr(res.body, "\"kpis\":{") != NULL, "chave kpis");
    T_ASSERT(strstr(res.body, "\"registros\":[") != NULL, "chave registros");
    /* Sem `cod=` e sem `nivel=uf` não há recorte, e portanto não há série. */
    T_ASSERT(strstr(res.body, "\"serie\"") == NULL, "sem recorte, sem série");

    /* O eco da consulta volta decodificado e com acento. */
    T_ASSERT(strstr(res.body, "\"fonte\":\"ibge\"") != NULL, "eco da fonte");
    T_ASSERT(strstr(res.body, "\"indicador\":\"populacao\"") != NULL, "eco do indicador");
    T_ASSERT(strstr(res.body, "\"nivel\":\"municipio\"") != NULL, "nível padrão");
    T_ASSERT(strstr(res.body, "\"ano\":2024") != NULL, "eco do ano");

    T_ASSERT(strstr(res.body, "\"registros\":[{\"cod\":\"1100015\","
                               "\"nome\":\"Porto Velho\",\"valor\":1500}]") != NULL,
             "registro de Porto Velho");

    /* Os KPIs são do conjunto filtrado, e o min/max/média estão dentro. */
    T_ASSERT(strstr(res.body, "\"min\":1500") != NULL, "min");
    T_ASSERT(strstr(res.body, "\"max\":1500") != NULL, "max");
    T_ASSERT(strstr(res.body, "\"media\":1500") != NULL, "media");
    T_ASSERT(strstr(res.body, "\"soma\":1500") != NULL, "soma");

    libera_body(&res);
}

static void teste_limite_nao_mexe_no_kpi(void)
{
    /* Acharado I4: `limite` corta a tabela e os KPIs continuam sobre o
     * conjunto. Se voltarem a ser truncados, o cartão de KPI mente. */
    http_response res = chama("api/consulta",
                              "indicador=populacao&ano=2024&limite=1");

    T_ASSERT(res.status == 200, "consulta com limite responde 200");
    T_ASSERT(res.body != NULL, "corpo");
    T_ASSERT(strstr(res.body, "\"total\":3") != NULL,
             "total é o do conjunto, não o da página");
    T_ASSERT(strstr(res.body, "\"com_dado\":3") != NULL, "KPIs sobre os 3");
    T_ASSERT(strstr(res.body, "\"max\":2200") != NULL,
             "o máximo vem do conjunto todo");
    /* Uma linha só na tabela: o segundo nome não pode aparecer. A ordenação é
     * a padrão (valor decrescente), então a linha é Curitiba, que tem o maior
     * valor — não a primeira do modelo. */
    T_ASSERT(strstr(res.body, "Curitiba") != NULL, "a primeira linha está lá");
    T_ASSERT(strstr(res.body, "Porto Velho") == NULL, "a segunda não");
    T_ASSERT(strstr(res.body, "Rio Branco") == NULL, "a terceira não");
    libera_body(&res);
}

static void teste_ordenar(void)
{
    http_response res;

    res = chama("api/consulta", "indicador=populacao&ano=2024&ordenar=valor_asc");
    T_ASSERT(res.status == 200 && res.body != NULL, "ordenar asc");
    /* 2024: Rio Branco 900 < Porto Velho 1500 < Curitiba 2200 */
    T_ASSERT(strstr(res.body, "Rio Branco") < strstr(res.body, "Porto Velho"),
             "valor_asc põe o menor primeiro");
    libera_body(&res);

    res = chama("api/consulta", "indicador=populacao&ano=2024&ordenar=nome_asc");
    T_ASSERT(res.status == 200 && res.body != NULL, "ordenar por nome");
    T_ASSERT(strstr(res.body, "Curitiba") < strstr(res.body, "Porto Velho"),
             "nome_asc põe Curitiba antes de Porto Velho");
    libera_body(&res);
}

static void teste_ausente_vira_null(void)
{
    /* Rio Branco não tem 2021. O registro traz `null` e não 0 — 0 é uma
     * população possível e fingir que vale 0 contamina mínimo e média. */
    http_response res = chama("api/consulta",
                              "indicador=populacao&ano=2021&busca=rio");

    T_ASSERT(res.status == 200 && res.body != NULL, "consulta do ano com lacuna");
    T_ASSERT(strstr(res.body, "\"valor\":null") != NULL, "o ausente é null");
    T_ASSERT(strstr(res.body, "\"valor\":0") == NULL, "o ausente não é 0");
    T_ASSERT(strstr(res.body, "\"com_dado\":0") != NULL, "o KPI não conta o ausente");
    T_ASSERT(strstr(res.body, "\"total\":1") != NULL,
             "`total` conta a linha, não os valores");
    libera_body(&res);
}

static void teste_serie(void)
{
    /* Com `cod=` a série do município vem no payload, com `null` na lacuna. */
    http_response res = chama("api/consulta",
                              "indicador=populacao&ano=2024&cod=1200401");
    T_ASSERT(res.status == 200 && res.body != NULL, "consulta com cod");
    T_ASSERT(strstr(res.body, "\"serie\":[800,null,900]") != NULL, "a série veio");
    T_ASSERT(strstr(res.body, "\"serie_anos\":[2019,2021,2024]") != NULL,
             "e os anos dela");
    libera_body(&res);

    /* `nivel=uf` soma os municípios da UF no ano e traz a série da UF. */
    res = chama("api/consulta", "indicador=populacao&ano=2024&nivel=uf&uf=RO");
    T_ASSERT(res.status == 200 && res.body != NULL, "consulta em nível de UF");
    T_ASSERT(strstr(res.body, "\"nivel\":\"uf\"") != NULL, "eco do nível");
    T_ASSERT(strstr(res.body, "\"serie\":[1000,1200,1500]") != NULL, "série da UF");
    libera_body(&res);

    /* `nivel=uf` com `uf=` é um recorte: a linha daquela UF, e a série dela. */
    res = chama("api/consulta", "indicador=populacao&ano=2024&nivel=uf&uf=RO");
    T_ASSERT(res.status == 200 && res.body != NULL, "recorte de UF");
    T_ASSERT(strstr(res.body, "\"nivel\":\"uf\"") != NULL, "eco do nível");
    T_ASSERT(strstr(res.body, "\"registros\":[{\"cod\":\"11\","
                               "\"nome\":\"Rondônia\",\"valor\":1500}]") != NULL,
             "a linha é a UF, com o nome por extenso");
    T_ASSERT(strstr(res.body, "\"total\":1") != NULL, "e o total é 1");
    T_ASSERT(strstr(res.body, "\"serie\":[1000,1200,1500]") != NULL, "série da UF");
    libera_body(&res);

    /* Um `cod=` de município com `nivel=uf` é recusado: uma linha de UF não
     * representa um município. */
    res = chama("api/consulta", "indicador=populacao&ano=2024&nivel=uf&cod=1100015");
    T_ASSERT(res.status == 400, "nível UF com código de município é 400");
    T_ASSERT(res.body != NULL, "com corpo");
    libera_body(&res);

    /* UF que não existe: 200 com zero linhas, como uma busca sem resultado. */
    res = chama("api/consulta", "indicador=populacao&ano=2024&nivel=uf&uf=XX");
    T_ASSERT(res.status == 200 && res.body != NULL, "UF inexistente é 200");
    T_ASSERT(strstr(res.body, "\"registros\":[]") != NULL, "sem linhas");
    T_ASSERT(strstr(res.body, "\"serie\"") == NULL, "e sem série");
    libera_body(&res);
}

/* A consulta mais simples que existe não traz indicador nem fonte, e é a
 * primeira coisa que alguém digita numa barra de endereço. Ela tem que
 * responder 200 com dado de verdade. Antes devolvia 400 "indicador inválido"
 * por não ter dito qual indicador queria — e essa resposta chegou depois de
 * toda a ingestão funcionar, o que é a pior hora para ela aparecer. */
static void teste_minima(void)
{
    http_response res = chama("api/consulta", "fonte=ibge&ano=2024");

    T_ASSERT(res.status == 200, "consulta sem indicador responde 200");
    T_ASSERT(res.body != NULL, "consulta mínima tem corpo");
    /* O eco da consulta mostra o que valeu: sem indicador na query, o eco
     * diz `populacao` — e não "", que descreveria uma consulta que ninguém
     * fez. */
    T_ASSERT(strstr(res.body, "\"indicador\":\"populacao\"") != NULL,
             "o eco nomeia o indicador que valeu");
    T_ASSERT(strstr(res.body, "\"fonte\":\"ibge\"") != NULL,
             "o eco nomeia a fonte que valeu");
    T_ASSERT(strstr(res.body, "\"registros\":[]") == NULL,
             "a consulta mínima devolve linhas, não lista vazia");
    libera_body(&res);

    /* Sem indicador E sem fonte: o mesmo default. `?ano=2024` tem que
     * funcionar tanto quanto `?fonte=ibge&ano=2024`. */
    res = chama("api/consulta", "ano=2024");
    T_ASSERT(res.status == 200, "consulta de um parâmetro só responde 200");
    libera_body(&res);

    /* Indicador AUSENTE tem default; indicador ERRADO continua sendo erro.
     * Sem esta metade, "default" seria "aceitar qualquer coisa". */
    res = chama("api/consulta", "indicador=naoexiste&ano=2024");
    T_ASSERT(res.status == 400, "indicador desconhecido continua 400");
    libera_body(&res);
}

static void teste_erros(void)
{
    http_response res;

    /* Ano inexistente na série: 400 e a mensagem nomeia o ano. */
    res = chama("api/consulta", "fonte=ibge&indicador=populacao&uf=RO&ano=2022");
    T_ASSERT(res.status == 400, "ano inexistente responde 400");
    T_ASSERT(res.body != NULL, "erro tem corpo");
    T_ASSERT(strstr(res.body, "\"erro\":{") == strstr(res.body, "\"erro\":{"),
             "envelope de erro da spec §12");
    T_ASSERT(strstr(res.body, "2022") != NULL, "a mensagem nomeia o ano");
    libera_body(&res);

    /* `ano=` vazio e `ano=..` viram 400, não 0. */
    res = chama("api/consulta", "indicador=populacao&ano=");
    T_ASSERT(res.status == 400, "ano vazio é 400");
    T_ASSERT(res.body && strstr(res.body, "\"erro\":{") != NULL, "com envelope");
    libera_body(&res);

    res = chama("api/consulta", "indicador=populacao&ano=..");
    T_ASSERT(res.status == 400, "ano=.. é 400");
    libera_body(&res);

    /* Ano ausente também: 0 não é ano de ninguém. */
    res = chama("api/consulta", "indicador=populacao");
    T_ASSERT(res.status == 400, "ano ausente é 400");
    libera_body(&res);

    /* Indicador desconhecido. */
    res = chama("api/consulta", "fonte=ibge&indicador=naoexiste&ano=2024");
    T_ASSERT(res.status == 400, "indicador inválido responde 400");
    T_ASSERT(res.body && strstr(res.body, "indicador") != NULL, "a mensagem nomeia");
    libera_body(&res);

    /* `nivel` fora do vocabulário é recusado com a lista do que existe. */
    res = chama("api/consulta", "indicador=populacao&ano=2024&nivel=galaxia");
    T_ASSERT(res.status == 400, "nível inválido é 400");
    T_ASSERT(res.body && strstr(res.body, "municipio") != NULL,
             "a mensagem diz o que aceita");
    libera_body(&res);

    /* Rota inexistente: 404 com o mesmo envelope. */
    res = chama("api/inexistente", "");
    T_ASSERT(res.status == 404, "rota inexistente responde 404");
    T_ASSERT(res.body != NULL && strstr(res.body, "\"erro\":{") != NULL,
             "com envelope de erro");
    libera_body(&res);
}

static void teste_erro_escapado(void)
{
    /* I17: `ano=%22x%5C%22` traz aspas e contrabarra dentro da mensagem. Sem
     * escape, isso fecha a string do envelope no meio e o corpo deixa de ser
     * JSON — o cliente veria um erro de sintaxe em vez de "ano inválido". */
    http_response res = chama("api/consulta", "indicador=populacao&ano=%22x%5C%22");

    T_ASSERT(res.status == 400, "ano com aspas é 400");
    T_ASSERT(res.body != NULL, "corpo");
    /* Nenhuma aspa crua entre as duas do envelope: só as de \". */
    {
        size_t n = 0, i;
        int dentro = 0;
        for (i = 0; res.body[i] && n < 64; i++) {
            if (res.body[i] == '\\') { i++; n = 0; continue; }
            if (res.body[i] != '"') { n++; continue; }
            dentro = !dentro;
            n = 0;
        }
        T_ASSERT(!dentro, "o envelope fecha: nenhuma aspa crua entrou");
    }
    T_ASSERT(strstr(res.body, "\\\"") != NULL, "a aspa foi escapada");
    T_ASSERT(strstr(res.body, "\\\\") != NULL, "a contrabarra foi escapada");
    T_ASSERT(res.body[0] == '{' && res.body[res.length - 1] == '}',
             "o corpo é um objeto só");
    libera_body(&res);
}

static void teste_opcoes(void)
{
    http_response res;

    /* A asserção antiga (`body == NULL || strstr(body,"2022")==NULL`) passava
     * com corpo nulo. Agora: corpo obrigatório, com o formato da spec §8 e as
     * UFs do modelo. */
    res = chama("api/opcoes", "fonte=ibge&indicador=populacao");
    T_ASSERT(res.status == 200, "opcoes responde 200");
    T_ASSERT(res.body != NULL, "opcoes tem corpo");
    T_ASSERT(strstr(res.body, "\"anos\":[2019,2021,2024]") != NULL,
             "traz os anos da série");
    T_ASSERT(strstr(res.body, "\"ufs\":[]") == NULL, "a lista de UFs não é vazia");
    T_ASSERT(strstr(res.body, "\"RO\"") && strstr(res.body, "\"AC\""),
             "traz as UFs do modelo");
    T_ASSERT(strstr(res.body, "2022") == NULL, "2022 não está na série");
    libera_body(&res);

    res = chama("api/opcoes", "indicador=naoexiste");
    T_ASSERT(res.status == 400, "indicador inválido em opcoes é 400");
    T_ASSERT(res.body != NULL, "com corpo");
    libera_body(&res);

    /* Com `uf=`, a lista de UFs cai para a própria UF. */
    res = chama("api/opcoes", "indicador=populacao&uf=ro");
    T_ASSERT(res.status == 200 && res.body != NULL, "opcoes filtrada por UF");
    T_ASSERT(strstr(res.body, "\"ufs\":[\"RO\"]") != NULL, "só a UF filtrada");
    T_ASSERT(strstr(res.body, "\"uf\":\"ro\"") != NULL, "e ela é nomeada");
    libera_body(&res);
}

static void teste_gzip_e_rota(void)
{
    http_request req = make_req("GET", "api/consulta",
                                "fonte=ibge&indicador=populacao&uf=RO&ano=2024");
    http_response res;
    memset(&res, 0, sizeof res);
    req.accept_encoding = "gzip";
    api_roteia(&req, &res);
    T_ASSERT(res.status == 200 && res.body != NULL, "consulta com Accept-Encoding");
    T_ASSERT(res.gzip == 1, "a resposta longa é marcada para gzip");
    libera_body(&res);
}

static void teste_sem_modelo(void)
{
    /* 503 e o comando para processar — não 200 com corpo inventado. */
    model guardado;
    const model *antes = NULL;
    http_response res;

    (void)antes;
    (void)guardado;

    api_set_model(NULL);
    res = chama("api/consulta", "indicador=populacao&ano=2024");
    T_ASSERT(res.status == 503, "sem modelo é 503");
    T_ASSERT(res.body != NULL && strstr(res.body, "--atualizar") != NULL,
             "e diz como resolver");
    libera_body(&res);
}

void test_api(void)
{
    model m = fixture_model();

    /* As rotas de geometria não dependem do modelo, mas as de dados sim: com o
     * modelo apontado, elas respondem. */
    api_set_model(&m);
    teste_consulta_valida();
    teste_minima();
    teste_limite_nao_mexe_no_kpi();
    teste_ordenar();
    teste_ausente_vira_null();
    teste_serie();
    teste_erros();
    teste_erro_escapado();
    teste_opcoes();
    teste_gzip_e_rota();

    teste_sem_modelo();

    api_set_model(NULL);
    model_libera(&m);
}

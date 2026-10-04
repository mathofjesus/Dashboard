#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/model.h"
#include "../src/api.h"
#include "../src/http_server.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

static model criar_model_teste(void)
{
    model m;
    memset(&m, 0, sizeof(m));

    m.n_local = 3;
    m.local = malloc(m.n_local * sizeof(locality));

    strcpy(m.local[0].cod, "1100015");
    strcpy(m.local[0].nome, "Porto Velho");
    strcpy(m.local[0].uf, "RO");
    strcpy(m.local[0].regiao, "Norte");

    strcpy(m.local[1].cod, "1200401");
    strcpy(m.local[1].nome, "Rio Branco");
    strcpy(m.local[1].uf, "AC");
    strcpy(m.local[1].regiao, "Norte");

    strcpy(m.local[2].cod, "4106902");
    strcpy(m.local[2].nome, "Curitiba");
    strcpy(m.local[2].uf, "PR");
    strcpy(m.local[2].regiao, "Sul");

    m.n_anos = 3;
    m.anos = malloc(m.n_anos * sizeof(size_t));
    m.anos[0] = 2019;
    m.anos[1] = 2021;
    m.anos[2] = 2024;
    m.ano_de = malloc(m.n_anos * sizeof(int));
    for (size_t i = 0; i < m.n_anos; i++) {
        m.ano_de[i] = (int)i;
    }

    for (int i = 0; i < IND_TOTAL; i++) {
        for (size_t j = 0; j < 5570; j++) {
            m.valores[i][j] = -1;
            m.soma[i][j] = 0;
        }
    }

    size_t idx0 = 0 * m.n_anos;
    m.valores[IND_POPULACAO][idx0 + 0] = 1000;
    m.valores[IND_POPULACAO][idx0 + 1] = 1200;
    m.valores[IND_POPULACAO][idx0 + 2] = 1500;

    size_t idx1 = 1 * m.n_anos;
    m.valores[IND_POPULACAO][idx1 + 0] = 800;
    m.valores[IND_POPULACAO][idx1 + 1] = -1;
    m.valores[IND_POPULACAO][idx1 + 2] = 900;

    size_t idx2 = 2 * m.n_anos;
    m.valores[IND_POPULACAO][idx2 + 0] = 2000;
    m.valores[IND_POPULACAO][idx2 + 1] = 2100;
    m.valores[IND_POPULACAO][idx2 + 2] = 2200;

    return m;
}

static http_request make_req(const char *method, const char *path, const char *query)
{
    http_request req;
    req.method = method;
    req.path = (char *)path;
    req.query = (char *)query;
    req.range_header = NULL;
    req.accept_encoding = NULL;
    return req;
}

void test_api(void)
{
    model m = criar_model_teste();
    api_set_model(&m);

    /* Caso 1: consulta válida */
    {
        http_request req = make_req("GET", "api/consulta", "fonte=ibge&indicador=populacao&uf=RO&ano=2024");
        http_response res;
        memset(&res, 0, sizeof(res));
        int r = api_roteia(&req, &res);
        T_ASSERT(r == 0 || res.status == 200, "consulta deve retornar 200");
        T_ASSERT(res.status == 200, "status 200");
        T_ASSERT(res.content_type != NULL && strstr(res.content_type, "charset=utf-8") != NULL,
                  "content_type com charset");
        T_ASSERT(res.body != NULL && strstr(res.body, "\"total\":1") != NULL, "total 1");
        T_ASSERT(res.body != NULL && strstr(res.body, "Porto Velho") != NULL, "tem Porto Velho");
    }

    /* Caso 2: ano 2022 inexistente */
    {
        http_request req = make_req("GET", "api/consulta", "fonte=ibge&indicador=populacao&uf=RO&ano=2022");
        http_response res;
        memset(&res, 0, sizeof(res));
        int r = api_roteia(&req, &res);
        (void)r;
        T_ASSERT(res.status == 400, "ano inexistente retorna 400");
        T_ASSERT(res.body != NULL && strstr(res.body, "\"erro\"") != NULL, "tem erro");
        T_ASSERT(res.body != NULL && strstr(res.body, "2022") != NULL, "erro contem 2022");
    }

    /* Caso 3: rota inexistente */
    {
        http_request req = make_req("GET", "api/inexistente", "");
        http_response res;
        memset(&res, 0, sizeof(res));
        int r = api_roteia(&req, &res);
        (void)r;
        T_ASSERT(res.status == 404, "rota inexistente 404");
        T_ASSERT(res.body != NULL && strstr(res.body, "{\"erro\":{") != NULL, "envelope de erro");
    }

    /* Caso 4: indicador naoexiste */
    {
        http_request req = make_req("GET", "api/consulta", "fonte=ibge&indicador=naoexiste&ano=2024");
        http_response res;
        memset(&res, 0, sizeof(res));
        int r = api_roteia(&req, &res);
        (void)r;
        T_ASSERT(res.status == 400, "indicador invalido 400");
    }

    /* Caso 5: opcoes */
    {
        http_request req = make_req("GET", "api/opcoes", "fonte=ibge&indicador=populacao");
        http_response res;
        memset(&res, 0, sizeof(res));
        int r = api_roteia(&req, &res);
        (void)r;
        T_ASSERT(res.status == 200, "opcoes 200");
        T_ASSERT(res.body == NULL || strstr(res.body, "2022") == NULL, "nao contem 2022");
    }

    /* Caso 6: gzip */
    {
        http_request req = make_req("GET", "api/consulta", "fonte=ibge&indicador=populacao&uf=RO&ano=2024");
        req.accept_encoding = "gzip";
        http_response res;
        memset(&res, 0, sizeof(res));
        int r = api_roteia(&req, &res);
        (void)r;
        T_ASSERT(res.gzip == 1, "gzip deve ser 1");
    }

    model_libera(&m);
}

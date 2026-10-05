/* src/aggregate.c — as consultas que a API faz sobre o modelo.
 *
 *   - aggregate_consulta: a tabela filtrada, com ordenação, limite e KPIs;
 *   - aggregate_opcoes:  o que de fato existe (anos e UFs da série);
 *   - aggregate_serie:   a série histórica de um município, para o gráfico de
 *                        linha — com `null` no ano que a série não cobre;
 *   - aggregate_serie_uf: a mesma coisa somada por UF (nível=uf).
 *
 * Três decisões que explicam o resto:
 *
 * 1. O ÍNDICE VEM DO MODELO, NUNCA DA ARITMÉTICA. O passo é `n_anos` e
 *    `i * n_anos + k` era calculado à mão em aggregate.c:122 sobre um array
 *    declarado `int[IND_TOTAL][5570]`: com 5570 municípios e 22 anos o índice
 *    chegava a 122 539 numa linha de 5570, ou seja, leitura 20 bytes depois
 *    do fim da linha (achado C4). Quem indexa agora é `model_valor()`, que
 *    confere os três limites antes de tocar a memória.
 *
 * 2. `total` É O DO CONJUNTO, `limite` É O DA TABELA. São coisas diferentes:
 *    o cartão de KPI diz "5570 municípios" mesmo quando a tabela mostra os 50
 *    primeiros, e min/max/média são sobre o conjunto todo. Antes, `limite`
 *    truncava `n` **antes** dos KPIs, e o cartão dizia 100 enquanto a média
 *    vinha de 5570 (achado I4).
 *
 * 3. AUSENTE É -1 E VIRA `null` NO JSON. A série do SIDRA tem lacunas (2007,
 *    2022 e 2023 não existem) e um município pode não existir no primeiro ano.
 *    Um -1 não conta para os KPIs nem para o total; 0 contaria, e
 *    contaminaria o mínimo, o máximo e a média.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "aggregate.h"

void aggregate_libera(consulta *c)
{
    if (!c) return;
    free(c->cod);
    free(c->nome);
    free(c->valor);
    free(c->ordem);
    c->cod = NULL;
    c->nome = NULL;
    c->valor = NULL;
    c->ordem = NULL;
    c->n = 0;
}

/* ------------------------------------------------------------------ */
/* busca e filtro                                                      */
/* ------------------------------------------------------------------ */

/* Case-insensitive sobre bytes, sem `strcasestr` (POSIX e nem sempre presente)
 * e sem `tolower` aplicado a char possivelmente negativo. */
static int contem_ci(const char *hay, const char *needle)
{
    size_t i, j, k;

    if (!needle || !*needle) return 1;
    if (!hay) return 0;
    k = strlen(needle);
    for (i = 0; hay[i]; i++) {
        for (j = 0; j < k && hay[i + j]; j++) {
            int a = tolower((unsigned char)hay[i + j]);
            int b = tolower((unsigned char)needle[j]);
            if (a != b) break;
        }
        if (j == k) return 1;
    }
    return 0;
}

/* A UF aparece na query de três formas, e todas precisam dar a mesma coisa:
 * a sigla em qualquer caixa ("ro", "RO"), o código de dois dígitos do IBGE
 * ("11", "41") e o nome por extenso ("Rondônia"), que é o que a pessoa digita.
 *
 * O código casa pelo **prefixo do código do município**, não pelos bytes da
 * sigla: "11" e "RO" não têm nenhum byte em comum, e comparar os dois
 * primeiros bytes de cada um — como fazia a versão anterior — nunca casa com
 * um e casa errado com o outro. Uma UF nunca é uma letra só, então `uf=R`
 * não devolve nada em vez de devolver "RO". */
static int uf_casa(const locality *l, const char *nome_uf, const char *filtro)
{
    size_t n;

    if (!filtro || !*filtro) return 1;
    if (!l->uf[0]) return 0;
    n = strlen(filtro);
    if (n < 2) return 0;                 /* "R" não é uma UF: não casa */
    if (n == 2 &&
        isdigit((unsigned char)filtro[0]) && isdigit((unsigned char)filtro[1]))
        return l->cod[0] == filtro[0] && l->cod[1] == filtro[1];
    if (n == 2)
        return tolower((unsigned char)l->uf[0]) == tolower((unsigned char)filtro[0]) &&
               tolower((unsigned char)l->uf[1]) == tolower((unsigned char)filtro[1]);
    return nome_uf && contem_ci(nome_uf, filtro);
}

static int municipio_entra(const model *m, const locality *l, const char *uf,
                           const char *busca)
{
    if (l->cod[0] == '\0') return 0;
    if (uf && *uf) {
        size_t pos = model_uf_de_sigla(m, uf);
        const char *nome_uf = (pos < m->n_ufs) ? m->ufs[pos].nome : NULL;
        if (!uf_casa(l, nome_uf, uf)) return 0;
    }
    if (busca && busca[0] &&
        !contem_ci(l->nome, busca) && !contem_ci(l->cod, busca)) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* ordenação                                                           */
/* ------------------------------------------------------------------ */

typedef enum { ORD_VALOR_DESC, ORD_VALOR_ASC, ORD_NOME_ASC } modo_ordem;

static modo_ordem modo_de(const char *ordenar)
{
    if (ordenar) {
        if (strcmp(ordenar, "valor_asc") == 0) return ORD_VALOR_ASC;
        if (strcmp(ordenar, "nome_asc") == 0) return ORD_NOME_ASC;
    }
    return ORD_VALOR_DESC;
}

/* O comparador recebe o índice de entrada, não as linhas: são quatro arrays
 * paralelos, e permutar structs de 103 bytes dentro do comparador seria
 * desperdício. qsort não é estável, e a ordem de saída precisa ser
 * reprodutível byte a byte entre execuções (o mesmo argumento de
 * geo.c:cmp_feature) — o índice de chegada desempata.
 *
 * O contexto vai em duas variáveis de arquivo em vez de `qsort_r`, que tem
 * assinatura própria de plataforma. O servidor atende cada conexão em um
 * filho (`fork`), e o fork herda um estado consistente: quem chama põe o
 * contexto antes do qsort e o limpa logo depois. */
static const consulta *g_ctx_consulta;
static modo_ordem g_ctx_modo;

static int cmp_global(const void *pa, const void *pb)
{
    const size_t *ia = pa;
    const size_t *ib = pb;

    if (g_ctx_modo == ORD_NOME_ASC) {
        int c = strcmp(g_ctx_consulta->nome[*ia], g_ctx_consulta->nome[*ib]);
        if (c != 0) return c;
    } else {
        int va = g_ctx_consulta->valor[*ia];
        int vb = g_ctx_consulta->valor[*ib];
        if (va == MODELO_AUSENTE && vb != MODELO_AUSENTE) return 1;
        if (vb == MODELO_AUSENTE && va != MODELO_AUSENTE) return -1;
        if (va != vb)
            return (g_ctx_modo == ORD_VALOR_ASC) ? (va < vb ? -1 : 1)
                                                 : (va < vb ? 1 : -1);
    }
    if (*ia < *ib) return -1;
    if (*ia > *ib) return 1;
    return 0;
}

/* Esta UF passa pelo filtro `uf=`? Mesma gramática do filtro de município:
 * nada, sigla em qualquer caixa, código de dois dígitos ou nome por extenso. */
static int uf_entra(const model *m, const uf_info *u, const char *filtro)
{
    char cod[8];

    if (!filtro || !*filtro) return 1;
    if (model_uf_de_sigla(m, filtro) < m->n_ufs) {
        size_t pos = model_uf_de_sigla(m, filtro);
        /* Casa pela identidade resolvida: assim "11" e "RO" dão a mesma UF,
         * porque quem decide é model_uf_de_sigla e não a comparação de bytes. */
        return strcmp(m->ufs[pos].sigla, u->sigla) == 0;
    }
    /* Não é uma UF do modelo: tenta o nome por extenso, que é o que a pessoa
     * digita quando a sigla não colou. */
    snprintf(cod, sizeof cod, "%s", filtro);
    return contem_ci(u->nome, cod) || contem_ci(u->sigla, cod);
}

/* Acumula um valor nos KPIs. `v == MODELO_AUSENTE` não conta: nem para o
 * `total`, nem para o mínimo, nem para a média. Contar 0 aqui é o que faria o
 * cartão dizer "população mínima 0" por causa de uma lacuna da série. */
static void conta_kpi(kpis *k, int v)
{
    k->linhas++;
    if (v == MODELO_AUSENTE) return;
    if (k->com_dado == 0) {
        k->min = v;
        k->max = v;
    } else {
        if (v < k->min) k->min = v;
        if (v > k->max) k->max = v;
    }
    k->com_dado++;
    k->soma += v;
}

/* ------------------------------------------------------------------ */
/* a consulta                                                          */
/* ------------------------------------------------------------------ */

int aggregate_consulta(const model *m, indicador ind, int nivel_uf,
                       const char *uf, int ano, const char *busca,
                       const char *ordenar, int limite,
                       consulta *out, kpis *kpis_out, char *erro, size_t erro_len)
{
    size_t i, idx, n_total = 0;

    if (!m || !out || !kpis_out) return -1;
    if (erro && erro_len > 0) erro[0] = '\0';
    memset(out, 0, sizeof *out);
    memset(kpis_out, 0, sizeof *kpis_out);

    if (ind < 0 || ind >= IND_TOTAL) {
        if (erro && erro_len > 0)
            snprintf(erro, erro_len, "indicador inválido");
        return -1;
    }
    if (m->n_anos == 0 || !m->anos) {
        if (erro && erro_len > 0)
            snprintf(erro, erro_len,
                     "não há dado processado: rode ./dashboard --atualizar ibge");
        return -1;
    }
    if (model_indice_ano(m, ano) >= m->n_anos) {
        if (erro && erro_len > 0)
            snprintf(erro, erro_len, "ano %d inexistente na série", ano);
        return -1;
    }
    if (nivel_uf && (!m->ufs || m->n_ufs == 0)) {
        if (erro && erro_len > 0)
            snprintf(erro, erro_len, "o modelo não tem tabela de UFs");
        return -1;
    }

    /* --- 1. quem entra no resultado --- */
    /* Conta antes de reservar: em nível de UF com `busca`, nem toda UF entra,
     * e reservar as 27 para devolver 3 seria trabalho jogado fora. */
    if (nivel_uf) {
        for (i = 0; i < m->n_ufs; i++) {
            const char *sigla = m->ufs[i].sigla;
            if (!uf_entra(m, &m->ufs[i], uf)) continue;
            if (busca && busca[0] &&
                !contem_ci(sigla, busca) &&
                !contem_ci(m->ufs[i].nome, busca)) continue;
            n_total++;
        }
    } else {
        for (i = 0; i < m->n_local; i++)
            if (municipio_entra(m, &m->local[i], uf, busca)) n_total++;
    }

    if (n_total) {
        out->cod = malloc(n_total * 8);
        out->nome = malloc(n_total * 96);
        out->valor = malloc(n_total * sizeof(int));
        out->ordem = malloc(n_total * sizeof(size_t));
        if (!out->cod || !out->nome || !out->valor || !out->ordem) {
            aggregate_libera(out);
            if (erro && erro_len > 0)
                snprintf(erro, erro_len, "memória insuficiente para a consulta");
            return -1;
        }
    }

    /* --- 2. cópia e KPIs sobre o conjunto inteiro --- */
    idx = 0;
    if (nivel_uf) {
        for (i = 0; i < m->n_ufs; i++) {
            const char *sigla = m->ufs[i].sigla;
            const char *extenso = m->ufs[i].nome;
            const locality *primeiro = NULL;
            long long soma = 0;
            int tem = 0;
            size_t j;

            /* Em nível de UF, `uf=` é um recorte de verdade — a linha
             * "Rondônia" do conjunto de 27. Não é ambíguo como parecia na
             * primeira versão: agregar por UF e escolher uma delas é a forma
             * mais comum de usar o filtro, e recusar deixava o front sem poder
             * clicar numa UF e ver a série dela.
             *
             * `cod=` (um município), esse sim, é recusado: uma linha de UF não
             * pode representar um município, e devolvê-lo sob o nome da UF
             * seria pior do que erro. */
            if (!uf_entra(m, &m->ufs[i], uf)) continue;

            /* A busca em nível de UF é sobre a UF (sigla ou nome), não sobre o
             * nome do município: quem procura "Rondônia" quer a linha
             * "Rondônia", não uma soma de todos os 52 municípios do estado. */
            if (busca && busca[0] &&
                !contem_ci(sigla, busca) && !contem_ci(extenso, busca)) continue;

            for (j = 0; j < m->n_local; j++) {
                int v;
                if (strcmp(m->local[j].uf, sigla) != 0) continue;
                if (!primeiro) primeiro = &m->local[j];
                v = model_valor(m, ind, j, ano);
                if (v == MODELO_AUSENTE) continue;
                soma += v;
                tem = 1;
            }

            /* O código da linha é o de dois dígitos da UF — o prefixo do código
             * do município, que é o mesmo número que `geo_codigo_uf` e
             * `uf=` na query aceitam. */
            memset(out->cod[idx], 0, 8);
            if (primeiro) {
                out->cod[idx][0] = primeiro->cod[0];
                out->cod[idx][1] = primeiro->cod[1];
            } else {
                size_t k = strlen(sigla);
                if (k > 7) k = 7;
                memcpy(out->cod[idx], sigla, k);
            }
            snprintf(out->nome[idx], 96, "%s", extenso[0] ? extenso : sigla);
            /* A soma de uma UF sem nenhum município com dado é ausente, não 0:
             * 0 seria uma UF que ninguém habita. E a soma é conferida contra
             * INT_MAX antes de virar int — a população do Brasil cabe, mas um
             * indicador contado em pessoas por vários vínculo, não. */
            out->valor[idx] = !tem                 ? MODELO_AUSENTE
                            : soma > 2147483647LL ? MODELO_AUSENTE
                                                  : (int)soma;
            out->ordem[idx] = idx;
            idx++;
        }
    } else {
        for (i = 0; i < m->n_local; i++) {
            int v;
            if (!municipio_entra(m, &m->local[i], uf, busca)) continue;

            v = model_valor(m, ind, i, ano);
            memcpy(out->cod[idx], m->local[i].cod, sizeof m->local[i].cod);
            memcpy(out->nome[idx], m->local[i].nome, sizeof m->local[i].nome);
            out->valor[idx] = v;
            out->ordem[idx] = idx;
            idx++;
            conta_kpi(kpis_out, v);
        }
    }
    if (nivel_uf)
        for (i = 0; i < idx; i++) conta_kpi(kpis_out, out->valor[i]);

    if (kpis_out->com_dado > 0)
        kpis_out->media = (int)(kpis_out->soma / (long long)kpis_out->com_dado);

    /* `idx` é o número de linhas realmente preenchidas e deve ser igual ao que
     * foi contado em `n_total`; se não for, alguma das duas contagens errou e
     * é melhor devolver erro do que um array com linha pela metade. */
    out->n = idx;
    if (idx != n_total) {
        aggregate_libera(out);
        if (erro && erro_len > 0)
            snprintf(erro, erro_len, "contagem interna inconsistente");
        return -1;
    }

    /* --- 3. ordenação --- */
    /* O front oferece as três (web/filters.js). Implementar só uma era o pior
     * resultado possível: a opção era aceita e ignorada, e a tabela saía em
     * ordem arbitrária sem nenhuma pista. */
    if (out->n > 1) {
        g_ctx_consulta = out;
        g_ctx_modo = modo_de(ordenar);
        qsort(out->ordem, out->n, sizeof(size_t), cmp_global);
        g_ctx_consulta = NULL;

        /* qsort reordena só `ordem`; os três arrays paralelos andam juntos. */
        {
            char (*cod)[8] = malloc(out->n * 8);
            char (*nome)[96] = malloc(out->n * 96);
            int *valor = malloc(out->n * sizeof(int));
            if (cod && nome && valor) {
                for (i = 0; i < out->n; i++) {
                    size_t o = out->ordem[i];
                    memcpy(cod[i], out->cod[o], 8);
                    memcpy(nome[i], out->nome[o], 96);
                    valor[i] = out->valor[o];
                }
                memcpy(out->cod, cod, out->n * 8);
                memcpy(out->nome, nome, out->n * 96);
                memcpy(out->valor, valor, out->n * sizeof(int));
            }
            free(cod);
            free(nome);
            free(valor);
        }
    }

    /* --- 4. limite, só sobre o que é devolvido --- */
    if (limite > 0 && out->n > (size_t)limite) out->n = (size_t)limite;
    return 0;
}

/* ------------------------------------------------------------------ */
/* série                                                               */
/* ------------------------------------------------------------------ */

/* JSON de uma série histórica: `[1000, null, 1200]`. O `null` é o ano que a
 * série não cobre, e é o que o gráfico de linha precisa para quebrar a linha
 * em vez de emendar 2021 em 2024.
 *
 * `soma_uf` != NULL muda o significado de `local`: em vez do índice de um
 * município, é o índice de uma UF e o valor do ano é a soma dos municípios
 * dela — que é o que `nivel=uf` promete. */
static char *serie_de(const model *m, indicador ind, size_t local,
                      int por_uf, size_t *n_anos)
{
    size_t cap, w = 0, i;
    char *buf;

    if (n_anos) *n_anos = m->n_anos;
    if (m->n_anos == 0) return NULL;
    cap = m->n_anos * 24 + 64;
    buf = malloc(cap);
    if (!buf) return NULL;

    w += (size_t)snprintf(buf + w, cap - w, "[");
    for (i = 0; i < m->n_anos; i++) {
        long long v = 0;
        int tem = 0;
        size_t j;

        if (por_uf) {
            const char *sigla = m->ufs[local].sigla;
            for (j = 0; j < m->n_local; j++) {
                int x;
                if (strcmp(m->local[j].uf, sigla) != 0) continue;
                x = model_valor(m, ind, j, m->anos[i]);
                if (x == MODELO_AUSENTE) continue;
                v += x;
                tem = 1;
            }
        } else {
            int x = model_valor(m, ind, local, m->anos[i]);
            if (x != MODELO_AUSENTE) { v = x; tem = 1; }
        }

        if (i > 0) w += (size_t)snprintf(buf + w, cap - w, ",");
        if (!tem) w += (size_t)snprintf(buf + w, cap - w, "null");
        else     w += (size_t)snprintf(buf + w, cap - w, "%lld", v);
        if (w + 32 > cap) { free(buf); return NULL; }   /* paranoia de estouro */
    }
    w += (size_t)snprintf(buf + w, cap - w, "]");
    buf[w] = '\0';
    return buf;
}

char *aggregate_serie(const model *m, indicador ind, const char *cod,
                      size_t *n)
{
    size_t pos;

    if (!m || !cod || !n) return NULL;
    if (ind < 0 || ind >= IND_TOTAL) return NULL;
    pos = model_local_de_cod(m, cod);
    if (pos >= m->n_local) return NULL;
    return serie_de(m, ind, pos, 0, n);
}

char *aggregate_serie_uf(const model *m, indicador ind, const char *uf,
                         size_t *n)
{
    size_t pos;

    if (!m || !uf || !n) return NULL;
    if (ind < 0 || ind >= IND_TOTAL) return NULL;
    pos = model_uf_de_sigla(m, uf);
    if (pos >= m->n_ufs) return NULL;
    return serie_de(m, ind, pos, 1, n);
}

/* ------------------------------------------------------------------ */
/* opções                                                              */
/* ------------------------------------------------------------------ */

/* A spec §8 pede "anos e UFs **realmente disponíveis**". Uma lista fixa de
 * UFs seria mentira se o dado não trouxesse a UF, e foi literalmente o que
 * estava aqui: `"ufs": []`, fixo, com `(void)ind` em cima. */
char *aggregate_opcoes(const model *m, indicador ind)
{
    size_t i, w = 0, cap;
    char *buf;

    if (!m) return NULL;
    if (ind < 0 || ind >= IND_TOTAL) return NULL;

    /* Cada ano até 4 dígitos, cada UF entre aspas + 2 bytes, mais folga: o
     * `malloc(4096)` fixo de antes seria apertado demais assim que as 27 UFs
     * entrassem, e o `strcat` sem limite estouraria (achado M4). */
    cap = m->n_anos * 8 + m->n_ufs * 16 + 128;
    buf = malloc(cap);
    if (!buf) return NULL;

    w += (size_t)snprintf(buf + w, cap - w, "{\"anos\":[");
    for (i = 0; i < m->n_anos; i++)
        w += (size_t)snprintf(buf + w, cap - w, "%s%d", i ? "," : "", m->anos[i]);

    w += (size_t)snprintf(buf + w, cap - w, "],\"ufs\":[");
    for (i = 0; i < m->n_ufs; i++)
        w += (size_t)snprintf(buf + w, cap - w, "%s\"%s\"", i ? "," : "",
                              m->ufs[i].sigla);

    w += (size_t)snprintf(buf + w, cap - w, "]}");
    buf[w] = '\0';
    return buf;
}
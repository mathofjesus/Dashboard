/* dataset_saude.c — Tarefa 12: o CNES podado, em JSON Lines.
 *
 * O caminho inteiro, e por que ele é este:
 *
 *   ZIP  ->  (zip_extrai)  ->  CSV em ISO-8859-1  ->  csv_abre_mem
 *        ->  seis colunas por linha  ->  transcodifica para UTF-8
 *        ->  uma linha JSON por estabelecimento  ->  data/processed/cnes.json
 *
 * Cada etapa existe por um motivo medido, e não por simetria com o resto:
 *
 * - `zip_extrai` devolve o CSV inteiro em memória: 231.007.483 bytes no
 *   arquivo real (medido), dos quais este código usa seis colunas. É caro, e
 *   é por isso que `--limite-amostra` existe — mas ele ainda descomprime tudo,
 *   porque o índice central do ZIP só está no fim do arquivo. Um ZIP de 56 MB
 *   com entrada única e descompressão sequencial permitiria ler em corrente; o
 *   que existe não. O custo está anotado em dataset_saude.h.
 *
 * - As colunas são localizadas por NOME (`csv_indice_coluna`), nunca por
 *   posição. A ordem do CSV real é CO_CNES, CO_UNIDADE, CO_UF, CO_IBGE… e
 *   mudar a ordem no arquivo de origem não pode trocar latitude por longitude
 *   em silêncio. Só depois de localizá-las é que a poda acontece.
 *
 * - A transcodificação é por campo e na passagem. `csv_abre_mem` com
 *   `CSV_ISO88591` não converte: devolve bytes. O 0xE3 do "SÃO" vira 0xC3 0x83
 *   aqui, e é isso que separa um "SÃO" de um "SÃ£o" na tela.
 *
 * - A ausência é -1. Um município que não veio, ou veio como "..", é -1 — e
 *   nunca 0, porque 0 estabelecimentos é um dado possível e "não sei" virando
 *   0 contaminaria a soma de quem somar os dois.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dataset_saude.h"
#include "charset.h"
#include "csv.h"
#include "zip.h"

/* As seis colunas que sobrevivem, NESTA ordem. O navegador lê por posição, e
 * `CO_IBGE` é primeiro porque é a chave de junção com o modelo e com a malha. */
const char *const SAUDE_COLUNAS_CNES[] = {
    "CO_IBGE",
    "NO_RAZAO_SOCIAL",
    "NO_FANTASIA",
    "DS_NIVEL_HIERARQUIA",
    "NU_LATITUDE",
    "NU_LONGITUDE",
};
const size_t SAUDE_N_COLUNAS_CNES = 6;

/* Preenchidas por src/dataset_saude.h; a Tarefa 12 implementa CNES no Passo 4 e
 * Leitos/Dengue no 6, e só o CNES tem coluna podada até lá. */
const char *const SAUDE_COLUNAS_LEITOS[] = {
    "CO_IBGE", "LEITOS_SUS", "UTI_TOTAL_SUS",
};
const size_t SAUDE_N_COLUNAS_LEITOS = 3;

const char *const SAUDE_COLUNAS_DENGUE[] = {
    "ID_MUNICIP", "SEM_PRI", "SEM_PRD",
};
const size_t SAUDE_N_COLUNAS_DENGUE = 3;

/* ------------------------------------------------------------------ */
/* auxiliares                                                          */
/* ------------------------------------------------------------------ */

/* O texto do CSV é ISO-8859-1; a saída é UTF-8. Um campo que já é ASCII passa
 * pelo mesmo caminho e sai igual — converter ASCII em UTF-8 é a identidade, e
 * tratar os dois casos separadamente só criaria dois jeitos de dar errado. */
static char *para_utf8(const char *campo)
{
    if (!campo) return NULL;
    return text_latin1_para_utf8(campo, strlen(campo), NULL);
}

/* Um campo de código de município. Vazio ou ".." é ausência, e ausência é -1:
 * o número que o aggregate do modelo já usa para a mesma coisa. */
static const char *codigo_ou_ausente(const char *bruto)
{
    if (!bruto) return NULL;
    while (*bruto == ' ' || *bruto == '\t') bruto++;
    if (bruto[0] == '\0') return NULL;
    if (strcmp(bruto, "..") == 0) return NULL;
    if (strcmp(bruto, "...") == 0) return NULL;
    return bruto;
}

/* JSON: escapa e escreve. Devolve 1 se escreveu, 0 se o buffer não comportou —
 * e nesse caso a linha inteira é descartada, porque meia linha de JSON é
 * pior do que uma linha a menos. */
static int escreve_json_str(FILE *f, const char *s)
{
    char *esc = para_utf8(s);
    if (!esc) return fputc('""', f) != EOF;
    esc = text_json_escape(esc);
    if (!esc) return fputc('""', f) != EOF;
    fputc('"', f);
    fputs(esc, f);
    fputc('"', f);
    free(esc);
    return 1;
}

/* A primeira linha: o que o arquivo é e o que ele traz. Ela existe para que o
 * navegador saiba o que esperar antes do primeiro registro, e para que uma
 * mudança no conjunto de colunas seja detectada em vez de produzir valores
 * trocados em silêncio. */
static void escreve_cabecalho(FILE *f, const char *formato, size_t linhas,
                              const char *const *cols, size_t n_cols)
{
    size_t i;
    fprintf(f, "{\"formato\":\"%s\",\"linhas\":%zu,\"colunas\":[", formato, linhas);
    for (i = 0; i < n_cols; i++) {
        if (i) fputc(',', f);
        fputc('"', f);
        fputs(cols[i], f);
        fputc('"', f);
    }
    fprintf(f, "],\"chaves\":[");
    /* As chaves do objeto de cada linha, na mesma ordem. O navegador usa isto
     * para não repetir os nomes 638 mil vezes — 300 KB que o JSON Lines
     * economiza e que o Passo 9 transforma em typed arrays. */
    for (i = 0; i < n_cols; i++) {
        static const char *const CHAVES[] = { "codarea", "razao", "fantasia",
                                               "nivel", "lat", "lon" };
        if (i >= sizeof CHAVES / sizeof CHAVES[0]) break;
        if (i) fputc(',', f);
        fprintf(f, "\"%s\"", CHAVES[i]);
    }
    fputs("]}\n", f);
}

/* ------------------------------------------------------------------ */
/* CNES                                                                */
/* ------------------------------------------------------------------ */

int saude_processa_cnes(const char *zip_local, const char *saida,
                        long limite_amostra)
{
    zip_arq z;
    csv *c = NULL;
    char *bruto = NULL;
    size_t bruto_len = 0;
    FILE *out = NULL;
    size_t idx[SAUDE_N_COLUNAS_CNES];
    size_t i, entrada_csv = 0;
    int encontrada_csv = 0;
    long escritas = 0;
    int rc = 1;

    if (!zip_local || !saida) {
        fprintf(stderr, "saude_processa_cnes: caminho do ZIP ou da saída é "
                        "vazio\n");
        return 1;
    }

    if (zip_abre(zip_local, &z) != 0) {
        fprintf(stderr, "saude_processa_cnes: não consegui abrir o ZIP %s\n",
                zip_local);
        return 1;
    }

    /* A entrada é o primeiro `.csv` do ZIP. O real se chama
     * `cnes_estabelecimentos.csv`, e escolher pelo sufixo em vez de pela
     * posição é o que sobrevive a uma exportação que ganhe um `README`. */
    for (i = 0; i < z.n; i++) {
        const char *nome = z.entradas[i].nome ? z.entradas[i].nome : "";
        size_t n = strlen(nome);
        if (n > 4 && strcmp(nome + n - 4, ".csv") == 0) {
            entrada_csv = i;
            encontrada_csv = 1;
            break;
        }
    }
    if (!encontrada_csv) {
        fprintf(stderr, "saude_processa_cnes: nenhum .csv dentro de %s\n",
                zip_local);
        goto fim_zip;
    }

    bruto = zip_extrai(&z, entrada_csv, &bruto_len);
    if (!bruto || bruto_len == 0) {
        fprintf(stderr, "saude_processa_cnes: o CSV de dentro de %s saiu "
                        "vazio\n", zip_local);
        goto fim_zip;
    }
    fprintf(stderr, "    %s: %zu bytes, %zu entradas\n",
            z.entradas[entrada_csv].nome ? z.entradas[entrada_csv].nome : "csv",
            bruto_len, z.n);

    /* ISO-8859-1: é o que o CNES publica. O que sai daqui vira UTF-8 na escrita. */
    c = csv_abre_mem(bruto, bruto_len, csv_detecta_delimitador(bruto, bruto_len),
                     CSV_ISO88591);
    if (!c) {
        fprintf(stderr, "saude_processa_cnes: não consegui ler o CSV\n");
        goto fim_csv;
    }

    /* Colunas por nome. Se alguma das seis não existir, o CSV não é o que o
     * plano diz que é — e seguir adiante escreveria nomes trocados. */
    for (i = 0; i < SAUDE_N_COLUNAS_CNES; i++) {
        int pos = csv_indice_coluna(c, SAUDE_COLUNAS_CNES[i]);
        if (pos < 0) {
            fprintf(stderr, "saude_processa_cnes: o CSV não tem a coluna %s\n",
                    SAUDE_COLUNAS_CNES[i]);
            goto fim_csv;
        }
        idx[i] = (size_t)pos;
    }

    out = fopen(saida, "wb");
    if (!out) {
        fprintf(stderr, "saude_processa_cnes: não consegui abrir %s para "
                        "escrita\n", saida);
        goto fim_csv;
    }

    {
        size_t total = csv_num_linhas(c);
        long alvo = (limite_amostra > 0 && (size_t)limite_amostra < total)
                    ? (size_t)limite_amostra : (total > 0 ? total - 1 : 0);

        /* O cabeçalho anuncia o total de registros que o ARQUIVO tem, não o que
         * este processamento escreveu: quem vai paginar por Range precisa
         * saber onde o arquivo termina, e com `--limite-amostra` o total real
         * seria uma mentira que ninguém acreditaria. */
        escreve_cabecalho(out, "cnes", total > 0 ? total - 1 : 0,
                          SAUDE_COLUNAS_CNES, SAUDE_N_COLUNAS_CNES);

        for (i = 1; i <= alvo; i++) {
            char campo[512];
            const char *cod;

            /* --- codarea --- */
            if (csv_campo(c, i, idx[0], campo, sizeof campo) != 0) campo[0] = '\0';
            cod = codigo_ou_ausente(campo);
            if (cod) fputs("{\"codarea\":\"", out);
            else     fputs("{\"codarea\":-1,\"codigo_bruto\":", out);

            if (!cod) {
                /* O valor original vai junto quando é ausente: um ".." na
                 * origem diz "o IBGE não mandou", e um vazio diz "o CNES não
                 * preencheu". São ausências diferentes e a auditoria precisa
                 * saber qual foi. */
                if (!escreve_json_str(out, campo)) { fputc('\n', out); continue; }
                fputc(',', out);
            } else {
                if (!escreve_json_str(out, cod)) { fputc('\n', out); continue; }
                fputc(',', out);
            }

            /* --- razao, fantasia, nivel: texto, transcodificado --- */
            if (csv_campo(c, i, idx[1], campo, sizeof campo) != 0) campo[0] = '\0';
            if (!escreve_json_str(out, campo)) { fputc('\n', out); continue; }
            fputc(',', out);
            if (csv_campo(c, i, idx[2], campo, sizeof campo) != 0) campo[0] = '\0';
            if (!escreve_json_str(out, campo)) { fputc('\n', out); continue; }
            fputc(',', out);
            if (csv_campo(c, i, idx[3], campo, sizeof campo) != 0) campo[0] = '\0';
            if (!escreve_json_str(out, campo)) { fputc('\n', out); continue; }
            fputc(',', out);

            /* --- lat e lon: número, e null quando o CNES não tem --- */
            if (csv_campo(c, i, idx[4], campo, sizeof campo) != 0) campo[0] = '\0';
            {
                const char *p = campo;
                while (*p == ' ') p++;
                if (p[0] == '\0' || strcmp(p, "..") == 0) fputs("\"lat\":null,", out);
                else fprintf(out, "\"lat\":%.6f,", strtod(p, NULL));
            }
            if (csv_campo(c, i, idx[5], campo, sizeof campo) != 0) campo[0] = '\0';
            {
                const char *p = campo;
                while (*p == ' ') p++;
                if (p[0] == '\0' || strcmp(p, "..") == 0) fputs("\"lon\":null", out);
                else fprintf(out, "\"lon\":%.6f", strtod(p, NULL));
            }

            fputs("}\n", out);
            escritas++;
        }
    }

    if (ferror(out)) {
        fprintf(stderr, "saude_processa_cnes: falha de escrita em %s\n", saida);
        goto fim_out;
    }

    fprintf(stderr, "    %ld estabelecimentos gravados em %s\n", escritas, saida);
    rc = 0;

fim_out:
    fclose(out);
fim_csv:
    csv_fecha(c);
    free(bruto);
fim_zip:
    zip_fecha(&z);
    return rc;
}
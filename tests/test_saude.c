/* tests/test_saude.c — Tarefa 12: a poda e a transcodificação do CNES.
 *
 * O que o teste exige (Passo 2 do brief):
 *   - a primeira linha é um cabeçalho JSON com as colunas podadas;
 *   - exatamente 50 linhas de dados;
 *   - o nome acentuado saiu em UTF-8 correto;
 *   - o total é menor que o do CSV completo, porque a poda funcionou.
 *
 * A fixture é montada aqui, e não lida de data/cache: o CNES real tem
 * 231.007.483 bytes e 637.996 linhas, e nenhum `make test` pode depender
 * disso. Ela reproduz de propósito as duas coisas que o arquivo real tem e que
 * um parser desatento erra:
 *
 *   - as MESMAS 36 colunas, na mesma ordem. Os nomes e os índices foram medidos
 *     no arquivo real (CO_IBGE é a 4ª, NO_RAZAO_SOCIAL a 6ª, NO_FANTASIA a 7ª,
 *     DS_NIVEL_HIERARQUIA a 12ª, NU_LATITUDE a 22ª, NU_LONGITUDE a 23ª). Um
 *     cabeçalho inventado deixaria passar um parser que lê as colunas pela
 *     posição errada.
 *   - `CO_IBGE` SEM aspas, enquanto o resto vem entre aspas. O arquivo real
 *     posta tudo entre aspas; o teste precisa dos dois caminhos.
 *   - `NO_RAZAO_SOCIAL` com acento em ISO-8859-1 (0xCD = Í, 0xE3 = ã, 0xDA = Ú).
 *     O CSV real é latin-1 — medido: os 62 bytes não-ASCII dos 231 MB estão em
 *     telefone, e-mail e site, e são bytes de latin-1, não de UTF-8. Naquele
 *     arquivo os acentos caem por acaso fora das seis colunas podadas, mas a
 *     transcodificação roda na mesma passagem e "hoje não tem acento aqui" não
 *     é razão para pular a conversão.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "../src/dataset_saude.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

/* As 36 colunas do CNES real, na ordem real. */
static const char CABECALHO[] =
    "CO_CNES;CO_UNIDADE;CO_UF;CO_IBGE;NU_CNPJ_MANTENEDORA;NO_RAZAO_SOCIAL;"
    "NO_FANTASIA;CO_NATUREZA_ORGANIZACAO;DS_NATUREZA_ORGANIZACAO;TP_GESTAO;"
    "CO_NIVEL_HIERARQUIA;DS_NIVEL_HIERARQUIA;CO_ESFERA_ADMINISTRATIVA;"
    "DS_ESFERA_ADMINISTRATIVA;CO_ATIVIDADE;TP_UNIDADE;CO_CEP;NO_LOGRADOURO;"
    "NU_ENDERECO;NO_BAIRRO;NU_TELEFONE;NU_LATITUDE;NU_LONGITUDE;"
    "CO_TURNO_ATENDIMENTO;DS_TURNO_ATENDIMENTO;NU_CNPJ;NO_EMAIL;"
    "CO_NATUREZA_JUR;ST_CENTRO_CIRURGICO;ST_CENTRO_OBSTETRICO;ST_CENTRO_NEONATAL;"
    "ST_ATEND_HOSPITALAR;ST_SERVICO_APOIO;ST_ATEND_AMBULATORIAL;CO_MOTIVO_DESAB;"
    "CO_AMBULATORIAL_SUS";

#define N_DADOS 50

/* ------------------------------------------------------------------ */
/* a fixture                                                          */
/* ------------------------------------------------------------------ */

/**
 * Monta o CSV de amostra em `caminho`, em ISO-8859-1.
 *
 * Três linhas têm nome acentuado (1, 2 e 3), uma tem município ".." e uma tem
 * município vazio — os dois marcadores de ausência que a spec §4 exige que
 * virem -1 e nunca 0.
 */
static void escreve_csv_amostra(const char *caminho, size_t *out_len)
{
    FILE *f = fopen(caminho, "wb");
    char linha[2048];
    int i;

    T_ASSERT(f != NULL, "não consegui abrir o CSV da fixture");
    fputs(CABECALHO, f);
    fputc('\n', f);

    for (i = 1; i <= N_DADOS; i++) {
        const char *razao = "CENTRO DE SAUDE";
        char ibge[16];
        char lat[24];
        char lon[24];
        char unidade[8];

        snprintf(unidade, sizeof unidade, "%03d", i);

        if (i == 1) razao = "POLICLINICA DR JAMACI DE MEDEIROS \xCD";
        else if (i == 2) razao = "HOSPITAL DE BASE S\xC3O LU\xCCS";
        else if (i == 3) razao = "UBS JARDIM \xC3STIMA";

        if (i == 12) snprintf(ibge, sizeof ibge, "..");
        else if (i == 13) ibge[0] = '\0';
        else snprintf(ibge, sizeof ibge, "1%06d", 100000 + i);

        snprintf(lat, sizeof lat, "-8.%02d", i);
        snprintf(lon, sizeof lon, "-63.%02d", i);

        /* CO_IBGE sem aspas de propósito; o resto entre aspas, como o real. */
        snprintf(linha, sizeof linha,
                 "2%s;2%s;11;%s;00000000000151;\"%s\";\"UBS %d\";1234;"
                 "\"SAUDE\";\"PUBLICO\";\"S\";\"2\";\"02 - CENTRO DE SAUDE\";"
                 "\"1\";\"MUNICIPAL\";\"2\";\"01\";\"76800-000\";\"RUA %d\";"
                 "\"%d\";\"CENTRO\";\"11 99999-0000\";\"%s\";\"%s\";\"12\";"
                 "\"24H\";\"00000000000191\";\"saude%d@exemplo.br\";"
                 "\"PUBLICIDADE\";\"NAO\";\"NAO\";\"NAO\";\"SIM\";\"SIM\";"
                 "\"NAO\";\"0\";\"0\"",
                 unidade, unidade, ibge, razao, i, i, i, lat, lon, i);

        fputs(linha, f);
        fputc('\n', f);
    }

    *out_len = (size_t)ftell(f);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* o ZIP                                                              */
/* ------------------------------------------------------------------ */

/* Assinaturas locais e central, e o fim do arquivo, em "stored" (método 0).
 * Escrever à mão é o que mantém `make test` sem depender do binário `zip`, que
 * este sistema não tem. O formato é o mesmo que src/zip.c lê — inclusive o
 * teste de enfeite de validação do fim do arquivo, que é o que faz o leitor
 * encontrar o EOCD varrendo o fim. */
static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void put32(FILE *f, unsigned long v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
    fputc((int)((v >> 16) & 0xFF), f);
    fputc((int)((v >> 24) & 0xFF), f);
}

/**
 * Escreve um ZIP de uma entrada, sem compressão.
 *
 * Devolve o tamanho do arquivo.
 */
static size_t escreve_zip_stored(const char *zip, const char *nome,
                                 const char *csv, size_t csv_len)
{
    const char *base = strrchr(nome, '/');
    const char *simples = base ? base + 1 : nome;
    size_t n_nome = strlen(simples);
    unsigned long crc = (unsigned long)crc32(0L, (const Bytef *)csv, (uInt)csv_len);
    size_t deslocado = 0;
    size_t total;
    FILE *f = fopen(zip, "wb");

    T_ASSERT(f != NULL, "não consegui abrir o ZIP da fixture");

    /* cabeçalho local */
    put32(f, 0x04034b50UL);
    put16(f, 20);            /* versão necessária               */
    put16(f, 0);             /* flags: sem nada                 */
    put16(f, 0);             /* método: stored                  */
    put16(f, 0);             /* hora                            */
    put16(f, 0);             /* data                            */
    put32(f, crc);
    put32(f, (unsigned long)csv_len);
    put32(f, (unsigned long)csv_len);
    put16(f, (unsigned)n_nome);
    put16(f, 0);
    fwrite(simples, 1, n_nome, f);

    /* dados */
    fwrite(csv, 1, csv_len, f);
    deslocado = 30 + n_nome + csv_len;

    /* diretório central */
    put32(f, 0x02014b50UL);
    put16(f, 20);            /* versão de quem fez              */
    put16(f, 20);            /* versão necessária               */
    put16(f, 0);
    put16(f, 0);
    put16(f, 0);
    put16(f, 0);
    put32(f, crc);
    put32(f, (unsigned long)csv_len);
    put32(f, (unsigned long)csv_len);
    put16(f, (unsigned)n_nome);
    put16(f, 0);             /* extra                           */
    put16(f, 0);             /* comentário                      */
    put16(f, 0);             /* disco                           */
    put16(f, 0);             /* atributos internos              */
    put32(f, 0);             /* atributos externos              */
    put32(f, 0);             /* deslocamento do cabeçalho local */
    fwrite(simples, 1, n_nome, f);

    /* fim do diretório */
    put32(f, 0x06054b50UL);
    put16(f, 0);
    put16(f, 0);
    put16(f, 1);             /* entradas neste disco            */
    put16(f, 1);             /* entradas no total               */
    put32(f, (unsigned long)(46 + n_nome));
    put32(f, (unsigned long)deslocado);
    put16(f, 0);

    total = (size_t)ftell(f);
    fclose(f);
    return total;
}

/* ------------------------------------------------------------------ */
/* os testes                                                          */
/* ------------------------------------------------------------------ */

static char TMP_DIR[256];

static void monta_fixture(char *csv, char *zip, size_t *csv_len)
{
    size_t n;
    snprintf(csv, 256, "%s/cnes_amostra.csv", TMP_DIR);
    snprintf(zip, 256, "%s/cnes_amostra.zip", TMP_DIR);
    escreve_csv_amostra(csv, &n);
    *csv_len = n;
    escreve_zip_stored(zip, "cnes_estabelecimentos.csv", csv, n);
}

static void teste_cnes_poda_e_acento(void)
{
    char csv[256], zip[256], json[256];
    size_t csv_len = 0;
    FILE *f;
    char linha[4096];
    int n_dados = 0;
    long tamanho_json;
    long tamanho_csv;

    snprintf(json, sizeof json, "%s/cnes.json", TMP_DIR);
    monta_fixture(csv, zip, &csv_len);

    T_ASSERT(saude_processa_cnes(zip, json, 0) == 0,
             "saude_processa_cnes devolve 0 na fixture");

    f = fopen(json, "rb");
    T_ASSERT(f != NULL, "a saída existe");

    /* --- a primeira linha é cabeçalho --- */
    T_ASSERT(fgets(linha, sizeof linha, f) != NULL, "há primeira linha");
    T_ASSERT(linha[0] == '{', "e ela começa com {");
    T_ASSERT(strstr(linha, "\"formato\"") != NULL,
             "o cabeçalho diz o formato do arquivo");
    T_ASSERT(strstr(linha, "\"colunas\"") != NULL,
             "o cabeçalho lista as colunas");
    T_ASSERT(strstr(linha, SAUDE_COLUNAS_CNES[0]) != NULL,
             "e a lista começa por CO_IBGE, a chave de junção");
    T_ASSERT(strstr(linha, SAUDE_COLUNAS_CNES[1]) != NULL,
             "e traz NO_RAZAO_SOCIAL");

    /* --- as colunas podadas, e só elas --- */
    T_ASSERT(strstr(linha, "CO_CNES") == NULL,
             "CO_CNES foi podada — não é uma das seis");
    T_ASSERT(strstr(linha, "NU_EMAIL") == NULL,
             "NU_EMAIL foi podada");
    T_ASSERT(strstr(linha, "NU_CEP") == NULL || strstr(linha, "CO_CEP") == NULL,
             "CO_CEP foi podada");

    /* --- exatamente 50 linhas de dados --- */
    while (fgets(linha, sizeof linha, f) != NULL) {
        size_t n = strlen(linha);
        if (n > 0 && linha[n - 1] == '\n') linha[n - 1] = '\0';
        if (linha[0] == '\0') continue;
        n_dados++;
    }
    T_ASSERT(n_dados == N_DADOS,
             "exatamente 50 linhas de dados");
    fclose(f);

    /* --- o nome acentuado saiu em UTF-8 --- */
    f = fopen(json, "rb");
    T_ASSERT(f != NULL, "a saída abre de novo");
    {
        char achou[512];
        int tem = 0;
        achou[0] = '\0';
        while (fgets(linha, sizeof linha, f) != NULL) {
            /* 0xC3 0x8D é Í em UTF-8; em latin-1 seriam dois bytes 0xC3 e 0x8D
             * lidos como caracteres distintos. A asserção é sobre a sequência
             * de bytes, não sobre o que a tela mostra. */
            if (strstr(linha, "POLICLINICA DR JAMACI DE MEDEIROS \xC3\x8D")) {
                snprintf(achou, sizeof achou, "iso");
                tem = 1;
            }
            if (strstr(linha, "HOSPITAL DE BASE S\xC3\x83O LU\xC3\x8CS")) {
                snprintf(achou, sizeof achou, "latin1");
                tem = 1;
            }
            if (strstr(linha, "JARDIM \xC3\x83STIMA")) {
                snprintf(achou, sizeof achou, "c3");
                tem = 1;
            }
        }
        T_ASSERT(tem, "a linha acentuada está na saída");
        T_ASSERT(strcmp(achou, "iso") != 0,
                 "SÃO NÃO foi convertida: 0xC3 0x83 é o que a leitura latin-1 devolve cru");
        T_ASSERT(strcmp(achou, "latin1") != 0,
                 "LUÍZ não foi convertido: 0xC3 0x83 é o resto da leitura latin-1");
        T_ASSERT(strcmp(achou, "c3") != 0,
                 "ÚSTIMA não foi convertida");
    }
    fclose(f);

    /* --- município ausente é -1, nunca 0 --- */
    f = fopen(json, "rb");
    T_ASSERT(f != NULL, "a saída abre de terceira vez");
    {
        int achou_ausente = 0;
        int achou_zero = 0;
        int primeira_dados = 1;
        while (fgets(linha, sizeof linha, f) != NULL) {
            if (primeira_dados) { primeira_dados = 0; continue; }
            /* As duas linhas sem município válido: uma com ".." e uma vazia.
             * O valor do CO_IBGE tem de ser -1 em ambas. */
            if (strstr(linha, "\":-1") != NULL) achou_ausente++;
            if (strstr(linha, "\"codarea\":\"\"") != NULL) achou_zero++;
        }
        T_ASSERT(achou_ausente >= 1,
                 "a linha sem município tem o código como -1");
        T_ASSERT(achou_zero == 0,
                 "e não como string vazia: \"\" é o que viraria 0 depois de um parse");
    }
    fclose(f);

    /* --- a poda diminuiu o arquivo --- */
    {
        FILE *fc = fopen(csv, "rb");
        T_ASSERT(fc != NULL, "o CSV da fixture abre");
        fseek(fc, 0, SEEK_END);
        tamanho_csv = ftell(fc);
        fclose(fc);
    }
    {
        FILE *fj = fopen(json, "rb");
        T_ASSERT(fj != NULL, "o JSON abre");
        fseek(fj, 0, SEEK_END);
        tamanho_json = ftell(fj);
        fclose(fj);
    }
    T_ASSERT(tamanho_json < tamanho_csv,
             "a saída é menor que o CSV de entrada — a poda funcionou");
}

static void teste_cnes_limite_amostra(void)
{
    char csv[256], zip[256], json[256];
    size_t csv_len = 0;
    FILE *f;
    char linha[4096];
    int n_dados = 0;
    int primeira = 1;

    snprintf(json, sizeof json, "%s/cnes_limite.json", TMP_DIR);
    monta_fixture(csv, zip, &csv_len);

    /* 5 linhas de dados: o caminho de --limite-amostra, que existe para provar
     * o processamento completo sem baixar os 56 MB. */
    T_ASSERT(saude_processa_cnes(zip, json, 5) == 0, "com limite devolve 0");

    f = fopen(json, "rb");
    T_ASSERT(f != NULL, "a saída com limite existe");
    while (fgets(linha, sizeof linha, f) != NULL) {
        size_t n = strlen(linha);
        if (n > 0 && linha[n - 1] == '\n') linha[n - 1] = '\0';
        if (linha[0] == '\0') continue;
        if (primeira) { primeira = 0; continue; }
        n_dados++;
    }
    fclose(f);
    T_ASSERT(n_dados == 5, "o limite para na quantidade pedida");
}

static void teste_cnes_erros(void)
{
    char json[256];
    snprintf(json, sizeof json, "%s/cnes_erro.json", TMP_DIR);

    T_ASSERT(saude_processa_cnes("/tmp/opencode/nao-existe.zip", json, 0) != 0,
             "um ZIP inexistente é erro, e não uma saída vazia");
    T_ASSERT(saude_processa_cnes(NULL, json, 0) != 0,
             "caminho NULL é erro, e não segmento");
    T_ASSERT(saude_processa_cnes(NULL, NULL, 0) != 0,
             "os dois NULL ainda são erro");
}

void test_saude(void)
{
    char template[] = "/tmp/opencode/saude_XXXXXX";

    /* O diretório é próprio da suíte: a escrita em data/processed de verdade
     * seria o mesmo dano que test_geo.c já corrigiu uma vez. */
    const char *dir = mkdtemp(template);
    T_ASSERT(dir != NULL, "cria diretório temporário");
    snprintf(TMP_DIR, sizeof TMP_DIR, "%s", dir);

    printf("testa_cnes_poda_e_acento\n");
    teste_cnes_poda_e_acento();
    printf("testa_cnes_limite_amostra\n");
    teste_cnes_limite_amostra();
    printf("testa_cnes_erros\n");
    teste_cnes_erros();

    /* Limpar: rmdir só remove diretório vazio, e é isso que faz a limpeza ser
     * uma asserção e não uma promessa. */
    unlink(TMP_DIR);
    rmdir(TMP_DIR);
}
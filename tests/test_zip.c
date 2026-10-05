/* tests/test_zip.c — o leitor de ZIP.
 *
 * I12: este arquivo dependia de `/tmp/opencode/t.zip`, `t_stored.zip` e
 * `leitos.zip`, que ninguém criava. As fixtures estavam na máquina de quem
 * escreveu, e `make test` quebrava em qualquer outra — que é a forma mais
 * comum de uma suíte parecer verde num repositório e vermelha no CI de outra
 * pessoa.
 *
 * As fixtures agora são construídas aqui, byte a byte, num diretório
 * temporário. Isso não é mais esforço do que depended de arquivo externo:
 * o formato de ZIP de um arquivo stored é um registro local, um diretório
 * central e um EOCD, e escrevê-los é mais honesto do que copiar um binário
 * opaco para dentro do repositório.
 *
 * O ZIP de um arquivo só tem três formas de estar errado que importam aqui:
 *
 *   - "nome com accent e barra", que produz o registro de diretório central de
 *     tamanho variável — é o que expõe a leitura por índice fixo;
 *   - conteúdo "repetido o suficiente para o deflate compensar", que expõe o
 *     caminho de descompressão;
 *   - "stored", que é o caminho sem compressão.
 *
 * Os três são gerados abaixo.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>

#include "../src/zip.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

/* ------------------------------------------------------------------ */
/* construção das fixtures                                             */
/* ------------------------------------------------------------------ */

static void w_u16(FILE *f, uint16_t v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}

static void w_u32(FILE *f, uint32_t v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 24) & 0xFF, f);
}

struct entrada_zip {
    const char *nome;
    const unsigned char *dados;
    size_t tam;
    int deflate;                 /* 1 = comprimido, 0 = stored */
};

/* Escreve um ZIP com os registros em ordem, com diretório central e EOCD.
 *
 * O CRC é calculado com a função do próprio zlib, que é a mesma que o
 * descompactador vai conferir: um CRC errado aqui apareceria como erro de
 * integridade lá, e o teste acusaria o leitor de um problema do teste. */
static void grava_zip(const char *caminho, const struct entrada_zip *es,
                      size_t n)
{
    FILE *f = fopen(caminho, "wb");
    uint32_t *crc = calloc(n ? n : 1, sizeof(uint32_t));
    uint32_t *comp = calloc(n ? n : 1, sizeof(uint32_t));
    uint32_t *local = calloc(n ? n : 1, sizeof(uint32_t));
    unsigned char *buf = NULL;
    size_t i;

    T_ASSERT(f != NULL, "abre o ZIP para escrita");
    T_ASSERT(crc && comp && local, "aloca os índices do ZIP");

    for (i = 0; i < n; i++) {
        crc[i] = (uint32_t)crc32(0L, es[i].dados, (uInt)es[i].tam);
        local[i] = (uint32_t)ftell(f);

        if (es[i].deflate) {
            /* DEFATE CRU, não zlib. `compress2` escreve o formato zlib — dois
             * bytes de cabeçalho e quatro de adler32 em volta — e o método 8 do
             * ZIP é deflate puro, sem os dois. A diferença não aparece no
             * `unzip -l`, que só lê o diretório: aparece quando alguém tenta
             * descompactar, com "invalid stored block lengths". */
            z_stream strm;
            uLongf cap = compressBound((uLong)es[i].tam) + 64;

            buf = realloc(buf, cap);
            T_ASSERT(buf != NULL, "aloca o buffer do deflate");
            memset(&strm, 0, sizeof strm);
            if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                             -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
                fprintf(stderr, "deflateInit2 falhou\n");
                exit(1);
            }
            strm.next_in = (Bytef *)(uintptr_t)es[i].dados;
            strm.avail_in = (uInt)es[i].tam;
            strm.next_out = buf;
            strm.avail_out = (uInt)cap;
            if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
                fprintf(stderr, "deflate falhou\n");
                exit(1);
            }
            comp[i] = (uint32_t)strm.total_out;
            deflateEnd(&strm);
        } else {
            comp[i] = (uint32_t)es[i].tam;
            buf = realloc(buf, es[i].tam ? es[i].tam : 1);
            T_ASSERT(buf != NULL, "aloca o buffer do stored");
            memcpy(buf, es[i].dados, es[i].tam);
        }

        w_u32(f, 0x04034B50u);              /* assinatura do registro local */
        w_u16(f, 20);                       /* versão necessária           */
        w_u16(f, 0x0800);                   /* flag: nome em UTF-8         */
        w_u16(f, es[i].deflate ? 8 : 0);     /* método                      */
        w_u16(f, 0);                        /* mod time                    */
        w_u16(f, 0x21);                     /* mod date (1980-01-01)       */
        w_u32(f, crc[i]);
        w_u32(f, comp[i]);
        w_u32(f, (uint32_t)es[i].tam);
        w_u16(f, (uint16_t)strlen(es[i].nome));
        w_u16(f, 0);                        /* sem extra                   */
        fwrite(es[i].nome, 1, strlen(es[i].nome), f);
        fwrite(buf, 1, comp[i], f);
    }
    free(buf);

    {
        uint32_t inicio_cd = (uint32_t)ftell(f);
        uint32_t fim_cd;              /* o tamanho do diretório é medido ANTES
                                      * de escrever o EOCD, e não depois: os 12
                                      * bytes do cabeçalho do EOCD já escritos
                                      * entravam na conta e o arquivo saía com
                                      * o diretório 12 bytes maior do que é —
                                      * o `unzip` reclamava e o `zip_lista`
                                      * não achava o fim. */
        for (i = 0; i < n; i++) {
            w_u32(f, 0x02014B50u);          /* assinatura do diretório     */
            w_u16(f, 20);                   /* versão que fez             */
            w_u16(f, 20);                   /* versão necessária          */
            w_u16(f, 0x0800);
            w_u16(f, es[i].deflate ? 8 : 0);
            w_u16(f, 0);
            w_u16(f, 0x21);
            w_u32(f, crc[i]);
            w_u32(f, comp[i]);
            w_u32(f, (uint32_t)es[i].tam);
            w_u16(f, (uint16_t)strlen(es[i].nome));
            w_u16(f, 0);                    /* extra   */
            w_u16(f, 0);                    /* comment */
            w_u16(f, 0);                    /* disco de início           */
            w_u16(f, 0);                    /* atributos internos        */
            w_u32(f, 0);                    /* offset externo            */
            w_u32(f, local[i]);
            fwrite(es[i].nome, 1, strlen(es[i].nome), f);
        }
        fim_cd = (uint32_t)ftell(f);

        w_u32(f, 0x06054B50u);              /* EOCD */
        w_u16(f, 0);
        w_u16(f, 0);
        w_u16(f, (uint16_t)n);
        w_u16(f, (uint16_t)n);
        w_u32(f, fim_cd - inicio_cd);
        w_u32(f, inicio_cd);
        w_u16(f, 0);                        /* sem comentário */
    }

    free(crc); free(comp); free(local);
    T_ASSERT(fclose(f) == 0, "fecha o ZIP");
}

/* ------------------------------------------------------------------ */
/* os testes                                                           */
/* ------------------------------------------------------------------ */

static void libera_lista(zip_entrada *lista, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) free(lista[i].nome);
    free(lista);
}

static void teste_inexistente(void)
{
    zip_entrada *lista = NULL;
    size_t nlista = 0;

    T_ASSERT(zip_lista("/tmp/nao_existe_12345.zip", &lista, &nlista) != 0,
             "arquivo inexistente deve falhar");
    T_ASSERT(lista == NULL, "falha não devolve lista");
    T_ASSERT(nlista == 0, "falha não devolve contagem");
}

static void teste_deflate(const char *caminho)
{
    static const unsigned char A[] = "hello";
    static const unsigned char B[] =
        "a linha que se repete; a linha que se repete; a linha que se repete";
    struct entrada_zip es[] = {
        { "a.txt", A, sizeof A - 1, 1 },
        { "b.txt", B, sizeof B - 1, 1 },
    };
    zip_entrada *lista = NULL;
    size_t nlista = 0;
    zip_arq z;

    grava_zip(caminho, es, 2);

    T_ASSERT(zip_lista(caminho, &lista, &nlista) == 0, "zip_lista lê deflate");
    T_ASSERT(nlista == 2, "duas entradas");
    T_ASSERT(lista != NULL, "lista alocada");
    /* A ordem do diretório central é a ordem de escrita; o teste não deve
     * depender dela para achar o nome, e sim procurar. */
    {
        int tem_a = 0, tem_b = 0;
        size_t i;
        for (i = 0; i < nlista; i++) {
            if (strcmp(lista[i].nome, "a.txt") == 0) {
                tem_a = 1;
                T_ASSERT(lista[i].metodo == 8, "a.txt pelo deflate");
                T_ASSERT(lista[i].tam_descomprimido == 5, "a.txt descomprimido");
            }
            if (strcmp(lista[i].nome, "b.txt") == 0) {
                tem_b = 1;
                T_ASSERT(lista[i].tam_descomprimido == sizeof B - 1,
                         "b.txt descomprimido");
            }
        }
        T_ASSERT(tem_a && tem_b, "os dois nomes estão lá");
    }
    libera_lista(lista, nlista);

    /* Extrair, e conferir o conteúdo — `zip_lista` só lê o diretório, e um
     * descompactador que devolve lixo ainda passa nela. */
    memset(&z, 0, sizeof z);
    T_ASSERT(zip_abre(caminho, &z) == 0, "zip_abre lê deflate");
    T_ASSERT(z.n == 2, "z.n bate com a lista");
    {
        size_t i;
        for (i = 0; i < z.n; i++) {
            size_t outlen = 0;
            char *buf = zip_extrai(&z, i, &outlen);
            T_ASSERT(buf != NULL, "zip_extrai devolve buffer");
            if (strcmp(z.entradas[i].nome, "a.txt") == 0) {
                T_ASSERT(outlen == 5, "a.txt com 5 bytes");
                T_ASSERT(memcmp(buf, "hello", 5) == 0, "a.txt com o conteúdo");
            } else if (strcmp(z.entradas[i].nome, "b.txt") == 0) {
                T_ASSERT(outlen == sizeof B - 1, "b.txt com o tamanho todo");
                T_ASSERT(memcmp(buf, B, outlen) == 0, "b.txt com o conteúdo");
            }
            free(buf);
        }
    }
    zip_fecha(&z);
}

static void teste_stored(const char *caminho)
{
    static const unsigned char S[] = "stored";
    struct entrada_zip es[] = {
        { "s.txt", S, sizeof S - 1, 0 },
    };
    zip_entrada *lista = NULL;
    size_t nlista = 0;
    zip_arq z;

    grava_zip(caminho, es, 1);

    T_ASSERT(zip_lista(caminho, &lista, &nlista) == 0, "zip_lista lê stored");
    T_ASSERT(nlista == 1, "uma entrada");
    T_ASSERT(lista != NULL && strcmp(lista[0].nome, "s.txt") == 0, "nome s.txt");
    T_ASSERT(lista[0].metodo == 0, "método stored");
    T_ASSERT(lista[0].tam_descomprimido == sizeof S - 1, "tamanho declarado");
    libera_lista(lista, nlista);

    memset(&z, 0, sizeof z);
    T_ASSERT(zip_abre(caminho, &z) == 0, "zip_abre lê stored");
    T_ASSERT(z.n == 1, "uma entrada no arquivo");
    {
        size_t outlen = 0;
        char *buf = zip_extrai(&z, 0, &outlen);
        T_ASSERT(buf != NULL, "extrai stored");
        T_ASSERT(outlen == sizeof S - 1, "stored tem 6 bytes, não 7");
        T_ASSERT(memcmp(buf, S, outlen) == 0, "conteúdo stored");
        free(buf);
    }
    /* Índice fora da faixa é erro, não buffer lido por cima. */
    {
        size_t outlen = 0;
        T_ASSERT(zip_extrai(&z, 99, &outlen) == NULL, "índice fora da faixa");
    }
    zip_fecha(&z);
}

/* O registro de diretório central tem tamanho variável: nome + extra +
 * comentário. Uma leitura por índice fixo (`i * 46`) só dá a posição certa
 * quando os três são vazios, que é o caso do ZIP mínimo e não o caso de um
 * ZIP de verdade. Este teste é o que separa as duas coisas. */
static void teste_nome_variavel(const char *caminho)
{
    static const unsigned char UM[] = "um";
    static const unsigned char DOIS[] = "dois";
    static const unsigned char TRES[] = "tres";
    static const unsigned char QUATRO[] = "quatro";
    struct entrada_zip es[] = {
        { "curto.txt", UM, sizeof UM - 1, 0 },
        { "um-nome-bem-mais-longo-que-o-outro.txt", DOIS, sizeof DOIS - 1, 0 },
        { "médio-accentuado-ñ.csv", TRES, sizeof TRES - 1, 0 },
        { "x.txt", QUATRO, sizeof QUATRO - 1, 0 },
    };
    zip_entrada *lista = NULL;
    size_t nlista = 0;
    zip_arq z;
    size_t i;

    grava_zip(caminho, es, 4);

    T_ASSERT(zip_lista(caminho, &lista, &nlista) == 0, "lê diretório variável");
    T_ASSERT(nlista == 4, "quatro entradas");
    T_ASSERT(lista != NULL, "lista alocada");
    for (i = 0; i < nlista; i++) {
        int esperado = 0;
        size_t j;
        for (j = 0; j < 4; j++)
            if (strcmp(lista[i].nome, es[j].nome) == 0) esperado = 1;
        T_ASSERT(esperado == 1, "cada nome lido bate com um nome escrito");
    }
    libera_lista(lista, nlista);

    /* E extrai cada um pelo nome: se a posição do diretório estivesse errada,
     * a extração traz o conteúdo de outro arquivo. */
    memset(&z, 0, sizeof z);
    T_ASSERT(zip_abre(caminho, &z) == 0, "abre o de nomes variáveis");
    T_ASSERT(z.n == 4, "quatro entradas no arquivo");
    for (i = 0; i < z.n; i++) {
        size_t j, outlen = 0;
        char *buf = zip_extrai(&z, i, &outlen);
        T_ASSERT(buf != NULL, "extrai cada entrada");
        for (j = 0; j < 4; j++) {
            if (strcmp(z.entradas[i].nome, es[j].nome) == 0) {
                T_ASSERT(outlen == es[j].tam, "tamanho do conteúdo certo");
                T_ASSERT(memcmp(buf, es[j].dados, outlen) == 0,
                         "conteúdo do arquivo certo");
            }
        }
        free(buf);
    }
    zip_fecha(&z);
}

static void teste_zip_truncado(const char *caminho)
{
    static const unsigned char A[] = "hello";
    struct entrada_zip es[] = { { "a.txt", A, sizeof A - 1, 1 } };
    FILE *f;

    grava_zip(caminho, es, 1);

    /* Corta pela metade: some o EOCD, e um arquivo sem EOCD não é ZIP. */
    {
        long tamanho;
        f = fopen(caminho, "rb");
        T_ASSERT(f != NULL, "reabre para truncar");
        T_ASSERT(fseek(f, 0, SEEK_END) == 0, "vai ao fim");
        tamanho = ftell(f);
        T_ASSERT(fclose(f) == 0, "fecha antes de truncar");
        T_ASSERT(truncate(caminho, tamanho / 2) == 0, "trunca o ZIP");
    }
    {
        zip_entrada *lista = NULL;
        size_t nlista = 0;
        T_ASSERT(zip_lista(caminho, &lista, &nlista) != 0,
                 "ZIP sem EOCD é recusado");
        T_ASSERT(lista == NULL, "recusa não devolve lista");
    }
}

void test_zip(void)
{
    char tmp[] = "/tmp/teste-zip-XXXXXX";
    char dir[64];
    char deflate_zip[128], stored_zip[128], variavel_zip[128], cortado_zip[128];

    T_ASSERT(mkdtemp(tmp) != NULL, "cria diretório temporário");
    snprintf(dir, sizeof dir, "%s", tmp);
    snprintf(deflate_zip, sizeof deflate_zip, "%s/deflate.zip", dir);
    snprintf(stored_zip, sizeof stored_zip, "%s/stored.zip", dir);
    snprintf(variavel_zip, sizeof variavel_zip, "%s/variavel.zip", dir);
    snprintf(cortado_zip, sizeof cortado_zip, "%s/cortado.zip", dir);

    teste_inexistente();
    teste_deflate(deflate_zip);
    teste_stored(stored_zip);
    teste_nome_variavel(variavel_zip);
    teste_zip_truncado(cortado_zip);

    /* Limpa o que criou. As fixtures são do teste, não de ninguém. */
    remove(deflate_zip);
    remove(stored_zip);
    remove(variavel_zip);
    remove(cortado_zip);
    rmdir(dir);
}
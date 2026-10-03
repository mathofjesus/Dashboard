/* tests/test_cache.c -- Tarefa 2: cache em disco.
 *
 * Os testes rodam no diretório de execução (a raiz do projeto, que é o que o
 * Makefile usa). Cada teste apaga no fim só os arquivos que ele criou, e o
 * teste de criação de diretório roda num diretório temporário para não
 * encostar no data/cache/ de verdade.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/cache.h"

#define T_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    exit(1); } } while (0)

/* Chaves usadas pelos testes (curtas, sem acento no nome de arquivo). */
#define K1 "t1"
#define K2 "t2"
#define K3 "t3"

/* Apaga o arquivo (e o .ttl) de uma chave. */
static void apaga_chave(const char *chave)
{
    char *p = cache_path(chave);
    char *livre;

    if (p) {
        remove(p);
        livre = malloc(strlen(p) + 8);
        if (livre) {
            snprintf(livre, strlen(p) + 8, "%s.ttl", p);
            remove(livre);
            free(livre);
        }
        free(p);
    }
}

static void apaga_tudo(void)
{
    apaga_chave(K1);
    apaga_chave(K2);
    apaga_chave(K3);
    apaga_chave("a/b");
    apaga_chave("../fuga");
    apaga_chave("binaria");
    apaga_chave("chave-muito-longa-que-estoura-o-limite-AAAAAAAAAA");
    apaga_chave("chave-muito-longa-que-estoura-o-limite-BBBBBBBBBBBB");
    apaga_chave("com espaco e & e ?");
}

/* ------------------------------------------------------------------ */
/* caminho                                                             */
/* ------------------------------------------------------------------ */

static void testa_caminho(void)
{
    char *p;
    struct stat st;

    /* Garante que o diretório do cache existe antes de conferir que nenhuma
     * chave cria subdiretório: senão o teste passaria por falta de diretório
     * e não provaria nada. */
    T_ASSERT(cache_grava(K1, "x", 1) == 0, "grava para criar o diretório");

    p = cache_path(K1);
    T_ASSERT(p != NULL, "cache_path de chave válida devolve ponteiro");
    T_ASSERT(strncmp(p, CACHE_DIR "/", strlen(CACHE_DIR) + 1) == 0,
             "o caminho começa no diretório do módulo");
    free(p);

    /* O brief pede: chave com '/' tem que virar nome de arquivo e não pode
     * criar diretório. */
    p = cache_path("a/b");
    T_ASSERT(p != NULL, "chave com barra não devolve NULL");
    T_ASSERT(strcmp(p, CACHE_DIR "/a_b") == 0,
             "a barra vira '_' e nenhum subdiretório é criado");
    T_ASSERT(stat(CACHE_DIR "/a", &st) != 0,
             "o diretório 'a' não foi criado");
    free(p);

    /* Não pode escapar do diretório do cache: é o mesmo defeito que o
     * servidor da Tarefa 1 teve com '..'. */
    p = cache_path("../fuga");
    T_ASSERT(p != NULL, "chave com '..' não devolve NULL");
    T_ASSERT(strncmp(p, CACHE_DIR "/", strlen(CACHE_DIR) + 1) == 0,
             "'..' não tira a chave de dentro do diretório do cache");
    T_ASSERT(strstr(p, "..") == NULL, "não sobra nenhum '..' no caminho");
    T_ASSERT(strlen(p) < 300, "o caminho não estourou o limite do sistema");
    free(p);

    T_ASSERT(cache_path("") == NULL, "chave vazia devolve NULL");
    T_ASSERT(cache_path(NULL) == NULL, "chave NULL devolve NULL");

    p = cache_path("com espaco e & e ?");
    T_ASSERT(p != NULL, "chave com caractere de espaço devolve caminho");
    T_ASSERT(strstr(p, " ") == NULL, "espaço não fica no nome do arquivo");
    free(p);

    apaga_chave(K1);
}

/* ------------------------------------------------------------------ */
/* grava e lê                                                          */
/* ------------------------------------------------------------------ */

static void testa_grava_e_le(void)
{
    char *buf = NULL;
    size_t len = 0;
    char *p = cache_path(K1);

    T_ASSERT(cache_grava(K1, "abc", 3) == 0, "cache_grava devolve 0");
    T_ASSERT(access(p, F_OK) == 0, "o arquivo foi criado");
    free(p);

    T_ASSERT(cache_le(K1, &buf, &len) == 0, "cache_le devolve 0");
    T_ASSERT(len == 3, "cache_le devolve o tamanho gravado");
    T_ASSERT(buf != NULL && memcmp(buf, "abc", 3) == 0,
             "cache_le devolve o conteúdo gravado");
    free(buf);

    /* conteúdo vazio: arquivo de 0 bytes, len == 0 e sem crash */
    T_ASSERT(cache_grava(K2, "", 0) == 0, "gravar 0 bytes devolve 0");
    buf = NULL;
    T_ASSERT(cache_le(K2, &buf, &len) == 0, "ler arquivo vazio devolve 0");
    T_ASSERT(len == 0, "arquivo vazio tem len == 0");
    T_ASSERT(buf != NULL, "arquivo vazio ainda devolve buffer alocado");
    free(buf);

    /* sobrescrever encolhe: o arquivo tem de ter o tamanho novo, e não a
     * sobra do anterior. */
    T_ASSERT(cache_grava(K2, "1234567890", 10) == 0, "gravar por cima");
    T_ASSERT(cache_grava(K2, "xy", 2) == 0, "gravar por cima menor");
    T_ASSERT(cache_le(K2, &buf, &len) == 0, "leitura depois de sobrescrever");
    T_ASSERT(len == 2, "o arquivo encolheu para 2 bytes");
    T_ASSERT(buf != NULL && memcmp(buf, "xy", 2) == 0,
             "conteúdo é o da última gravação");
    free(buf);
}

/* Nada de conversão de texto: o cache vai guardar ZIP e CSV crus. */
static void testa_binario(void)
{
    unsigned char dado[256];
    char *buf = NULL;
    size_t len = 0;
    int i;

    for (i = 0; i < 256; i++)
        dado[i] = (unsigned char)i;     /* inclui 0x00 e 0xFF */

    T_ASSERT(cache_grava("binaria", dado, sizeof dado) == 0,
             "gravar 256 bytes crus");
    T_ASSERT(cache_le("binaria", &buf, &len) == 0, "ler os bytes crus");
    T_ASSERT(len == sizeof dado, "os 256 bytes voltaram");
    T_ASSERT(buf != NULL && memcmp(buf, dado, sizeof dado) == 0,
             "leitura é byte a byte igual ao que foi gravado");
    free(buf);
}

static void testa_inexistente(void)
{
    char *buf = (char *)0x1;            /* sentinela: tem que virar NULL */
    size_t len = 12345;

    T_ASSERT(cache_tem("inexistente") == 0, "cache_tem de chave ausente é 0");
    T_ASSERT(cache_le("inexistente", &buf, &len) != 0,
             "cache_le de chave ausente falha");
    T_ASSERT(buf == NULL, "cache_le de chave ausente devolve NULL");
    T_ASSERT(len == 0, "cache_le de chave ausente devolve len 0");
    T_ASSERT(cache_idade_segundos("inexistente") == -1,
             "idade de chave ausente é -1");
    T_ASSERT(cache_ttl("inexistente") == CACHE_TTL_NENHUM,
             "TTL de chave ausente é -1");
    T_ASSERT(cache_grava(NULL, "x", 1) == -1, "gravar com chave NULL falha");
    T_ASSERT(cache_tem(NULL) == 0, "cache_tem(NULL) é 0");
}

/* ------------------------------------------------------------------ */
/* idade e TTL                                                         */
/* ------------------------------------------------------------------ */

static void testa_idade(void)
{
    T_ASSERT(cache_grava(K1, "abc", 3) == 0, "grava para medir a idade");
    T_ASSERT(cache_idade_segundos(K1) >= 0,
             "idade logo depois de gravar é 0 ou mais");
    T_ASSERT(cache_idade_segundos(K1) < 60,
             "idade logo depois de gravar é pequena");

    /* TTL default é "não expira nunca" */
    T_ASSERT(cache_ttl(K1) == CACHE_TTL_NENHUM,
             "chave nova tem TTL -1 (imutável)");
    T_ASSERT(cache_tem(K1) == 1, "chave sem TTL existe");

    cache_set_ttl(K1, 60);
    T_ASSERT(cache_ttl(K1) == 60, "cache_set_ttl(60) e cache_ttl devolvem 60");
    T_ASSERT(cache_idade_segundos(K1) < 60, "idade ainda dentro do TTL");
    T_ASSERT(cache_tem(K1) == 1, "dentro do TTL o cache vale");

    /* TTL já vencido: o arquivo continua no disco, mas não vale mais */
    cache_set_ttl(K1, 0);
    T_ASSERT(cache_ttl(K1) == 0, "TTL 0 é aceito");
    sleep(1);
    T_ASSERT(cache_idade_segundos(K1) >= 1, "esperou 1 s para o TTL vencer");
    T_ASSERT(cache_tem(K1) == 0, "TTL vencido: cache_tem é 0");
    {
        char *buf = NULL;
        size_t len = 0;
        /* o conteúdo continua legível, só não é mais válido */
        T_ASSERT(cache_le(K1, &buf, &len) == 0, "o arquivo continua no disco");
        T_ASSERT(len == 3, "e com o conteúdo inteiro");
        free(buf);
    }

    /* limpar o TTL volta para "imutável" */
    cache_set_ttl(K1, CACHE_TTL_NENHUM);
    T_ASSERT(cache_ttl(K1) == CACHE_TTL_NENHUM, "TTL volta para -1");
    T_ASSERT(cache_tem(K1) == 1, "sem TTL o cache vale de novo");

    /* TTL negativo diferente de -1 também é "não expira": o arquivo de lado
     * some em vez de ficar com um prazo que ninguém entende. */
    cache_set_ttl(K2, 30);
    T_ASSERT(cache_ttl(K2) == 30, "TTL do K1 e do K2 são independentes");
    cache_set_ttl(K2, -99);
    T_ASSERT(cache_ttl(K2) == CACHE_TTL_NENHUM, "TTL negativo é -1");
    T_ASSERT(cache_tem(K2) == 1, "e sem TTL o K2 (que existe) vale");
    T_ASSERT(cache_ttl(K1) == CACHE_TTL_NENHUM,
             "mexer no TTL do K2 não mexeu no do K1");
}

/* Escreve bytes crus no arquivo de lado do TTL, para simular uma escrita
 * interrompida no meio (o arquivo existe, o prazo não está inteiro
 * dentro). cache_set_ttl() nunca produz isso, mas um disco cheio, um
 * Energy Loss Configuration ou um cache copiado pela metade produzem. */
static int escreve_ttl(const char *ttl_path, const char *conteudo, size_t len)
{
    FILE *f = fopen(ttl_path, "wb");
    size_t k;

    if (!f)
        return -1;
    k = fwrite(conteudo, 1, len, f);
    fclose(f);
    return k == len ? 0 : -1;
}

/* Um .ttl ilegível não pode virar "não expira nunca". Seria servir dado
 * velho indefinidamente, sem erro e sem log — que é exatamente o que o TTL
 * existe para evitar. São dois comportamentos diferentes e os dois ficam
 * fixados aqui: */
static void testa_ttl_ilegivel(void)
{
    char *p = cache_path(K1);
    char *ttl;
    size_t n, len = 0;
    char *buf = NULL;

    T_ASSERT(p != NULL, "caminho da chave");
    n = strlen(p);
    ttl = malloc(n + 8);
    T_ASSERT(ttl != NULL, "memória para o caminho do .ttl");
    snprintf(ttl, n + 8, "%s.ttl", p);
    free(p);

    T_ASSERT(cache_grava(K1, "abc", 3) == 0, "grava a chave");

    /* 1. sem arquivo de lado: imutável, como o header promete */
    T_ASSERT(cache_ttl(K1) == CACHE_TTL_NENHUM,
             "AUSÊNCIA de .ttl é CACHE_TTL_NENHUM (imutável)");
    T_ASSERT(cache_tem(K1) == 1, "e a chave vale");

    /* 2. .ttl de 0 bytes: cache_set_ttl interrompido antes do fprintf */
    T_ASSERT(escreve_ttl(ttl, "", 0) == 0, ".ttl de 0 bytes");
    T_ASSERT(cache_ttl(K1) == CACHE_TTL_CORROMPIDO,
             ".ttl vazio é corrompido, não imutável");
    T_ASSERT(cache_tem(K1) == 0, ".ttl vazio expira a chave");

    /* 3. .ttl com lixo no lugar do número */
    T_ASSERT(escreve_ttl(ttl, "86400 segundos\n", 15) == 0, ".ttl não numérico");
    T_ASSERT(cache_ttl(K1) == CACHE_TTL_CORROMPIDO,
             "lixo no .ttl é corrompido");
    T_ASSERT(cache_tem(K1) == 0, ".ttl não numérico expira a chave");

    /* 4. número válido: a chave volta a valer */
    T_ASSERT(escreve_ttl(ttl, "86400\n", 6) == 0, ".ttl válido");
    T_ASSERT(cache_ttl(K1) == 86400, "86400 é lido como está");
    T_ASSERT(cache_tem(K1) == 1, "e a chave vale de novo");

    /* 5. número truncado: legível, logo é um prazo — menor, mas prazo.
     * Não é corrupção, é um prazo que o cache_set_ttl interrupted deixou. */
    T_ASSERT(escreve_ttl(ttl, "864", 3) == 0, ".ttl truncado para 864");
    T_ASSERT(cache_ttl(K1) == 864, "o número que está no arquivo é o prazo");
    T_ASSERT(cache_tem(K1) == 1, "864 s à frente ainda não venceu");

    /* 6. prazo negativo escrito por fora: ninguém entendeu esse prazo, então
     * não pode virar "imutável" */
    T_ASSERT(escreve_ttl(ttl, "-5\n", 3) == 0, ".ttl negativo");
    T_ASSERT(cache_ttl(K1) == CACHE_TTL_CORROMPIDO,
             "prazo negativo em disco é corrompido");
    T_ASSERT(cache_tem(K1) == 0, "e a chave expira");

    /* 7. lixo DEPOIS do número: o fscanf para no número e ignora o resto, o
     * que devolveria um prazo inventado a partir de um arquivo quebrado */
    T_ASSERT(escreve_ttl(ttl, "86400\nlixo\n", 11) == 0, ".ttl com lixo no fim");
    T_ASSERT(cache_ttl(K1) == CACHE_TTL_CORROMPIDO,
             "lixo depois do número também é corrompido");
    T_ASSERT(cache_tem(K1) == 0, "e a chave expira");

    /* o conteúdo continua no disco e legível: expirado não é apagado */
    T_ASSERT(cache_le(K1, &buf, &len) == 0, "conteúdo continua legível");
    T_ASSERT(len == 3, "e com os 3 bytes de sempre");
    free(buf);
    T_ASSERT(access(ttl, F_OK) == 0, "o .ttl corrompido não foi apagado");

    free(ttl);
}

/* ------------------------------------------------------------------ */
/* criação do diretório                                                */
/* ------------------------------------------------------------------ */

static void testa_cria_diretorio(void)
{
    char *anterior = getcwd(NULL, 0);
    char base[128], sub[256];
    char *buf = NULL;
    size_t len = 0;
    struct stat st;

    T_ASSERT(anterior != NULL, "getcwd falhou");
    /* Diretório novo, com o pid no nome, para não precisar de rm -rf: é
     * criado agora e removido no fim com rmdir. */
    snprintf(base, sizeof base, "/tmp/opencode/cache_fresh_%ld", (long)getpid());
    T_ASSERT(mkdir(base, 0777) == 0, "não consegui criar o diretório temporário");
    if (chdir(base) != 0)
        T_ASSERT(0, strerror(errno));
    snprintf(sub, sizeof sub, "%s/%s", base, CACHE_DIR);

    T_ASSERT(stat(CACHE_DIR, &st) != 0, "data/cache/ não existe ainda");
    T_ASSERT(cache_tem(K3) == 0, "cache_tem de chave ausente é 0");

    if (cache_grava(K3, "conteúdo com acento\n", 20) != 0)
        T_ASSERT(0, strerror(errno));
    T_ASSERT(stat(CACHE_DIR, &st) == 0, "data/cache/ foi criado");
    T_ASSERT(S_ISDIR(st.st_mode), "data/cache/ é um diretório");
    T_ASSERT(cache_le(K3, &buf, &len) == 0, "leitura dentro do dir novo");
    T_ASSERT(len == 20, "tamanho certo");
    T_ASSERT(buf != NULL && memcmp(buf, "conteúdo com acento\n", 20) == 0,
             "conteúdo com acento intacto");
    free(buf);
    apaga_chave(K3);

    T_ASSERT(chdir(anterior) == 0, "não consegui voltar ao diretório de origem");
    free(anterior);
    T_ASSERT(rmdir(sub) == 0, "consegui remover o data/cache/ temporário");
    {
        char pai[sizeof base + 8];
        snprintf(pai, sizeof pai, "%s/data", base);
        T_ASSERT(rmdir(pai) == 0, "consegui remover o data/ temporário");
    }
    T_ASSERT(rmdir(base) == 0, "consegui remover o diretório temporário");
}

/* ------------------------------------------------------------------ */
/* chave longa                                                         */
/* ------------------------------------------------------------------ */

/* O nome do arquivo tem teto: duas chaves longas que só diferem no fim têm de
 * virar arquivos diferentes, senão uma leitura pega o download da outra. */
static void testa_chave_longa(void)
{
    const char *k1 = "chave-muito-longa-que-estoura-o-limite-AAAAAAAAAA";
    const char *k2 = "chave-muito-longa-que-estoura-o-limite-BBBBBBBBBBBB";
    char *p1 = cache_path(k1);
    char *p2 = cache_path(k2);

    T_ASSERT(p1 != NULL && p2 != NULL, "chave longa devolve caminho");
    T_ASSERT(strlen(p1) < 300, "o nome foi encurtado");
    T_ASSERT(strcmp(p1, p2) != 0, "duas chaves longas viram nomes diferentes");

    T_ASSERT(cache_grava(k1, "um", 2) == 0, "grava a primeira");
    T_ASSERT(cache_grava(k2, "dois", 4) == 0, "grava a segunda");
    T_ASSERT(cache_tem(k1) == 1 && cache_tem(k2) == 1, "as duas existem");
    {
        char *buf = NULL;
        size_t len = 0;
        T_ASSERT(cache_le(k1, &buf, &len) == 0, "lê a primeira");
        T_ASSERT(len == 2 && memcmp(buf, "um", 2) == 0, "conteúdo da primeira");
        free(buf);
        T_ASSERT(cache_le(k2, &buf, &len) == 0, "lê a segunda");
        T_ASSERT(len == 4 && memcmp(buf, "dois", 4) == 0, "conteúdo da segunda");
        free(buf);
    }
    free(p1);
    free(p2);
}

/* ------------------------------------------------------------------ */

void test_cache(void)
{
    apaga_tudo();

    printf("testa_caminho\n");             testa_caminho();
    printf("testa_grava_e_le\n");          testa_grava_e_le();
    printf("testa_binario\n");             testa_binario();
    printf("testa_inexistente\n");         testa_inexistente();
    printf("testa_idade\n");               testa_idade();
    printf("testa_ttl_ilegivel\n");        testa_ttl_ilegivel();
    printf("testa_cria_diretorio\n");      testa_cria_diretorio();
    printf("testa_chave_longa\n");         testa_chave_longa();

    apaga_tudo();
}

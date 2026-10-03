/* cache.h -- cache dos downloads brutos em data/cache/.
 *
 * Um arquivo por chave. O nome do arquivo é a chave com todo caractere que
 * não for [A-Za-z0-9_-] trocado por '_', de modo que uma URL (que tem '/',
 * '?', '&', ':' e '%') vire nome de arquivo válido sem criar diretório nenhum
 * e sem conseguir escapar de data/cache/ — o ponto também é trocado, então
 * '..' não sobrevive.
 *
 * O TTL fica num arquivo de lado, "<arquivo>.ttl", com o prazo em segundos.
 * Ele não pode ser um cabeçalho no próprio arquivo: o conteúdo é o download
 * cru e a Tarefa 4/5 vai ler ZIP e CSV a partir dele byte a byte.
 *
 * Contrato de memória: cache_path() e cache_le() devolvem ponteiro de malloc;
 * quem chama libera com free(). As demais não alocam.
 */
#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>

/* Diretório base, relativo ao diretório de execução do processo (é o
 * raiz do projeto quando o binário é rodado de lá). Exposto num lugar só,
 * porque várias funções precisam dele. */
#define CACHE_DIR "data/cache"

/* cache_ttl(): valor que significa "não expira nunca". Uma fonte imutável
 * (a lista de municípios do IBGE, por exemplo) usa esse valor; o padrão de
 * uma chave nova também é esse. */
#define CACHE_TTL_NENHUM (-1)

/* cache_ttl(): valor que significa "o arquivo .ttl existe, mas não tem um
 * prazo legível dentro". Não é a mesma coisa que CACHE_TTL_NENHUM e a
 * diferença é de propósito: a ausência de .ttl é imutável, que é o padrão
 * do contrato, mas um .ttl quebrado (escrita interrompida no meio, disco
 * cheio, cache copiado pela metade) seria fail-open — a chave valeria para
 * sempre, servindo dado velho sem erro e sem log, que é o oposto do que o
 * TTL existe para fazer. Então a chave é tratada como expirada. */
#define CACHE_TTL_CORROMPIDO (-2)

/* Caminho do arquivo da chave, malloc'd, ou NULL se a chave for inválida
 * (NULL ou vazia) ou faltar memória. Não cria o diretório nem o arquivo. */
char *cache_path(const char *chave);

/* 1 se o arquivo existe e ainda está dentro do TTL; 0 se não existe ou se
 * expirou. Isto é o que a ingestão (T5/T8) vai usar para decidir entre ler o
 * cache e baixar de novo, então o TTL é respeitado aqui de propósito. Para
 * só saber se o arquivo existe, use cache_path() + access(). */
int cache_tem(const char *chave);

/* Grava len bytes crus em data/cache/, criando o diretório se faltar.
 * Devolve 0 ou -1. */
int cache_grava(const char *chave, const void *buf, size_t len);

/* Lê o arquivo inteiro. Devolve 0 com *out malloc'd (e '\0' no fim) e
 * *out_len com o tamanho, ou -1 se não der para ler. */
int cache_le(const char *chave, char **out, size_t *out_len);

/* Segundos desde a última gravação, ou -1 se o arquivo não existe. */
int cache_idade_segundos(const char *chave);

/* Prazo em segundos da chave, CACHE_TTL_NENHUM se não houver prazo (o
 * arquivo de lado não existe), ou CACHE_TTL_CORROMPIDO se ele existir mas
 * não der para ler um prazo de dentro — nesse caso a chave está expirada. */
int cache_ttl(const char *chave);

/* Define (ou limpa, com CACHE_TTL_NENHUM) o prazo da chave. Não cria o
 * diretório; o TTL só é lido junto com o arquivo, em cache_ttl(). */
void cache_set_ttl(const char *chave, int segundos);

#endif /* CACHE_H */

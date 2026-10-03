/* http_client.h -- download de arquivo pela CLI do curl.
 *
 * Não há libcurl neste projeto (§4.1 da spec): HTTPS só existe invocando o
 * binário curl. Este módulo monta a chamada, executa, lê o corpo inteiro e
 * devolve os bytes crus.
 *
 * Por que não system() e por que a URL vai entre aspas simples:
 *   - system() passa a linha por /bin/sh. A URL do IBGE tem query string com
 *     '?' e '&'; sem aspas o shell corta o comando no '&', o curl baixa só a
 *     metade da URL e o resto vira um comando separado (que pode ter efeito
 *     colateral). Dentro de aspas simples o shell não expande nada: nem &, nem
 *     ?, nem $, nem crase.
 *   - dentro de aspas simples a única coisa que precisa escapar é a própria
 *     aspa simples, virada em '\'' (fecha, aspa escapada, aspa). Não há
 *     arquivo temporário nem --config, que daria duas pontas de Injection
 *     possíveis em vez de uma.
 *
 * Nada de conversão de texto no caminho: o corpo é lido com fread e devolvo
 * byte a byte, porque as respostas são ZIP e CSV binários (o CNES são 231 MB
 * comprimidos, em tarefa futura).
 */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

/* Tempo total que o curl espera pela resposta, em segundos. */
#define HTTP_CLIENT_MAX_TIME_S 120

/* Tamanho inicial da leitura, só para não ficar devolvendo bloco de 4 KiB
 * num arquivo de centenas de MB. */
#define HTTP_CLIENT_LIMITE_BYTES (512u * 1024u * 1024u)   /* 512 MB */

/* Baixa a url e devolve o corpo em *out (malloc, com '\0' depois do último
 * byte para facilitar o uso como string) e o tamanho em *out_len.
 *
 * Devolve:
 *    0  deu certo;
 *   -1  erro local: url NULL, url com caractere de controle, falta de memória,
 *       ou o popen() não abriu (curl ausente do PATH);
 *   >0  o código de saída do curl (6 = DNS, 7 = não conectou, 22 = HTTP >= 400
 *       com --fail, 28 = estourou --max-time), que é o que o chamador precisa
 *       para distinguir "não tem rede" de "o servidor respondeu 404".
 *
 * Em qualquer erro, *out fica NULL e *out_len vale 0. */
int http_get(const char *url, char **out, size_t *out_len);

#endif /* HTTP_CLIENT_H */

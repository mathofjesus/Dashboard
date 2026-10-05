/* dataset_saude.h — o processamento dos CSVs do Ministério da Saúde.
 *
 * Por que isto é uma etapa separada do modelo
 * ---------------------------------------------
 * A população do IBGE cabe em memória: 5570 municípios × 22 anos × 4 bytes dá
 * menos de 1 MB, e a spec §3 manda manter isso em memória. O CNES não cabe, e
 * não por pouco: são ~630 mil estabelecimentos, e o CSV completo tem mais de
 * 130 colunas das quais o dashboard usa seis. Guardar o CSV cru em memória
 * custaria centenas de megabytes, e guardá-lo em disco sem processar empurraria
 * para o navegador um texto de tamanho desconhecido.
 *
 * A saída é, portanto, um arquivo podado: seis colunas, uma linha por
 * estabelecimento, em JSON Lines. O navegador o busca por Range requests e
 * guarda os campos numéricos em typed arrays. O texto bruto não vai para o
 * navegador — é o que a spec §5 pede ("cache no navegador permitido para
 * CNES, buscado na primeira consulta") e o que o Passo 9 do plano detalha.
 *
 * Formato da saída
 * ----------------
 * JSON Lines, e não um array: um documento de 70 MB que o `JSON.parse` do
 * navegador precisa inteiro em memória antes de devolver o primeiro byte. Em
 * JSON Lines o navegador lê linha a linha, e o Range request entrega a próxima
 * fatia sem nada ser reanalisado.
 *
 *   linha 1  = cabeçalho: {"formato":"cnes","colunas":[...],"linhas":N}
 *   linha 2+ = um objeto por estabelecimento, na ordem do CSV
 *
 * A primeira linha é cabeçalho de verdade e não um registro: quem lê sabe o
 * que esperar antes de ver o primeiro dado, e uma mudança de conjunto de
 * de colunas é detectada em vez de produzir valores silenciosamente trocados.
 *
 * Ausência é -1
 * --------------
 * Um município que não veio, ou que veio como "..", é -1. Nunca 0: 0
 * estabelecimentos é um dado possível, e "não sei" virar 0 contaminaria a soma
 * e a média de quem soma os dois. É a mesma regra do modelo (model.h) e a mesma
 * exigência da spec §4.
 *
 * Codificação
 * -----------
 * CNES e Leitos são ISO-8859-1; Dengue é UTF-8. A transcodificação é por
 * dataset e acontece na leitura, campo a campo — antes de escrever qualquer
 * byte. Um "POLICÍNICA" em latin-1 que escapa sem converter chega ao
 * navegador como byte quebrado com HTTP 200, que é o modo de falha que a
 * spec §9 chama de pior.
 */
#ifndef DATASET_SAUDE_H
#define DATASET_SAUDE_H

#include <stddef.h>

/* Onde a saída processada é gravada. Relativo ao diretório de execução, como
 * o resto de data/ — o binário roda a partir da raiz do projeto. */
#define SAUDE_DIR "data/processed"

/* O arquivo do CNES, servido por Range em /api/cnes. */
#define CNES_JSON "data/processed/cnes.json"

/* O arquivo de Leitos e Dengue, agregados por município. */
#define LEITOS_JSON "data/processed/leitos.json"
#define DENGUE_JSON "data/processed/dengue.json"

/* Colunas do CNES que sobrevivem à poda, NESTA ordem.
 *
 * A ordem é fixa e não negociável: o navegador lê por posição, e trocar a
 * ordem trocaria latitude por longitude sem erro nenhum. `CO_IBGE` vem
 * primeiro porque é a chave de junção com o modelo e com a malha — a spec §8
 * diz que o `codarea` da malha e o `CO_IBGE` do CSV são a mesma chave, e é
 * isso que faz o coroplético fechar sem gambiarra. */
extern const char *const SAUDE_COLUNAS_CNES[];
extern const size_t SAUDE_N_COLUNAS_CNES;

/* Colunas de Leitos, também em ordem fixa. */
extern const char *const SAUDE_COLUNAS_LEITOS[];
extern const size_t SAUDE_N_COLUNAS_LEITOS;

/* Colunas de Dengue. */
extern const char *const SAUDE_COLUNAS_DENGUE[];
extern const size_t SAUDE_N_COLUNAS_DENGUE;

/* ------------------------------------------------------------------ */
/* CNES                                                                */
/* ------------------------------------------------------------------ */

/*
 * Abre o ZIP do CNES, extrai o CSV de dentro, poda para as seis colunas e
 * grava o JSON Lines em `saida`.
 *
 * `zip_local` é o caminho do ZIP já baixado (data/cache/cnes) e `saida` o do
 * JSON. Ambos explícitos para que o teste não dependa do diretório de execução
 * — a mesma razão de `model_carrega_de` existir.
 *
 * `limite_amostra` > 0 para de ler depois de N linhas de dados. É o que
 * `--atualizar cnes --limite-amostra 1000` usa: 1001 linhas num arquivo de
 * 630 mil provam o caminho completo sem baixar os 56 MB.
 *
 * Devolve 0 em sucesso, ou != 0 com mensagem já impressa em português e stderr
 * (a spec exige que falha de ingestão saia com código != 0 e mensagem legível).
 */
int saude_processa_cnes(const char *zip_local, const char *saida,
                        long limite_amostra);

/* ------------------------------------------------------------------ */
/* Leitos e Dengue                                                     */
/* ------------------------------------------------------------------ */

/*
 * Agrega o CSV de Leitos por município.
 *
 * `LEITOS_SUS` e `UTI_TOTAL_SUS` são somados por `CO_IBGE`, e o resultado
 * vira uma linha por município no mesmo formato do CNES. Somar por município e
 * não por estabelecimento é o que mantém o total verdadeiro: o dashboard soma
 * as linhas que tem, e somaroice por estabelecimento daria um número maior
 * que a soma de uma parte dele.
 *
 * Campo vazio e ".." não entram na soma: contam como zero de beds, que é o
 * que o CNES usa para "não informado", e somar "-1" numa soma produz um total
 * negativo que ninguém sabe explicar.
 */
int saude_processa_leitos(const char *zip_local, const char *saida,
                          long limite_amostra);

/*
 * Conta os casos de dengue por município e por semana.
 *
 * Dengue é UTF-8 (ver a restrição global do plano) e a chave é `ID_MUNICIP`,
 * não `CO_IBGE` — o SINAN identifica município pelo código do IBGE sem o
 * prefixo de dígito verificador, e as duas chaves precisam ser reconciliadas
 * antes da junção.
 */
int saude_processa_dengue(const char *csv_local, const char *saida,
                          long limite_amostra);

#endif /* DATASET_SAUDE_H */
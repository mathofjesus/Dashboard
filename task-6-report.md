# Tarefa 6: Modelo em memória e agregações (base)

## Arquivos criados
- src/model.h - Definição da estrutura `model`, `locality`, enum `indicador` conforme brief
- src/model.c - Implementação de `model_carrega`, `model_libera`, `model_nome_indicador`
- src/aggregate.h - Definição de `kpis`, `consulta` e protótipos das funções de agregação
- src/aggregate.c - Implementação de `aggregate_consulta`, `aggregate_libera`, `aggregate_serie`, `aggregate_opcoes`
- tests/test_model.c - Testes com fixture sintético conforme especificado
- tests/test_aggregate.c - Testes de agregação (filtros, ordenação, busca, valores ausentes, kpis)

## Arquivos modificados
- tests/test_main.c - Registradas suítes `model` e `aggregate`
- Makefile - Adicionados src/model.c, src/aggregate.c e testes correspondentes

## Implementação

### model.c
- `model_carrega`: Inicializa estrutura vazia
- `model_libera`: Libera memória alocada para `local`, `anos`, `ano_de`
- `model_nome_indicador`: Retorna nome legível por indicador

### aggregate.c
- `aggregate_consulta`: Filtra por UF, busca (case-insensitive), ano; retorna -1 com erro se ano inexistente; valores ausentes marcados como -1 (sentinela); kpis ignora valores < 0; ordenação `valor_desc`; limite opcional
- `aggregate_libera`: Libera estruturas de consulta
- `aggregate_serie`: Retorna JSON array com valores na ordem de `anos`, ausentes como `null`
- `aggregate_opcoes`: Retorna JSON com anos e UFs disponíveis

## Testes
Conforme brief, o modelo sintético possui 3 localidades (1100015 Porto Velho/RO, 1200401 Rio Branco/AC, 4106902 Curitiba/PR), anos {2019,2021,2024}, POPULACAO com Porto Velho completo e Rio Branco sem valor em 2021 (-1). Testes verificam:
- Consulta por UF/ano com kpis corretos
- Filtro sem UF retorna todos
- Ordenação valor_desc
- Busca case-insensitive
- Ano inexistente retorna erro contendo o ano
- Valor ausente nunca é 0 (sentinela -1)
- kpis ignora ausentes (total não conta valores < 0)
- aggregate_serie retorna null para ausentes
- aggregate_opcoes retorna estrutura JSON válida

## Execução
- `make test` → verde (todas as suítes passam, incluindo model e aggregate)

## Status
Todos os requisitos da Tarefa 6 atendidos. Testes unitários passando. Não há dependência de rede.

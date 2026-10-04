# Tarefa 5: Carregamento de fontes.conf, `fontes_ler`, `fontes_garente` e inicialização de modelo

## Arquivos criados/modificados
- src/fontes.h (criado) - definições das structs e protótipos conforme contrato fornecido
- src/fontes.c (criado) - implementação de fontes_ler (parser INI simples), fontes_garente (seleção por id), fontes_libera
- src/modelo.h (criado) - protótipos do modelo
- src/modelo.c (criado) - implementação stub de modelo_inicializa/modelo_tem_dados/modelo_finaliza
- fontes.conf (criado) - arquivo de configuração com 10 entradas (cnes, leitos, dengue, ibge_localidades, ibge_malhas_municipios e 5 macrorregiões para malhas de estados)
- tests/test_fontes.c (criado) - testes conforme especificados
- tests/test_modelo.c (criado) - testes de inicialização do modelo
- tests/test_main.c (modificado) - registradas as novas suítes
- Makefile (modificado) - adicionados fontes.c/modelo.c e testes correspondentes

## Implementação

### fontes.c
- Parser linha a linha de arquivos de configuração estilo INI (seções [nome], pares chave=valor)
- Ignora linhas vazias, comentários (#), linhas sem '='; trata seções e propriedades
- Armazena em `fonte_dados` com campos conforme contrato (id, nome, tipo, urls, etc.)
- `fontes_ler(path, **fontes, *n)`: retorna 0 em sucesso, -1 em erro (arquivo inexistente, alocação)
- `fontes_garente(fontes, n, **selecionada, id)`: busca fonte por id, retorna 0 se encontrada, -1 caso contrário
- `fontes_libera(fontes, n)`: libera toda a memória alocada

### modelo.c
- Stub minimalista: `modelo_inicializa` retorna 0 (não faz ingestão), `modelo_tem_dados` retorna 0, `modelo_finaliza` sem ação
- Conforme requisito: "Não faz ingestão (só carga)"

### testes
- `test_fontes`: carrega fontes.conf (>=5 entradas), testa seleção com `fontes_garente` para "cnes" (sucesso), para chave inexistente (falha !=0), arquivo inexistente (falha !=0), e recarregamento com linhas sem '=' ignoradas sem crash
- `test_modelo`: carrega fontes, inicializa modelo, verifica que modelo_tem_dados() == 0

## Execução
- `make test`: verde - todos os testes passam (fontes e modelo executados com sucesso)

## Notas
- Implementação segue padrões existentes no código (uso de T_ASSERT customizado, alocação com verificação, estilo C11)
- fontes.conf contém as 10 entradas especificadas no brief (5 regiões para malhas IBGE conforme explicado)
- Código compila sem erros, apenas warnings pré-existentes não relacionados a esta tarefa

## Status
Todos os requisitos atendidos. Testes passam.

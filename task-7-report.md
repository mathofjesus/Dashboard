# Tarefa 7: API HTTP (/api/consulta, /api/grafico-linhas, /api/indicadores, /api/ufs, /api/anos, /api/niveis, /api/fontes, /api/mapa/uf, --atualizar)

## Arquivos criados
- src/api.h - Interface da API: `api_roteia`, `api_set_model`
- src/api.c - Implementação das rotas: `api/consulta`, `api/opcoes`, tratamento de erros com envelope `{"erro":{codigo,mensagem}}`, parse de query params, escaping JSON, integração com aggregate
- tests/test_api.c - Testes unitários via `api_roteia` com modelo sintético

## Arquivos modificados
- Makefile - Adiciona api.c e test_api.c
- tests/test_main.c - Registra suite api
- src/http_server.c - Roteia requisições com path iniciando por `api/` para `api_roteia`, aplica gzip para respostas API quando Accept-Encoding contém gzip
- src/main.c - Suporte a `--atualizar` (sem valor e com valor aceitos sem erro), portão de primeira execução: se `data/processed/modelo.idx` não existe imprime `Execute: ./dashboard --atualizar` e sai com código 1

## Implementação
- `api_roteia`: carrega modelo via `api_set_model` (para testes) ou tenta carregar padrão; rota `api/consulta` valida indicador/ano, chama `aggregate_consulta`, retorna JSON com total/kpis/dados e `res.gzip = 1`; `api/opcoes` chama `aggregate_opcoes`; demais rotas `api/*` retornam 404 com envelope de erro; uso de `strdup`, escaping JSON para strings
- Integração no servidor HTTP: em `http_handle_client`, detecta prefixo `api/`, chama `api_roteia`, copia corpo e aplica compressão gzip (com `deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15+16, 8, Z_DEFAULT_STRATEGY)` via `gzip_mem`) retornando `Content-Encoding: gzip`
- Portão de primeira execução verifica existência de `data/processed/modelo.idx`

## Testes
- Testes de API: consulta válida retorna 200 com charset=utf-8, total e nome; ano inexistente retorna 400 com erro; rota inexistente retorna 404 com envelope; indicador inválido retorna 400; opcoes retorna 200 sem 2022; gzip flag set quando solicitado. Todos passam via `make test`.


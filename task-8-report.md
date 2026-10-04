# Tarefa 8: Geometria (malhas IBGE) e mapa

## Resumo em uma linha

As malhas do IBGE são lidas do cache, mescladas (5 coleções regionais num
GeoJSON só), simplificadas com Douglas-Peucker e reescritas em coordenadas
inteiras de graus×1000; servidas por `/api/geo/{estados,municipios}` e
`/api/mapa/uf`, com teste sem rede.

## Arquivos criados
- `src/geo.h` — contrato: enum `geo_nivel`, `geo_prepara`, `geo_saida`,
  `geo_saida_filtrada`, `geo_codigo_uf`, `TOLERANCIA_SIMPLIFICACAO`
- `src/geo.c` — pipeline completo (cache → parse → merge → simplifica → serializa → grava)
- `tests/test_geo.c` — 8 casos, sem rede e sem dado real

## Arquivos modificados
- `Makefile` — `src/geo.c` em `SRC`, `tests/test_geo.c` em `TEST_SRC`, `src/geo.h` em `TEST_HDR`
- `tests/test_main.c` — registra a suíte `geo` (uma vez só: protótipo linha 35, `cabecalho`/`test_geo` linhas 89-90)
- `src/api.c` — rotas `api/geo/estados`, `api/geo/municipios`, `api/mapa/uf`
- `src/main.c` — `--geo-info` (Passo 6) e `--atualizar` passou a chamar `geo_prepara` de verdade

## Dado verificado ao vivo (não de memória)

Formato real de `https://servicodados.ibge.gov.br/api/v2/malhas/BR?resolucao=5&qualidade=minima&formato=application/vnd.geo+json`:

| fato | valor medido |
|---|---|
| bytes crus | 5.781.544 |
| features | 5570 |
| `properties` | exatamente `codarea` (string) e `centroide` ([lon, lat]) |
| geometria | 2797 `Polygon` + 2773 `MultiPolygon` |
| anéis | 6185 (máx. 888 pontos, média 40,8) |
| posições | 252.160 |

**O parser foi confirmado, não assumido.** `src/json.c` da Tarefa 2 parseia os
5,78 MB em **0,07 s** com pico de **87,3 MB** de RSS — bem abaixo do teto de
4.000.000 nós. Por isso o pipeline processa uma fonte por vez e libera o parse
antes do seguinte, em vez de segurar tudo: o pico não é a soma das cinco
coleções regionais.

Estados: 5 chamadas regionais (`malhas/{1..5}`) com 7+9+4+3+4 = **27** features.

## Junção `codarea` ↔ modelo

Cruzei os 5570 `codarea` da malha contra os 5571 municípios de
`api/v1/localidades/municipios` (a fonte de localidades do modelo):

```
codarea SEM contrapartida no modelo:   0
cobertura do lado da malha:            100,0000%
modelo SEM malha:                      1  → 5101837
UF no modelo: 27   malhas de estado: 27   faltando: 0
```

O buraco de 5571 vs 5570 da spec é **um município só**: `5101837` =
**Boa Esperança do Norte (MT)**, criado por plebiscite depois de a coleção de
malhas ser cortada. Não é bug de join, é o IBGE ainda não ter publicado o
polígono. Nenhum `codarea` da malha fica sem par no modelo, que é a direção que
quebraria o coroplético (uma feature sem valor para pintar).

## Medição do Passo 6

`./dashboard --atualizar` com a malha real em cache:

```
tolerância da simplificação: 0.05 graus
coordenadas: graus×1000 (inteiras)
estados:          27 features,     81194 bytes processados
municípios:    5570 features,   1485243 bytes processados
```

### O teto de 1.200.000 bytes não é alcançável — e a tabela mostra por quê

Varredura da tolerância na malha real:

| tol | bytes crus | gzip | vertices |
|---|---|---|---|
| 0,02 | 2.086.544 | 467.041 | 85.903 |
| 0,03 | 1.749.826 | 365.906 | 64.344 |
| **0,05** | **1.485.243** | **286.450** | **47.384** ← escolhido |
| 0,07 | 1.371.672 | 252.482 | 40.099 |
| 0,10 | 1.285.199 | 226.488 | 34.548 |
| 0,15 | 1.215.928 | 205.088 | 30.103 |

**796 KB dos 1.485.243 (54%) são andaime de JSON repetido 5570 vezes** —
`{"type":"Feature","properties":{…},"geometry":{…}}`, ~143 bytes por feature —
e nenhuma simplificação de geometria toca nisso. O piso do arquivo é ~800 KB
mesmo com geometria de tamanho zero. Chegar a 1,2 MB exigiria tol ≈ 0,16°, ou
seja, deslocar litoral e limite interno por ~17 km para economizar 285 KB que já
saem comprimidos.

O que realmente atravessa a rede é a coluna gzip: **286.450 bytes** a 0,05°, ou
seja 4,2× abaixo do teto, porque `http_server.c` já comprime e o navegador
guarda em cache. Medido no servidor rodando: `Content-Encoding: gzip`,
290.636 bytes baixados para 1.485.243 no disco, e o `gunzip` do que foi servido
é byte a byte igual ao arquivo.

**Decisão:** mantive o valor que a spec prescreve (0,05) e o contrato de
coordenadas (×1000), e reporto o estouro em vez de distorcer a geometria para
caber num número que foi medido no arquivo em disco. A tabela está em
`src/geo.h` ao lado da constante, para quem vier ajustar.

### Invariantes da geometria (medidos na saída real, parse de JSON independente)

| | estados | municípios |
|---|---|---|
| JSON válido | sim | sim |
| features | 27 | 5570 |
| `codarea` únicos | 27 | 5570 |
| anéis | 625 | 6185 (= entrada, **nenhum anel perdido**) |
| vértices | 4.828 | 47.384 (de 252.160, −81%) |
| menor anel | 4 pontos | 4 pontos |
| anéis não fechados | 0 | 0 |
| coordenadas não inteiras | 0 | 0 |
| propriedades | `codarea`, `centroide` | `codarea`, `centroide` |

## Dois bugs reais que os testes do brief não pegavam

**1. `]}` no lugar de `]}}`.** `escreve_feature` fechava com uma chave a menos.
Os testes do brief (prefixo do texto, `strstr("codarea")`, contagem de
features) **passavam com o JSON quebrado**. Só notei rodando `json.load` na
saída real. Escrevi `confere_geojson()` em `tests/test_geo.c`, que devolve a
saída ao `json_parse` do próprio projeto e confere campo a campo — inclusive o
aninhamento `Polygon` vs `MultiPolygon`, onde erro de colchete se esconde.
Confirmei que pega: reintroduzi o `]}` e o teste falha.

**2. Douglas-Peucker não simplificava anel nenhum.** O anel é fechado, então
`p[0] == p[n-1]` e o primeiro segmento do DP tinha comprimento zero: todo ponto
era medido contra `p[0]` e o critério perdia o sentido. Anéis pequenos (o caso
comum) colapsavam para 2 pontos e a guarda "devolve o anel original" devolvia
o anel **inteiro** — a simplificação estava mascarada, não aplicada. Para
corrigir, o anel é tratado como polilinha aberta de 0 a n−2 (primeiro e último
distintos, segmenta de verdade) e fechado de novo reemitindo o primeiro vértice.

O sintoma era visível nos números: com o bug, 43.813 vértices; corrigido,
47.384 — **mais** vértices, porque a versão com bug pulava um arco inteiro de
cada anel (o trecho entre o último split e o fechamento nunca era subdividido).
A curva de tamanho também era não-monotônica antes (0,10° dava arquivo *maior*
que 0,05°) e ficou monotônica depois.

## Testes — 8 casos, sem rede, sem dado real

`data/cache/` e `data/processed/` estão no `.gitignore`, então num clone novo
não existe malha nenhuma: um teste que exigisse o download de 5,78 MB só
passaria na máquina de quem desenvolveu e `make test` deixaria de ser portão.
A suíte **semeia as fontes cruas no cache** com fixtures sintéticos no formato
exato que a API devolve (verificado contra a API, não de memória) e exercita o
pipeline de verdade, do cache bruto ao GeoJSON final. O que se testa é o
pipeline, não a malha real.

| caso | o que prova |
|---|---|
| `testa_fonte_ausente` | sem fonte no cache e sem rede, `geo_prepara` falha em vez de inventar dado; não deixa saída pela metade |
| `testa_sem_dado` | sem arquivo processado, `geo_saida` devolve `NULL` (base do 503) |
| `testa_codigo_uf` | sigla em qualquer caixa e código numérico; `XX`, `99`, `""`, `NULL` → `NULL` |
| `testa_municipios` | prefixo `{"type":"FeatureCollection"`, `"codarea":"1100015"` sobrevive como string, centroide com 2 números, simplificação com efeito, GeoJSON parseável |
| `testa_ordem_estavel` | dois builds do zero (apagando o arquivo entre eles) dão **bytes idênticos** |
| `testa_estados` | mesclar as 5 macrorregiões dá 27 features, GeoJSON válido |
| `testa_filtro_uf` | filtro traz só a UF pedida, UF sem município dá coleção vazia (não lixo), `NULL` devolve tudo |
| `testa_rotas` | 503 sem malha, 200 com malha e `gzip=1`, 400 para UF inválida, **404 para caminho desconhecido** |

O `testa_ordem_estavel` apaga o arquivo processado antes de **cada** build: se
`geo_prepara` reusasse o resultado anterior, a comparação seria circular e não
provaria nada.

O limite de simplificação (`< 60` colchetes) foi calibrado nos números reais:
sem simplificação a fixture daria 132, com Douglas-Peucker dá 22. Ele pega
simplificador virando pass-through — confirmei por mutação. E pega a volta do
bug do anel fechado, porque o fallback devolvia o anel inteiro e o documento
voltava a 132.

O `testa_rotas` pegou um bug que eu não tinha visto: eu escolhia o nível com
`strstr(path, "municipios")`, então **`api/geo/naoexiste` respondia 200 com a
coleção de estados**. O teste de 200 existente continuaria passando, porque é um
200 válido — só o 404 explícito pega. Agora o caminho é conferido com `strcmp` e
`api/mapa/uf` também.

## Verificação

```
make test                                   OK: todos os testes passaram (a partir de cache vazio)
ASAN + UBSAN                                0 erros de runtime, 0 leaks em geo.c
PATH=/nonexistent ./tests/test_bin          OK  (prova que a suíte não toca a rede)
resíduo depois da suíte                     nenhum (data/cache vazio, só modelo.idx)
```

Servidor real na porta 8099, com a malha real processada:

| rota | código | bytes | observação |
|---|---|---|---|
| `/api/geo/estados` | 200 | 81.194 | 27 features |
| `/api/geo/municipios` | 200 | 1.485.243 | 5570 features, `Content-Encoding: gzip` |
| `/api/mapa/uf?uf=RO` | 200 | 1.563 | só Rondônia |
| `/api/mapa/uf?uf=sp&nivel=municipio` | 200 | 157.532 | 645 municípios |
| `/api/mapa/uf?uf=99` | 400 | 48 | UF inexistente |
| `/api/geo/naoexiste` | 404 | 53 | |
| `/api/mapa/x` | 404 | 53 | |

Contagens do filtro conferidas contra as localidades: RO 52, SP 645, AM 62 —
exatamente. MT dá 141 contra 142 do modelo, que é o `5101837` sem malha.

## Requisito de arquitetura respeitado

- As geometrias **são** cacheadas pelo navegador: `res.gzip = 1` em todas as
  rotas de geometria.
- Os **valores nunca** são pré-carregados: as properties da malha são só
  `codarea` e `centroide` (verificado na saída real), nenhum indicador, ano ou
  número. `/api/consulta` continua exigindo indicador e ano explícitos
  (`400` sem eles).
- Estado inicial em branco/neutro: sem `data/processed/geo_*.json` as rotas
  devolvem **503 com o comando exato** (`Execute: ./dashboard --atualizar`), não
  200 com corpo vazio — que renderizaria mapa em branco sem pista do erro.

## Contrato para o frontend

Toda coordenada — vértice e centroide — está em **graus × 1000, inteira**.
`web/map.js` divide por 1000 antes de projetar. Inteiro foi escolhido porque
(a) corta o payload sem perda visível (1 unidade ≈ 111 m) e (b) tem
representação textual única, o que é o que torna a saída reproduzível byte a
byte entre execuções e entre máquinas.

## Além do escopo (drive-by, 1 caractere)

`src/api.c` linha 62 tinha um byte NUL **literal** dentro do literal de char
(`buf[vlen] = '<NUL>';` em vez de `'\0'`). Compilava e funcionava, mas fazia o
gcc avisar "null character(s) preserved in literal" e — mais grave — fazia o
arquivo ser tratado como binário por grep, diff, editors e pelo meu próprio
`read`. Como eu estava editando esse arquivo, corrigi.

## Preocupações / pendências

1. **O teto de 1,2 MB é incompatível com o resto do spec.** Detalhado acima. Se
   a intenção era medir o que vai pela rede, o número já está 4,2× abaixo do
   alvo e nada precisa mudar. Se a intenção era o arquivo em disco, ou o spec
   muda o alvo, ou aceita um GeoJSON sem o redundante `"type":"Feature"` (o
   RFC 7946 §3.2 permite omitir, e economiza 111 KB), ou desce a coordenada
   para graus ×100. **Nenhuma dessas três foi feita** — mudam contrato que a
   spec pede explicitamente. É decisão de quem especificou, não minha.

2. **`model_carrega` ainda é stub (`n_local = 0`).** A checagem de cobertura
   codarea↔modelo não roda dentro de `make test` por causa disso: um teste que
   passasse em silêncio com modelo vazio seria pior do que nenhum. Fiz a
   checagem fora da suíte, contra a API de localidades, e está na seção acima.
   Quando o modelo for de verdade, essa checagem vira teste de verdade.

3. **`Boa Esperança do Norte (MT)` não tem polígono.** O frontend vai receber
   5570 features contra 5571 localidades. Não quebra o mapa (feature sem dado é
   só não pintada), mas se aparecer um KPI com denominador 5571 e o mapa com
   5570, a diferença vai parecer bug. Vale um teste de consistência na Tarefa do
   modelo.

4. **Os dois harnesses de verificação (`tests/geo_real.c`,
   `tests/geo_real_est.c`) foram removidos.** Eles baixavam 5,78 MB e não podem
   fazer parte de `make test`. As tabelas deste relatório vieram deles; se
   quiser reprodutir, dá para recriar — o caminho é semear
   `data/cache/ibge_malhas_municipios` e chamar `geo_prepara`.
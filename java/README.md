# Dashboard — backend Java

Servidor HTTP e ingestão do Dashboard, migrados do C para Java 21 + Spring
Boot 3.5. O contrato externo é o mesmo: as cinco rotas `/api/*`, os estáticos
de `web/`, os artefatos em `data/processed/` e a CLI `./dashboard`.

## Pré-requisitos

- **JDK 21+** — testado com OpenJDK 21.0.13 (Temurin)
- **Maven 3.9+** — testado com 3.9.12, rodando no mesmo JDK 21
- **Node** — só para as suítes do front; testado com v24.22.1

`make` verifica o ambiente em toda invocação, inclusive quando o JAR já existe:
sem `java`, sem `mvn` ou com Java < 21 ele falha com mensagem explícita em vez
de dizer "Nada a ser feito". `./dashboard` repete a checagem do Java antes de
executar. Sem JDK não há como rodar a suíte JUnit — os resultados citados aqui
foram medidos neste ambiente, e `make test` é o que os reproduz.

## Build, execução e testes

```sh
make                 # compila java/target/dashboard.jar (checa o ambiente antes)
./dashboard          # sobe em http://127.0.0.1:8080/
make test            # JUnit + sintaxe/testes do front + HTTP de verdade
make clean           # apaga java/target (não precisa de Maven)
```

Verificação completa, do zero:

```sh
make clean && make && make test
# 56 testes JUnit, 142 do front, 21 do mapa, 32 verificações HTTP
```

Opções da CLI:

```
./dashboard [--porta N] [--web DIR] [--atualizar [ibge|malhas|cnes]]
            [--geo-info] [--amostra]
```

`--atualizar` sem fonte e `--atualizar ibge` ingerem a população do IBGE e, em
seguida, as malhas. `--atualizar malhas` prepara só as malhas. `--atualizar
cnes` baixa (ou usa `data/cache/cnes`) o ZIP do CNES e grava
`data/processed/cnes.json` em JSON Lines.

O servidor só escuta em `127.0.0.1`: não há autenticação por decisão de
contrato. Rodar da raiz do projeto — `data/`, `web/` e `fontes.conf` são
relativos ao diretório de execução, como no binário C.

## Onde está o quê

```
src/main/java/br/gov/dashboard/
├── DashboardApplication.java   # parsing da CLI, portão de primeira execução
├── cli/                        # --atualizar, --geo-info, --amostra
├── consulta/ opcoes/ valores/  # /api/consulta, /api/opcoes, /api/valores
├── geografia/                  # /api/geo/*, /api/mapa/uf
├── modelo/                     # modelo.idx imutável, leitor/escritor, amostra
└── ingestao/
    ├── comum/                  # fontes.conf, cache com TTL, HTTP, CSV, ZIP
    ├── ibge/                   # Localidades + SIDRA 6579
    ├── geo/                    # Douglas-Peucker 0,05°, graus×1000
    └── saude/                  # CNES → JSON Lines
```

`data/cache/` guarda downloads crus com TTL lateral (`<chave>.ttl`);
`data/processed/` guarda `modelo.idx`, as duas malhas e o `cnes.json`.

## Paridade com o C

A migração comparou o servidor nas 50 rotas (amostra) e nas 19 rotas do dado
real, além de `modelo.idx` e das malhas byte a byte. A verificação foi executada
neste ambiente (OpenJDK 21.0.13 + Maven 3.9.12) com `make clean && make && make
test`; em outra máquina é preciso ter o mesmo toolchain. O histórico está em
`tools/contrato-diff.sh`; ele hoje exige um `C_BIN` externo, porque o C foi
removido.

O CNES é o único ponto com divergência deliberada de forma: o C gravava JSON
inválido (`"codarea":""1100001"` com aspas duplicadas, `razao`/`fantasia`/
`nivel` sem chave e conversão Latin-1 → UTF-8 duplicada). O Java emite o
formato que o próprio header `dataset_saude.h` documenta e que o campo
`chaves` do cabeçalho anuncia; os dados (código, textos, latitude, longitude,
ausências) são os mesmos.

Leitos e Dengue não foram portados: o C nunca os implementou — só declarou as
colunas — e não há formato de saída, oráculo ou consumidor. `--atualizar
leitos` e `--atualizar dengue` saem 2 com uma mensagem que diz isso.

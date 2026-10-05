CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2
LDLIBS  = -lz

SRC       = src/main.c src/http_server.c src/api.c src/model.c src/aggregate.c src/json.c src/cache.c src/charset.c src/zip.c src/csv.c src/fontes.c src/http_client.c src/geo.c
OBJ       = $(SRC:.c=.o)
TEST_SRC  = tests/test_main.c tests/test_http.c tests/test_json.c \
            tests/test_cache.c tests/test_charset.c tests/test_zip.c tests/test_csv.c \
            tests/test_fontes.c tests/test_model.c tests/test_aggregate.c tests/test_http_client.c tests/test_api.c tests/test_geo.c \
            src/http_server.c src/json.c src/cache.c src/charset.c src/zip.c src/csv.c \
            src/fontes.c src/model.c src/aggregate.c src/http_client.c src/api.c src/geo.c
TEST_HDR  = src/http_server.h src/json.h src/cache.h src/charset.h src/zip.h src/csv.h \
            src/fontes.h src/model.h src/aggregate.h src/http_client.h src/api.h src/geo.h
TEST_BIN  = tests/test_bin

all: dashboard

dashboard: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_BIN) dashboard web/test-web
	./$(TEST_BIN)
	$(NODE) tests/test_web.mjs
	sh tests/test_web.sh

# A suíte de rede fica no mesmo binário, mas só roda com --rede: assim `make test` não depende de internet e `make test-rede` baixa a API do IBGE.
test-rede: $(TEST_BIN)
	./$(TEST_BIN) --rede

# O front não tem build step (é ES modules servido como estático), então só há o
# que verificar: sintaxe de cada módulo e o porte de node antes do teste.
#
# `node` nem sempre está no PATH — no Debian ele costuma vir só de um gerenciador
# de versões instalado pelo usuário. Sem isto, `make test` morria em
# "node: not found" depois de a suíte C já ter passado, o que é o pior lugar
# para descobrir que falta um interpretador. A ordem de busca é: o NODE do
# ambiente, o PATH, e então o node do próprio Hermes. `command -v` em vez de
# `which` porque `which` não é padrão em POSIX e aqui é o /bin/sh do dash.
NODE ?= $(shell command -v node 2>/dev/null || \
                 ls -d $${HOME}/.hermes/tools/node-*/bin/node 2>/dev/null | sort -V | tail -1)
web/test-web:
	@if [ -z "$(NODE)" ] || [ ! -x "$(NODE)" ]; then \
	    echo "make test: node não encontrado — defina NODE=/caminho/para/node" >&2; \
	    exit 1; \
	fi
	@for m in web/*.js; do $(NODE) --check "$$m" || exit 1; done
	@echo "sintaxe ok: $$(ls web/*.js | tr '\n' ' ')"

$(TEST_BIN): $(TEST_SRC) $(TEST_HDR)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(LDLIBS)

clean:
	rm -f $(OBJ) $(TEST_BIN) dashboard src/*.o

.PHONY: all test test-rede clean

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2
LDLIBS  = -lz

SRC       = src/main.c src/http_server.c src/api.c src/model.c src/aggregate.c src/json.c src/cache.c src/charset.c src/zip.c src/csv.c src/fontes.c src/modelo.c src/http_client.c
OBJ       = $(SRC:.c=.o)
TEST_SRC  = tests/test_main.c tests/test_http.c tests/test_json.c \
            tests/test_cache.c tests/test_charset.c tests/test_zip.c tests/test_csv.c \
            tests/test_fontes.c tests/test_modelo.c tests/test_model.c tests/test_aggregate.c tests/test_http_client.c tests/test_api.c \
            src/http_server.c src/json.c src/cache.c src/charset.c src/zip.c src/csv.c \
            src/fontes.c src/modelo.c src/model.c src/aggregate.c src/http_client.c src/api.c
TEST_HDR  = src/http_server.h src/json.h src/cache.h src/charset.h src/zip.h src/csv.h \
            src/fontes.h src/modelo.h src/model.h src/aggregate.h src/http_client.h src/api.h
TEST_BIN  = tests/test_bin

all: dashboard

dashboard: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_BIN)
	./$(TEST_BIN)

# A suíte de rede fica no mesmo binário, mas só roda com --rede: assim `make test` não depende de internet e `make test-rede` baixa a API do IBGE.
test-rede: $(TEST_BIN)
	./$(TEST_BIN) --rede

$(TEST_BIN): $(TEST_SRC) $(TEST_HDR)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(LDLIBS)

clean:
	rm -f $(OBJ) $(TEST_BIN) dashboard src/*.o

.PHONY: all test test-rede clean

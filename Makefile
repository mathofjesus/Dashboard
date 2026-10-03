CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2
LDLIBS  = -lz

SRC       = src/main.c src/http_server.c
OBJ       = $(SRC:.c=.o)
TEST_SRC  = tests/test_main.c tests/test_http.c src/http_server.c
TEST_BIN  = tests/test_bin

all: dashboard

dashboard: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) src/http_server.h
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(LDLIBS)

clean:
	rm -f $(OBJ) $(TEST_BIN) dashboard

.PHONY: all test clean
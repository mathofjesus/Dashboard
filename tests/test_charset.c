#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/charset.h"

#define T_ASSERT(cond) do { if (!(cond)) { \
    fprintf(stderr, "FALHOU: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    exit(1); } } while (0)

static void test_latin1_simples(void) {
    const char *in = "abc";
    char out[10];
    size_t out_len = 0;
    int r = charset_latin1_para_utf8(in, 3, out, 10, &out_len);
    T_ASSERT(r == 0);
    T_ASSERT(out_len == 3);
    T_ASSERT(memcmp(out, "abc", 3) == 0);
}

static void test_latin1_acentos_portugues(void) {
    const char in[] = {'c', 'a', 'f', (char)0xE9, '\0'};
    char out[10];
    size_t out_len = 0;
    int r = charset_latin1_para_utf8(in, 4, out, 10, &out_len);
    T_ASSERT(r == 0);
    T_ASSERT(out_len == 5);
    T_ASSERT((unsigned char)out[3] == 0xC3 && (unsigned char)out[4] == 0xA9);
}

static void test_latin1_todos_acentos(void) {
    struct {
        unsigned char l1;
        unsigned char u1, u2;
    } cases[] = {
        {0xE1, 0xC3, 0xA1}, {0xE3, 0xC3, 0xA3}, {0xE7, 0xC3, 0xA7},
        {0xE9, 0xC3, 0xA9}, {0xED, 0xC3, 0xAD}, {0xF3, 0xC3, 0xB3},
        {0xF5, 0xC3, 0xB5}, {0xFA, 0xC3, 0xBA}, {0xE0, 0xC3, 0xA0},
    };
    for (int i = 0; i < 9; i++) {
        const char in[] = {(char)cases[i].l1};
        char out[4];
        size_t out_len = 0;
        int r = charset_latin1_para_utf8(in, 1, out, 4, &out_len);
        T_ASSERT(r == 0);
        T_ASSERT(out_len == 2);
        T_ASSERT((unsigned char)out[0] == cases[i].u1);
        T_ASSERT((unsigned char)out[1] == cases[i].u2);
    }
}

static void test_buffer_pequeno(void) {
    const char in[] = {(char)0xE9};
    char out[1];
    size_t out_len = 0;
    int r = charset_latin1_para_utf8(in, 1, out, 1, &out_len);
    T_ASSERT(r == 1);
    T_ASSERT(out_len == 2);
}

static void test_invalido(void) {
    const char in[] = {'a', (char)0x00, 'b'};
    char out[10];
    size_t out_len = 0;
    int r = charset_latin1_para_utf8(in, 3, out, 10, &out_len);
    T_ASSERT(r == 2);
}

void test_charset(void) {
    test_latin1_simples();
    test_latin1_acentos_portugues();
    test_latin1_todos_acentos();
    test_buffer_pequeno();
    test_invalido();
}

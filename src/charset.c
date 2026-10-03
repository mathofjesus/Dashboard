#include <stddef.h>
#include "charset.h"

int charset_latin1_para_utf8(const char *in, size_t in_len, char *out, size_t out_cap, size_t *out_len) {
    if (in == NULL || out == NULL || out_len == NULL) {
        return 2;
    }

    /* Detecta inválido: byte 0x00 presente */
    for (size_t i = 0; i < in_len; i++) {
        unsigned char b = (unsigned char)in[i];
        if (b == 0x00) {
            return 2;
        }
    }

    /* Calcula espaço necessário */
    size_t need = 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char b = (unsigned char)in[i];
        if (b < 0x80) {
            need += 1;
        } else {
            need += 2;
        }
    }

    if (need > out_cap) {
        *out_len = need;
        return 1;
    }

    /* Converte */
    size_t w = 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char b = (unsigned char)in[i];
        if (b < 0x80) {
            out[w++] = (char)b;
        } else {
            out[w++] = (char)(0xC0 | (b >> 6));
            out[w++] = (char)(0x80 | (b & 0x3F));
        }
    }
    *out_len = w;
    return 0;
}

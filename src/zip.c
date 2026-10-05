#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "zip.h"

#define LOCAL_FILE_HEADER_SIG 0x04034b50
#define CENTRAL_DIR_SIG       0x02014b50
#define END_OF_CENTRAL_DIR_SIG 0x06054b50

typedef struct {
    uint32_t signature;
    uint16_t version_made_by;
    uint16_t version_needed;
    uint16_t general_flag;
    uint16_t compression;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t internal_attr;
    uint32_t external_attr;
    uint32_t local_offset;
} cd_entry;

static uint32_t read_u32_le(const unsigned char *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}
static uint16_t read_u16_le(const unsigned char *p) {
    return p[0] | (p[1] << 8);
}

void zip_fecha(zip_arq *z) {
    if (!z) return;
    if (z->fp) {
        fclose(z->fp);
        z->fp = NULL;
    }
    if (z->entradas) {
        for (size_t i = 0; i < z->n; i++) {
            free(z->entradas[i].nome);
        }
        free(z->entradas);
        z->entradas = NULL;
    }
    z->n = 0;
}

int zip_lista(const char *caminho, zip_entrada **saida, size_t *nsaida) {
    if (!caminho || !saida || !nsaida) return -1;
    FILE *f = fopen(caminho, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf && size > 0) {
        fclose(f);
        return -1;
    }
    if (size > 0) {
        size_t rd = fread(buf, 1, (size_t)size, f);
        if (rd != (size_t)size) {
            free(buf);
            fclose(f);
            return -1;
        }
    }
    fclose(f);

    /* procurar EOCD a partir do fim */
    long eocd_off = -1;
    for (long i = size - 22; i >= 0; i--) {
        if (read_u32_le(buf + i) == END_OF_CENTRAL_DIR_SIG) {
            eocd_off = i;
            break;
        }
        if (size - i > 65535 + 22) break; /* limita busca */
    }
    if (eocd_off < 0) {
        free(buf);
        return -1;
    }
    unsigned char *eocd = buf + eocd_off;
    uint16_t total_entries = read_u16_le(eocd + 10);
    uint32_t cd_offset = read_u32_le(eocd + 16);

    unsigned char *cd = buf + cd_offset;
    zip_entrada *lista = (zip_entrada *)calloc((size_t)total_entries, sizeof(zip_entrada));
    if (!lista) {
        free(buf);
        return -1;
    }

    /* Itera por ponteiro, e não por índice: o registro de diretório central
     * tem tamanho variável (nome + extra + comentário), então "i * 46" só dá
     * a posição certa quando os três campos são vazios — que é o caso do ZIP
     * mínimo do teste e não o caso de um CSV do Ministério da Saúde.
     *
     * Havia aqui um laço de tentativa por índice fixo, com o corpo comentado
     * ("melhor ler dinamicamente", "reset") e nada sendo feito: ele não
     * escrevia em `lista` e não alterava nada, e o compilador avisava das duas
     * variáveis que ele declarava sem usar. A abordagem por ponteiro, logo
     * abaixo, é a que funciona. */
    unsigned char *p = cd;
    size_t out_n = 0;
    for (uint16_t i = 0; i < total_entries && out_n < (size_t)total_entries; i++) {
        if (read_u32_le(p) != CENTRAL_DIR_SIG) {
            p++;
            continue;
        }
        uint16_t name_len = read_u16_le(p + 28);
        uint16_t extra_len = read_u16_le(p + 30);
        uint16_t comment_len = read_u16_le(p + 32);
        uint32_t comp_size = read_u32_le(p + 20);
        uint32_t uncomp_size = read_u32_le(p + 24);
        uint16_t method = read_u16_le(p + 10);
        uint32_t local_off = read_u32_le(p + 42);
        char *nome = (char *)malloc((size_t)name_len + 1);
        if (!nome) {
            /* libera parcial */
            for (size_t j = 0; j < out_n; j++) free(lista[j].nome);
            free(lista);
            free(buf);
            return -1;
        }
        memcpy(nome, p + 46, name_len);
        nome[name_len] = '\0';
        lista[out_n].nome = nome;
        lista[out_n].tam_comprimido = comp_size;
        lista[out_n].tam_descomprimido = uncomp_size;
        lista[out_n].metodo = method;
        lista[out_n].offset_local = (long)local_off;
        out_n++;
        p += 46 + name_len + extra_len + comment_len;
    }

    *saida = lista;
    *nsaida = out_n;
    free(buf);
    return 0;
}

int zip_abre(const char *caminho, zip_arq *z) {
    if (!caminho || !z) return -1;
    zip_entrada *lista = NULL;
    size_t nlista = 0;
    int r = zip_lista(caminho, &lista, &nlista);
    if (r != 0) return r;
    FILE *f = fopen(caminho, "rb");
    if (!f) {
        for (size_t i = 0; i < nlista; i++) free(lista[i].nome);
        free(lista);
        return -1;
    }
    z->entradas = lista;
    z->n = nlista;
    z->fp = f;
    return 0;
}

static voidpf zlib_alloc(voidpf opaque, uInt items, uInt size) {
    (void)opaque;
    return malloc((size_t)items * (size_t)size);
}

static void zlib_free(voidpf opaque, voidpf address) {
    (void)opaque;
    free(address);
}

char *zip_extrai(const zip_arq *z, size_t idx, size_t *out_len) {
    if (!z || !z->fp || !z->entradas || idx >= z->n || !out_len) return NULL;
    const zip_entrada *e = &z->entradas[idx];
    if (fseek(z->fp, e->offset_local, SEEK_SET) != 0) return NULL;
    unsigned char lh[30];
    if (fread(lh, 1, 30, z->fp) != 30) return NULL;
    if (read_u32_le(lh) != LOCAL_FILE_HEADER_SIG) return NULL;
    uint16_t name_len = read_u16_le(lh + 26);
    uint16_t extra_len = read_u16_le(lh + 28);
    if (fseek(z->fp, (long)name_len + (long)extra_len, SEEK_CUR) != 0) return NULL;

    unsigned char *comp = NULL;
    unsigned char *uncomp = NULL;
    if (e->tam_comprimido > 0) {
        comp = (unsigned char *)malloc(e->tam_comprimido);
        if (!comp) return NULL;
        if (fread(comp, 1, e->tam_comprimido, z->fp) != e->tam_comprimido) {
            free(comp);
            return NULL;
        }
    }

    if (e->metodo == 0) {
        /* stored */
        uncomp = (unsigned char *)malloc(e->tam_descomprimido);
        if (!uncomp) {
            free(comp);
            return NULL;
        }
        if (comp) {
            memcpy(uncomp, comp, e->tam_descomprimido);
        }
        free(comp);
        *out_len = e->tam_descomprimido;
        return (char *)uncomp;
    } else if (e->metodo == 8) {
        /* deflate */
        z_stream strm;
        memset(&strm, 0, sizeof(strm));
        strm.zalloc = zlib_alloc;
        strm.zfree = zlib_free;
        strm.opaque = Z_NULL;
        if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
            free(comp);
            return NULL;
        }
        uncomp = (unsigned char *)malloc(e->tam_descomprimido);
        if (!uncomp) {
            inflateEnd(&strm);
            free(comp);
            return NULL;
        }
        strm.next_in = comp;
        strm.avail_in = (uInt)e->tam_comprimido;
        strm.next_out = uncomp;
        strm.avail_out = (uInt)e->tam_descomprimido;
        int res = inflate(&strm, Z_FINISH);
        if (res != Z_STREAM_END) {
            inflateEnd(&strm);
            free(uncomp);
            free(comp);
            return NULL;
        }
        *out_len = (size_t)strm.total_out;
        inflateEnd(&strm);
        free(comp);
        return (char *)uncomp;
    } else {
        free(comp);
        return NULL;
    }
}

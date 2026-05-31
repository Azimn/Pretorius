#include "cartridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t pe_cart_checksum(const void *data, size_t n){
    const unsigned char *p = (const unsigned char*)data;
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; ++i){
        h ^= (uint32_t)p[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

int pe_is_cart_path(const char *path){
    size_t n;
    if (!path) return 0;
    n = strlen(path);
    return n >= 5
        && path[n - 5] == '.'
        && (path[n - 4] == 'c' || path[n - 4] == 'C')
        && (path[n - 3] == 'a' || path[n - 3] == 'A')
        && (path[n - 2] == 'r' || path[n - 2] == 'R')
        && (path[n - 1] == 't' || path[n - 1] == 'T');
}

int pe_cart_state_dir(char *out, size_t n, const char *cart_path){
    const char *slash;
    size_t len;
    if (!out || n == 0 || !cart_path) return -1;
    slash = strrchr(cart_path, '/');
    if (!slash) slash = strrchr(cart_path, '\\');
    if (!slash){
        if (n < 2) return -1;
        out[0] = '.';
        out[1] = 0;
        return 0;
    }
    len = (size_t)(slash - cart_path);
    if (len == 0 || len >= n) return -1;
    memcpy(out, cart_path, len);
    out[len] = 0;
    return 0;
}

static int read_header(FILE *f, PECartridgeHeader *h){
    if (fread(h, 1, sizeof(*h), f) != sizeof(*h)) return -1;
    if (h->magic != PE_CART_MAGIC) return -2;
    if (h->version != PE_CART_VERSION_V4 && h->version != PE_CART_VERSION_V5) return -3;
    if (h->entry_count > PE_CART_MAX_SECTIONS) return -4;
    if (h->header_size != sizeof(*h)) return -5;
    return 0;
}

int pe_cart_validate_file(const char *cart_path){
    PECartridgeHeader h;
    unsigned char buf[1024];
    uint32_t checksum = 2166136261u;
    uint32_t remaining;
    FILE *f = fopen(cart_path, "rb");
    if (!f) return -1;
    if (read_header(f, &h) != 0){ fclose(f); return -2; }
    remaining = h.payload_size;
    while (remaining > 0){
        size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
        size_t got = fread(buf, 1, want, f);
        size_t i;
        if (got != want){ fclose(f); return -3; }
        for (i = 0; i < got; ++i){
            checksum ^= (uint32_t)buf[i];
            checksum *= 16777619u;
        }
        remaining -= (uint32_t)got;
    }
    fclose(f);
    if ((checksum ? checksum : 1u) != h.payload_checksum) return -4;
    return 0;
}

int pe_cart_load_section(const char *cart_path, const char *name, void *buf, size_t n){
    PECartridgeHeader h;
    PECartridgeEntry *e = NULL;
    unsigned char *bytes = (unsigned char*)buf;
    FILE *f;
    uint32_t got_sum;
    uint16_t i;

    if (!cart_path || !name || !buf) return -1;
    f = fopen(cart_path, "rb");
    if (!f) return -2;
    if (read_header(f, &h) != 0){ fclose(f); return -3; }

    for (i = 0; i < h.entry_count; ++i){
        if (strncmp(h.entries[i].name, name, PE_CART_NAME_LEN) == 0){
            e = &h.entries[i];
            break;
        }
    }
    if (!e){ fclose(f); return -4; }
    if (e->size != n){ fclose(f); return -5; }
    if (e->offset + e->size > h.payload_size){ fclose(f); return -6; }

    if (fseek(f, (long)(h.header_size + e->offset), SEEK_SET) != 0){
        fclose(f);
        return -7;
    }
    if (fread(buf, 1, n, f) != n){
        fclose(f);
        return -8;
    }
    fclose(f);
    got_sum = pe_cart_checksum(bytes, n);
    return got_sum == e->checksum ? 0 : -9;
}

int pe_cart_load_section_alloc(const char *cart_path, const char *name,
                               void **out, size_t *out_size){
    PECartridgeHeader h;
    PECartridgeEntry *e = NULL;
    FILE *f;
    void *buf;
    uint16_t i;

    if (!cart_path || !name || !out || !out_size) return -1;
    *out = NULL;
    *out_size = 0;
    f = fopen(cart_path, "rb");
    if (!f) return -2;
    if (read_header(f, &h) != 0){ fclose(f); return -3; }

    for (i = 0; i < h.entry_count; ++i){
        if (strncmp(h.entries[i].name, name, PE_CART_NAME_LEN) == 0){
            e = &h.entries[i];
            break;
        }
    }
    if (!e){ fclose(f); return -4; }
    if (e->offset + e->size > h.payload_size){ fclose(f); return -5; }
    buf = malloc(e->size ? e->size : 1u);
    if (!buf){ fclose(f); return -6; }
    if (fseek(f, (long)(h.header_size + e->offset), SEEK_SET) != 0){
        free(buf);
        fclose(f);
        return -7;
    }
    if (fread(buf, 1, e->size, f) != e->size){
        free(buf);
        fclose(f);
        return -8;
    }
    fclose(f);
    if (pe_cart_checksum(buf, e->size) != e->checksum){
        free(buf);
        return -9;
    }
    *out = buf;
    *out_size = e->size;
    return 0;
}

/* compile_cartridge.c - manifest-driven .cart packer.
 *
 * Usage:
 *   compile_cartridge <manifest.json> [output.cart]
 *
 * Manifest shape intentionally stays tiny so the tool remains C99-only:
 * {
 *   "output": "characters/pretorius/pretorius.cart",
 *   "sections": [
 *     {"name": "identity.bin", "path": "characters/pretorius/identity.bin"}
 *   ]
 * }
 */
#include "cartridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[PE_CART_NAME_LEN];
    char path[256];
    unsigned char *data;
    uint32_t size;
} Section;

static char *read_text(const char *path, long *out_size){
    FILE *f = fopen(path, "rb");
    char *p;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = (char*)malloc((size_t)n + 1u);
    if (!p){ fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n){
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    p[n] = 0;
    if (out_size) *out_size = n;
    return p;
}

static int read_binary(const char *path, unsigned char **out, uint32_t *out_size){
    long n;
    char *p = read_text(path, &n);
    if (!p) return -1;
    if (n < 0 || (unsigned long)n > 0xffffffffUL){
        free(p);
        return -1;
    }
    *out = (unsigned char*)p;
    *out_size = (uint32_t)n;
    return 0;
}

static const char *skip_ws(const char *p){
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
    return p;
}

static int copy_json_string(const char *p, char *out, size_t cap, const char **end){
    size_t w = 0;
    p = skip_ws(p);
    if (*p != '"') return -1;
    ++p;
    while (*p && *p != '"'){
        char c = *p++;
        if (c == '\\' && *p) c = *p++;
        if (w + 1u >= cap) return -1;
        out[w++] = c;
    }
    if (*p != '"') return -1;
    out[w] = 0;
    if (end) *end = p + 1;
    return 0;
}

static int extract_key_string(const char *start, const char *key,
                              char *out, size_t cap, const char **end){
    char needle[64];
    const char *p;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(start, needle);
    if (!p) return -1;
    p += strlen(needle);
    p = skip_ws(p);
    if (*p != ':') return -1;
    return copy_json_string(p + 1, out, cap, end);
}

static int parse_manifest(const char *json, char *output, size_t output_cap,
                          Section *sections, int *section_count){
    const char *p;
    int count = 0;
    if (extract_key_string(json, "output", output, output_cap, NULL) != 0)
        output[0] = 0;
    p = strstr(json, "\"sections\"");
    if (!p) return -1;
    while ((p = strstr(p, "\"name\"")) != NULL){
        const char *after_name;
        if (count >= PE_CART_MAX_SECTIONS) return -2;
        if (extract_key_string(p, "name", sections[count].name,
                               sizeof(sections[count].name), &after_name) != 0)
            return -3;
        if (extract_key_string(after_name, "path", sections[count].path,
                               sizeof(sections[count].path), &p) != 0)
            return -4;
        sections[count].data = NULL;
        sections[count].size = 0;
        count++;
    }
    {
        char lm_path[256];
        if (extract_key_string(json, "lm_file", lm_path, sizeof(lm_path), NULL) == 0){
            if (count >= PE_CART_MAX_SECTIONS) return -2;
            snprintf(sections[count].name, sizeof(sections[count].name), "LM ");
            snprintf(sections[count].path, sizeof(sections[count].path), "%s", lm_path);
            sections[count].data = NULL;
            sections[count].size = 0;
            count++;
        }
    }
    *section_count = count;
    return count > 0 ? 0 : -5;
}

int main(int argc, char **argv){
    char output[256];
    Section sections[PE_CART_MAX_SECTIONS];
    PECartridgeHeader h;
    unsigned char *payload = NULL;
    uint32_t payload_size = 0;
    uint32_t cursor = 0;
    long manifest_size;
    char *json;
    FILE *f;
    int section_count = 0;
    int i;

    if (argc != 2 && argc != 3){
        fprintf(stderr, "usage: %s <manifest.json> [output.cart]\n", argv[0]);
        return 1;
    }

    json = read_text(argv[1], &manifest_size);
    if (!json){
        fprintf(stderr, "compile_cartridge: cannot read %s\n", argv[1]);
        return 1;
    }
    (void)manifest_size;

    if (parse_manifest(json, output, sizeof(output), sections, &section_count) != 0){
        fprintf(stderr, "compile_cartridge: invalid manifest %s\n", argv[1]);
        free(json);
        return 1;
    }
    free(json);
    if (argc == 3) snprintf(output, sizeof(output), "%s", argv[2]);
    if (!output[0]){
        fprintf(stderr, "compile_cartridge: manifest has no output\n");
        return 1;
    }

    for (i = 0; i < section_count; ++i){
        if (read_binary(sections[i].path, &sections[i].data, &sections[i].size) != 0){
            fprintf(stderr, "compile_cartridge: cannot read section %s (%s)\n",
                    sections[i].name, sections[i].path);
            return 1;
        }
        payload_size += sections[i].size;
    }

    payload = (unsigned char*)malloc(payload_size ? payload_size : 1u);
    if (!payload) return 1;
    memset(&h, 0, sizeof(h));
    h.magic = PE_CART_MAGIC;
    h.version = PE_CART_VERSION;
    h.entry_count = (uint16_t)section_count;
    h.header_size = (uint32_t)sizeof(h);
    h.payload_size = payload_size;

    for (i = 0; i < section_count; ++i){
        snprintf(h.entries[i].name, sizeof(h.entries[i].name), "%s", sections[i].name);
        h.entries[i].offset = cursor;
        h.entries[i].size = sections[i].size;
        h.entries[i].checksum = pe_cart_checksum(sections[i].data, sections[i].size);
        memcpy(payload + cursor, sections[i].data, sections[i].size);
        cursor += sections[i].size;
        free(sections[i].data);
    }
    h.payload_checksum = pe_cart_checksum(payload, payload_size);

    f = fopen(output, "wb");
    if (!f){
        perror(output);
        free(payload);
        return 1;
    }
    if (fwrite(&h, 1, sizeof(h), f) != sizeof(h)
        || (payload_size && fwrite(payload, 1, payload_size, f) != payload_size)){
        fprintf(stderr, "compile_cartridge: write failed\n");
        fclose(f);
        free(payload);
        return 1;
    }
    fclose(f);
    free(payload);

    fprintf(stderr, "wrote %s (%u sections, %u payload bytes)\n",
            output, (unsigned)section_count, (unsigned)payload_size);
    return 0;
}

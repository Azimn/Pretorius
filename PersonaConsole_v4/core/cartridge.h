/* cartridge.h - single-file Persona cartridge container. */
#ifndef PE_CARTRIDGE_H
#define PE_CARTRIDGE_H

#include <stdint.h>
#include <stddef.h>

#define PE_CART_MAGIC      0x54524143u  /* 'CART' little-endian */
#define PE_CART_VERSION    1u
#define PE_CART_NAME_LEN   48
#define PE_CART_MAX_SECTIONS 32

#pragma pack(push, 1)
typedef struct {
    char     name[PE_CART_NAME_LEN];
    uint32_t offset;
    uint32_t size;
    uint32_t checksum;
} PECartridgeEntry;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t entry_count;
    uint32_t header_size;
    uint32_t payload_size;
    uint32_t payload_checksum;
    uint8_t  reserved[12];
    PECartridgeEntry entries[PE_CART_MAX_SECTIONS];
} PECartridgeHeader;
#pragma pack(pop)

uint32_t pe_cart_checksum(const void *data, size_t n);
int pe_cart_load_section(const char *cart_path, const char *name, void *buf, size_t n);
int pe_cart_load_section_alloc(const char *cart_path, const char *name,
                               void **out, size_t *out_size);
int pe_cart_validate_file(const char *cart_path);
int pe_is_cart_path(const char *path);
int pe_cart_state_dir(char *out, size_t n, const char *cart_path);

#endif

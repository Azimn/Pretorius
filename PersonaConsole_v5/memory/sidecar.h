/* sidecar.h — versioning policy for optional persistent state.
 *
 * Doctrine: new persistent state lands as a sidecar rather than a core
 * struct layout change. Every sidecar carries this header so it can be
 * validated, version-checked, and migrated without surprising the engine.
 * Future modules (speech events, actor index, long-arc drift, conflict
 * history, etc.) load and save through this surface.
 *
 *   - Required sidecars hard-fail on corruption.
 *   - Optional sidecars (F_OPTIONAL) soft-fail on missing or unreadable.
 *   - Forward-compat: a sidecar with version > max_version is refused;
 *     the runtime never half-reads a future format.
 *   - Reserved padding is zero today and must remain zero in writers
 *     until a future version repurposes it.
 */
#ifndef PE_SIDECAR_H
#define PE_SIDECAR_H

#include <stdint.h>

/* Build a 4-char little-endian magic tag, e.g. PE_SIDECAR_MAGIC('S','P','C','H'). */
#define PE_SIDECAR_MAGIC(a, b, c, d) \
    ( ((uint32_t)(unsigned char)(a)      ) \
    | ((uint32_t)(unsigned char)(b) <<  8) \
    | ((uint32_t)(unsigned char)(c) << 16) \
    | ((uint32_t)(unsigned char)(d) << 24) )

/* Header flags. */
#define PE_SIDECAR_F_REQUIRED  0u           /* default: missing = hard fail */
#define PE_SIDECAR_F_OPTIONAL  (1u << 0)    /* soft-fail on missing/unreadable */

typedef struct {
    uint32_t magic;        /* identifies the sidecar kind */
    uint16_t version;      /* monotonic; bump on any layout change */
    uint16_t flags;        /* PE_SIDECAR_F_* */
    uint32_t entry_count;  /* in-use entries */
    uint32_t capacity;     /* maximum entries the file holds */
    uint32_t reserved[4];  /* zero today; reserved for future expansion */
} pe_sidecar_header_t;

/* Validation result codes. */
#define PE_SIDECAR_OK             0
#define PE_SIDECAR_E_NULL        -1   /* header pointer was NULL */
#define PE_SIDECAR_E_MAGIC       -2   /* magic mismatch (wrong file kind) */
#define PE_SIDECAR_E_TOO_OLD     -3   /* version < min_version */
#define PE_SIDECAR_E_TOO_NEW     -4   /* version > max_version */
#define PE_SIDECAR_E_OVERFLOW    -5   /* entry_count > capacity */

#ifdef __cplusplus
extern "C" {
#endif

/* Returns PE_SIDECAR_OK or a negative PE_SIDECAR_E_* code. */
int pe_sidecar_validate(const pe_sidecar_header_t *h,
                        uint32_t expected_magic,
                        uint16_t min_version,
                        uint16_t max_version);

/* True if validation said TOO_NEW; callers may want to treat that
 * differently from corruption (e.g. log + degrade rather than crash). */
int pe_sidecar_is_forward_incompatible(int validate_result);

#ifdef __cplusplus
}
#endif
#endif

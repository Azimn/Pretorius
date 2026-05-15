/* mutator.h — procedural template expansion engine.
 *
 * Templates may contain splice markers like [adj_morbid] or [verb_create].
 * The mutator replaces each marker with one fragment chosen from a
 * synonym bank, biased by plan.theatricality / plan.aggression.
 *
 * v3.2: banks were previously a hardcoded Pretorian-flavored table in
 * mutator.c.  They now live on the cartridge as a serialisable
 * BankRegistry struct — each character ships its own register.  The old
 * mutator_expand() is preserved (uses an internal default registry) so
 * standalone callers like plasticity_test keep working; engine code
 * uses mutator_expand_banks(..., &eng->banks).
 *
 * Deterministic: caller supplies an xorshift32 RNG state, advanced per
 * substitution. Same seed → same expansion.
 */
#ifndef PE_MUTATOR_H
#define PE_MUTATOR_H

#include <stdint.h>
#include <stddef.h>

/* ---------- v3.2: cartridge-borne bank registry ---------- */

#define PE_BANK_COUNT_MAX      12
#define PE_BANK_NAME_LEN       16
#define PE_BANK_ENTRY_LEN      48
#define PE_BANK_ENTRIES_MAX    10
#define PE_BANK_REGISTRY_MAGIC 0x424B4150u   /* 'PAKB' */
#define PE_BANK_REGISTRY_VERSION 1u

#pragma pack(push, 1)
typedef struct {
    char    name[PE_BANK_NAME_LEN];                       /* NUL-terminated */
    char    entries[PE_BANK_ENTRIES_MAX][PE_BANK_ENTRY_LEN];
    uint8_t entry_count;
    uint8_t min_theatricality;
    uint8_t min_aggression;
    uint8_t _pad;
} BankDef;
/* layout: 16 + 10*48 + 4 = 500 bytes */

typedef struct {
    uint32_t magic;                              /* PE_BANK_REGISTRY_MAGIC */
    uint32_t version;                            /* PE_BANK_REGISTRY_VERSION */
    uint8_t  bank_count;                         /* 0..PE_BANK_COUNT_MAX */
    uint8_t  _pad[3];
    BankDef  banks[PE_BANK_COUNT_MAX];
} BankRegistry;
/* layout: 12 + 12*500 = 6012 bytes */
#pragma pack(pop)

/* Populate a BankRegistry with the Pretorian-flavored default bank set
 * (back-compat for callers that don't ship their own banks.bin). */
void mutator_load_default_banks(BankRegistry *out);

/* Expand [bank_name] markers in `tmpl` into `out` (NUL-terminated).
 * Returns number of bytes written (excluding NUL), or 0 on overflow.
 *
 *   tmpl            input template, may contain [bank_name] markers
 *   out, out_cap    output buffer
 *   theatricality   0..255, gates dramatic/grand banks
 *   aggression      0..255, gates morbid/violent banks
 *   rng_state       xorshift32 state, advanced once per substitution
 *   banks           bank registry (NULL → internal default)
 *
 * Markers that don't correspond to a known bank are passed through verbatim.
 */
size_t mutator_expand_banks(const char *tmpl, char *out, size_t out_cap,
                            uint8_t theatricality, uint8_t aggression,
                            uint32_t *rng_state,
                            const BankRegistry *banks);

/* Back-compat wrapper — uses the internal default Pretorian registry. */
size_t mutator_expand(const char *tmpl, char *out, size_t out_cap,
                      uint8_t theatricality, uint8_t aggression,
                      uint32_t *rng_state);

/* Introspection: how many banks are registered in the internal default. */
int mutator_bank_count(void);

/* Introspection: bank metadata by index. NULL on out-of-range. */
const char *mutator_bank_name(int idx);
int         mutator_bank_size(int idx);

#endif /* PE_MUTATOR_H */

/* mutator.c — see mutator.h.
 *
 * v3.2: banks are now data, not code.  The internal default registry
 * mirrors the original Pretorian-flavored hardcoded table for back-compat;
 * engine callers pass their cartridge's BankRegistry via the _banks variant.
 */
#include "mutator.h"
#include <string.h>

/* ---------- xorshift32 (matches engine RNG) ---------- */
static uint32_t xs32(uint32_t *s){
    uint32_t x = *s ? *s : 0xA5A5A5A5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* ---------- default Pretorian-flavored bank set ---------- *
 *
 * Identical to the original hand-coded table.  Used to populate the
 * internal default registry on first call to mutator_expand() and by
 * mutator_load_default_banks() for cartridges that haven't authored their
 * own banks.bin. */
typedef struct {
    const char *name;
    const char *const *entries;
    int entry_count;
    uint8_t min_theatricality;
    uint8_t min_aggression;
} DefaultBank;

static const char *adj_morbid[] = {
    "grim", "unwholesome", "sepulchral", "cadaverous", "putrid",
    "moribund", "fetid", "ghastly", "necrotic", "saturnine"
};
static const char *adj_grand[] = {
    "magnificent", "prodigious", "monumental", "transcendent",
    "exalted", "vaulting", "Promethean", "irrepressible"
};
static const char *adj_unwholesome[] = {
    "queer", "unnatural", "peculiar", "unsettling", "aberrant",
    "irregular", "perverse", "deviant"
};
static const char *adj_scientific[] = {
    "experimental", "empirical", "anatomical", "galvanic",
    "chemical", "physiologic", "biological", "vital"
};
static const char *noun_obsession[] = {
    "homunculi", "the bones", "the substrate", "the vital fluid",
    "my specimens", "the apparatus", "the matrix", "the principle"
};
static const char *verb_create[] = {
    "create", "fashion", "beget", "conjure", "engender",
    "wrest from chaos", "summon", "compound"
};
static const char *verb_destroy[] = {
    "destroy", "dissolve", "annihilate", "unmake", "reduce to constituents",
    "extinguish", "obliterate"
};
static const char *exclamation[] = {
    "—", "!", "!!", "...", "—!", " indeed"
};
static const char *simile_anatomical[] = {
    "as the spine articulates with the skull",
    "like marrow drawn from a long bone",
    "as ligature follows blade",
    "with the precision of a dissection",
    "as a heart resumes after the shock",
    "like clay obedient to the wheel"
};
static const char *intensifier[] = {
    "quite", "rather", "exceedingly", "monstrously", "uncommonly",
    "preternaturally", "extraordinarily"
};

#define DBANK(name_, table_, theat_, agg_)                                \
    { (name_), (const char *const*)(table_),                              \
      (int)(sizeof(table_)/sizeof((table_)[0])), (theat_), (agg_) }

static const DefaultBank DEFAULT_BANKS[] = {
    DBANK("adj_morbid",         adj_morbid,         100, 100),
    DBANK("adj_grand",          adj_grand,          150,   0),
    DBANK("adj_unwholesome",    adj_unwholesome,     50,  50),
    DBANK("adj_scientific",     adj_scientific,       0,   0),
    DBANK("noun_obsession",     noun_obsession,       0,   0),
    DBANK("verb_create",        verb_create,         50,   0),
    DBANK("verb_destroy",       verb_destroy,         0, 100),
    DBANK("exclamation",        exclamation,        100,   0),
    DBANK("simile_anatomical",  simile_anatomical,  150,   0),
    DBANK("intensifier",        intensifier,         50,   0),
};
#define DEFAULT_BANK_COUNT ((int)(sizeof(DEFAULT_BANKS)/sizeof(DEFAULT_BANKS[0])))

/* ---------- default registry, lazy-built once ---------- */
static BankRegistry g_default_registry;
static int          g_default_built = 0;

static void copy_default_into(BankRegistry *r){
    memset(r, 0, sizeof(*r));
    r->magic   = PE_BANK_REGISTRY_MAGIC;
    r->version = PE_BANK_REGISTRY_VERSION;
    int n = DEFAULT_BANK_COUNT;
    if (n > PE_BANK_COUNT_MAX) n = PE_BANK_COUNT_MAX;
    r->bank_count = (uint8_t)n;
    for (int i = 0; i < n; ++i){
        BankDef *b = &r->banks[i];
        const DefaultBank *d = &DEFAULT_BANKS[i];
        size_t nm_len = strlen(d->name);
        if (nm_len >= PE_BANK_NAME_LEN) nm_len = PE_BANK_NAME_LEN - 1;
        memcpy(b->name, d->name, nm_len);
        b->name[nm_len] = 0;
        int ec = d->entry_count;
        if (ec > PE_BANK_ENTRIES_MAX) ec = PE_BANK_ENTRIES_MAX;
        b->entry_count       = (uint8_t)ec;
        b->min_theatricality = d->min_theatricality;
        b->min_aggression    = d->min_aggression;
        for (int e = 0; e < ec; ++e){
            const char *src = d->entries[e];
            size_t L = strlen(src);
            if (L >= PE_BANK_ENTRY_LEN) L = PE_BANK_ENTRY_LEN - 1;
            memcpy(b->entries[e], src, L);
            b->entries[e][L] = 0;
        }
    }
}

void mutator_load_default_banks(BankRegistry *out){
    if (!out) return;
    copy_default_into(out);
}

static const BankRegistry *get_default_registry(void){
    if (!g_default_built){
        copy_default_into(&g_default_registry);
        g_default_built = 1;
    }
    return &g_default_registry;
}

/* ---------- bank lookup in a registry ---------- */
static int registry_find_bank(const BankRegistry *r, const char *name, size_t len){
    if (!r) return -1;
    if (len >= PE_BANK_NAME_LEN) return -1;
    for (int i = 0; i < r->bank_count && i < PE_BANK_COUNT_MAX; ++i){
        const char *bn = r->banks[i].name;
        if (strlen(bn) == len && memcmp(bn, name, len) == 0) return i;
    }
    return -1;
}

/* ---------- expansion (registry-driven) ---------- */
size_t mutator_expand_banks(const char *tmpl, char *out, size_t out_cap,
                            uint8_t theatricality, uint8_t aggression,
                            uint32_t *rng_state,
                            const BankRegistry *banks){
    if (!tmpl || !out || out_cap == 0) return 0;

    /* Validate the registry — if magic/version don't match or bank_count
     * is zero, fall back to the internal default Pretorian registry.
     * This keeps a freshly-zeroed Engine functional even before a
     * cartridge ships its own banks.bin. */
    if (!banks || banks->magic != PE_BANK_REGISTRY_MAGIC
        || banks->version != PE_BANK_REGISTRY_VERSION
        || banks->bank_count == 0){
        banks = get_default_registry();
    }

    size_t w = 0;
    const char *p = tmpl;
    while (*p){
        if (*p == '['){
            const char *end = strchr(p + 1, ']');
            if (end){
                size_t name_len = (size_t)(end - p - 1);
                int b = registry_find_bank(banks, p + 1, name_len);
                if (b >= 0){
                    const BankDef *bank = &banks->banks[b];
                    int pick;
                    if (bank->entry_count == 0){ p = end + 1; continue; }
                    if (theatricality < bank->min_theatricality ||
                        aggression    < bank->min_aggression){
                        pick = 0;
                    } else {
                        pick = (int)(xs32(rng_state) % (uint32_t)bank->entry_count);
                    }
                    const char *frag = bank->entries[pick];
                    size_t flen = strlen(frag);
                    if (w + flen + 1 > out_cap) return 0;
                    memcpy(out + w, frag, flen);
                    w += flen;
                    p = end + 1;
                    continue;
                }
                /* Unknown bank — pass through verbatim. */
            }
        }
        if (w + 2 > out_cap) return 0;
        out[w++] = *p++;
    }
    out[w] = 0;
    return w;
}

size_t mutator_expand(const char *tmpl, char *out, size_t out_cap,
                      uint8_t theatricality, uint8_t aggression,
                      uint32_t *rng_state){
    return mutator_expand_banks(tmpl, out, out_cap,
                                theatricality, aggression,
                                rng_state, get_default_registry());
}

int mutator_bank_count(void){ return DEFAULT_BANK_COUNT; }

const char *mutator_bank_name(int idx){
    if (idx < 0 || idx >= DEFAULT_BANK_COUNT) return NULL;
    return DEFAULT_BANKS[idx].name;
}

int mutator_bank_size(int idx){
    if (idx < 0 || idx >= DEFAULT_BANK_COUNT) return 0;
    return DEFAULT_BANKS[idx].entry_count;
}

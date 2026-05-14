/* mutator.c — see mutator.h.
 *
 * Bank registry: hand-curated synonym lists in Pretorius's register.
 * Each bank has a min_theatricality and min_aggression gate; if the
 * current plan doesn't clear the gate, the bank falls back to a "plain"
 * substitute (the first entry, treated as default).
 *
 * Add new banks by appending to the BANKS table.  Keep entries short.
 */
#include "mutator.h"
#include <string.h>

typedef struct {
    const char *name;
    const char *const *entries;
    int entry_count;
    uint8_t min_theatricality;   /* fall back to entries[0] below this */
    uint8_t min_aggression;
} Bank;

/* ---------- bank contents ---------- */

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

#define BANK(name_, table_, theat_, agg_)                                 \
    { (name_), (const char *const*)(table_),                              \
      (int)(sizeof(table_)/sizeof((table_)[0])), (theat_), (agg_) }

static const Bank BANKS[] = {
    BANK("adj_morbid",         adj_morbid,         100, 100),
    BANK("adj_grand",          adj_grand,          150,   0),
    BANK("adj_unwholesome",    adj_unwholesome,     50,  50),
    BANK("adj_scientific",     adj_scientific,       0,   0),
    BANK("noun_obsession",     noun_obsession,       0,   0),
    BANK("verb_create",        verb_create,         50,   0),
    BANK("verb_destroy",       verb_destroy,         0, 100),
    BANK("exclamation",        exclamation,        100,   0),
    BANK("simile_anatomical",  simile_anatomical,  150,   0),
    BANK("intensifier",        intensifier,         50,   0)
};
#define BANK_COUNT ((int)(sizeof(BANKS)/sizeof(BANKS[0])))

/* ---------- xorshift32 (matches engine RNG) ---------- */
static uint32_t xs32(uint32_t *s){
    uint32_t x = *s ? *s : 0xA5A5A5A5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* ---------- bank lookup ---------- */
static int find_bank(const char *name, size_t len){
    int i;
    for (i = 0; i < BANK_COUNT; i++){
        if (strlen(BANKS[i].name) == len &&
            memcmp(BANKS[i].name, name, len) == 0) return i;
    }
    return -1;
}

/* ---------- expansion ---------- */
size_t mutator_expand(const char *tmpl, char *out, size_t out_cap,
                      uint8_t theatricality, uint8_t aggression,
                      uint32_t *rng_state){
    size_t w = 0;
    const char *p = tmpl;
    if (!tmpl || !out || out_cap == 0) return 0;

    while (*p){
        if (*p == '['){
            const char *end = strchr(p + 1, ']');
            if (end){
                size_t name_len = (size_t)(end - p - 1);
                int b = find_bank(p + 1, name_len);
                if (b >= 0){
                    const Bank *bank = &BANKS[b];
                    int pick;
                    const char *frag;
                    size_t flen;
                    /* Gating: if plan doesn't meet thresholds, force entry 0
                     * (the most "neutral" version of the bank). */
                    if (theatricality < bank->min_theatricality ||
                        aggression    < bank->min_aggression){
                        pick = 0;
                    } else {
                        pick = (int)(xs32(rng_state) % (uint32_t)bank->entry_count);
                    }
                    frag = bank->entries[pick];
                    flen = strlen(frag);
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

int mutator_bank_count(void){ return BANK_COUNT; }

const char *mutator_bank_name(int idx){
    if (idx < 0 || idx >= BANK_COUNT) return NULL;
    return BANKS[idx].name;
}

int mutator_bank_size(int idx){
    if (idx < 0 || idx >= BANK_COUNT) return 0;
    return BANKS[idx].entry_count;
}

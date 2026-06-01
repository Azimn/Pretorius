/* dissonance.c — V6 Phase 5b implementation. See dissonance.h. */
#include "persona.h"
#include "persona_internal.h"
#include "dissonance.h"
#include "speech_ledger.h"      /* PE_SA_* enum */

#include <string.h>

void pe_dissonance_init(pe_dissonance_t *d){
    if (!d) return;
    memset(d, 0, sizeof(*d));
    d->header.magic       = PE_DISSONANCE_MAGIC;
    d->header.version     = PE_DISSONANCE_VERSION;
    d->header.flags       = PE_SIDECAR_F_OPTIONAL;
    d->header.entry_count = 1;
    d->header.capacity    = 1;
}

int pe_dissonance_load(pe_dissonance_t *d, const char *char_dir){
    if (!d || !char_dir) return -1;
    pe_dissonance_init(d);

    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, "dissonance.bin") != 0)
        return -1;

    pe_dissonance_t tmp;
    if (pe_read_file(path, &tmp, sizeof(tmp)) != 0) return 0;

    int v = pe_sidecar_validate(&tmp.header, PE_DISSONANCE_MAGIC,
                                1, PE_DISSONANCE_VERSION);
    if (v != PE_SIDECAR_OK) return 0;
    *d = tmp;
    return 0;
}

int pe_dissonance_save(const pe_dissonance_t *d, const char *char_dir){
    if (!d || !char_dir) return -1;
    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, "dissonance.bin") != 0)
        return -1;
    return pe_write_file_atomic(path, d, sizeof(*d));
}

/* Saturating add / subtract on the 0..1000 range. */
static uint16_t accum_add(uint16_t cur, int delta){
    int v = (int)cur + delta;
    if (v < 0)    v = 0;
    if (v > 1000) v = 1000;
    return (uint16_t)v;
}

void pe_dissonance_update_from_speech(pe_dissonance_t *d,
                                      uint8_t speech_act,
                                      uint8_t impact_pct){
    if (!d) return;
    int mag = impact_pct > 0 ? (int)impact_pct : 0;
    if (mag > 100) mag = 100;
    int impact = 5 + mag / 5;   /* range ~5..25 per event */

    switch (speech_act){
    /* Ideal-self gap: the character wanted to engage but did not.
     * Evasion, deflection, withdrawal, and pause are all "failed
     * approaches" toward the ideal. */
    case PE_SA_EVASION:
    case PE_SA_DEFLECTION:
    case PE_SA_WITHDRAWAL:
    case PE_SA_PAUSE:
        d->ideal_gap = accum_add(d->ideal_gap, impact);
        break;
    /* Ought-self gap: apology and concession are reparative — the
     * character honored an obligation, so the ought gap closes. */
    case PE_SA_APOLOGY:
    case PE_SA_CONCESSION:
        d->ought_gap = accum_add(d->ought_gap, -impact);
        break;
    /* Feared-self gap: aggressive acts move the character toward a
     * common feared-self direction (cruelty / pettiness). Insult and
     * threat raise feared_gap. */
    case PE_SA_INSULT:
    case PE_SA_THREAT:
        d->feared_gap = accum_add(d->feared_gap, impact);
        break;
    /* Confession is mixed: lowers ought-gap (you came clean) but
     * raises ideal_gap modestly (the ideal self might not need to
     * confess at all). */
    case PE_SA_CONFESSION:
        d->ought_gap = accum_add(d->ought_gap, -impact);
        d->ideal_gap = accum_add(d->ideal_gap,  impact / 4);
        break;
    /* Refusal: gentle ideal-gap rise (refusing to engage with what one
     * "should" be capable of, regardless of whether the refusal is
     * wise). Phase 5c will refine using withhold_reason. */
    case PE_SA_REFUSAL:
        d->ideal_gap = accum_add(d->ideal_gap, impact / 2);
        break;
    default:
        break;  /* assertions, questions, greetings, etc.: no shift */
    }
}

void pe_dissonance_decay_to(pe_dissonance_t *d, uint32_t turn_count){
    if (!d) return;
    /* Idempotent within the same turn. */
    if (turn_count <= d->last_decay_turn) {
        d->last_decay_turn = turn_count;
        return;
    }
    uint32_t turns = turn_count - d->last_decay_turn;
    /* 1 pt per turn per accumulator — unresolved dissonance lingers but
     * does not pile up indefinitely. */
    if (turns > 1000u) turns = 1000u;
    int drop = (int)turns;
    d->ideal_gap  = accum_add(d->ideal_gap,  -drop);
    d->ought_gap  = accum_add(d->ought_gap,  -drop);
    d->feared_gap = accum_add(d->feared_gap, -drop);
    d->last_decay_turn = turn_count;
}

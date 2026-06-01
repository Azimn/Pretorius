/* dissonance.h — V6 Phase 5b: typed dissonance accumulators.
 *
 * Per V6_DOCTRINE §15, self-discrepancy theory (Higgins 1987)
 * distinguishes three internal references that each produce a
 * different emotional family:
 *
 *   Discrepancy             Source                   Emotion family
 *   --------------------- + ----------------------- + -----------------------------
 *   Actual vs ideal_self   what I want to be vs am   disappointment, shame, envy, aspiration
 *   Actual vs ought_self   what I should be vs am    guilt, anxiety, defensiveness
 *   Actual vs feared_self  what I dread becoming     panic, denial, overcorrection
 *
 * V6 maintains one accumulator per type. They are canonical Layer-1
 * state — engine-authored, persisted in a sidecar, exposed in state
 * JSON, and updated only by structured engine signals (Phase 5b updates
 * from speech-event types; Phase 5c will read them to choose typed
 * resolution paths: confess / apologize / rationalize / deny /
 * overcorrect).
 *
 * Without cartridge-authored ideal/ought/feared fields (which come in
 * Phase 5c), Phase 5b uses speech-act class as the proxy signal:
 *   - evasion / deflection / withdrawal / pause raise ideal_gap (the
 *     character wanted to engage but didn't),
 *   - apology / concession lower ought_gap (the character repaired
 *     an obligation),
 *   - insult / threat raise feared_gap (the character became
 *     aggressive — a typical feared-self direction).
 * All accumulators decay slowly toward zero per turn; unresolved
 * dissonance lingers but does not pile up indefinitely.
 */
#ifndef PE_DISSONANCE_H
#define PE_DISSONANCE_H

#include <stdint.h>
#include "sidecar.h"

#define PE_DISSONANCE_MAGIC   PE_SIDECAR_MAGIC('D','S','S','N')
#define PE_DISSONANCE_VERSION 1

typedef struct {
    pe_sidecar_header_t header;       /* 32 bytes */
    uint16_t ideal_gap;               /* 0..1000 — gap from ideal self */
    uint16_t ought_gap;               /* 0..1000 — gap from ought self */
    uint16_t feared_gap;              /* 0..1000 — proximity to feared self */
    uint16_t _pad;                    /* alignment */
    uint32_t last_decay_turn;         /* turn count at last decay tick */
    uint32_t _reserved[6];            /* zero today; future fields */
} pe_dissonance_t;

#ifdef __cplusplus
extern "C" {
#endif

void pe_dissonance_init(pe_dissonance_t *d);
int  pe_dissonance_load(pe_dissonance_t *d, const char *char_dir);
int  pe_dissonance_save(const pe_dissonance_t *d, const char *char_dir);

/* Phase 5b update: a speech event has just been committed. Apply the
 * proxy rules above. impact_pct is the rendered arousal/intensity 0..100
 * (use the input ev.arousal at call time — the character speaks more
 * intensely when their internal state is louder, so the dissonance
 * shifts harder). */
void pe_dissonance_update_from_speech(pe_dissonance_t *d,
                                      uint8_t speech_act,
                                      uint8_t impact_pct);

/* Per-turn decay toward zero. last_decay_turn tracks the last call so
 * idempotent within the same turn. Decay rate is conservative: 1 pt
 * per turn per accumulator, so unresolved dissonance from ~10 turns
 * ago is still mostly present. */
void pe_dissonance_decay_to(pe_dissonance_t *d, uint32_t turn_count);

#ifdef __cplusplus
}
#endif
#endif

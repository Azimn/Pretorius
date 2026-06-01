/* speech_ledger.h — V6 Phase 3: engine-authored speech events.
 *
 * The self-ledger is V6's autobiographical spine and the most important
 * new layer. Without it, the character can remember user facts but
 * cannot remember ITSELF. That is the difference between a clever puppet
 * and a character with continuity.
 *
 * Every rendered turn produces a structured speech event. Records are
 * small, fixed-size, packed structs — never prose. Stored in a ring
 * buffer sidecar `speech_events.bin`. Per V6_DOCTRINE §12, the renderer
 * audit's deterministic checks include realizing the logged speech act.
 *
 * Cross-tier note: the speech-event ring is engine-canonical and never
 * includes raw renderer prose. Only an `output_hash` (uint32) of the
 * rendered text is stored, so the firewall holds: the engine remembers
 * its own communicative acts (an accusation, a refusal, a withholding)
 * without ingesting model-authored facts about the world.
 */
#ifndef PE_SPEECH_LEDGER_H
#define PE_SPEECH_LEDGER_H

#include <stdint.h>
#include "sidecar.h"

#define PE_SPEECH_LEDGER_MAGIC     PE_SIDECAR_MAGIC('S','P','E','V')
#define PE_SPEECH_LEDGER_VERSION   1
#define PE_SPEECH_LEDGER_RING_SIZE 512u

/* ---------- speech-act enum (initial set, per V6_DOCTRINE §12) ---------- */
enum {
    PE_SA_NONE = 0,
    PE_SA_ASSERTION,
    PE_SA_QUESTION,
    PE_SA_REQUEST,
    PE_SA_COMMAND,
    PE_SA_APOLOGY,
    PE_SA_PRAISE,
    PE_SA_INSULT,
    PE_SA_THREAT,
    PE_SA_DISCLOSURE,
    PE_SA_REFUSAL,
    PE_SA_CORRECTION,
    PE_SA_GREETING,
    PE_SA_FAREWELL,
    PE_SA_EVASION,
    PE_SA_CONFESSION,
    PE_SA_CONCESSION,
    PE_SA_PROMISE,
    PE_SA_DEFLECTION,
    PE_SA_PAUSE,
    PE_SA_WITHDRAWAL,
    PE_SA_META_CONVERSATION,
    PE_SA_COUNT
};

/* ---------- withhold reasons (when withheld_intent != PE_SA_NONE) ---------- */
enum {
    PE_WR_NONE = 0,
    PE_WR_SHAME,
    PE_WR_PRIVACY,
    PE_WR_DISTRUST,
    PE_WR_TABOO,
    PE_WR_CONFUSION,
    PE_WR_FATIGUE,
    PE_WR_STRATEGY,
    PE_WR_COUNT
};

/* ---------- audit result ---------- */
enum {
    PE_AUDIT_PASS     = 0,   /* output passed audit on first try */
    PE_AUDIT_REPAIRED = 1,   /* failed first audit, repaired pass succeeded */
    PE_AUDIT_FALLBACK = 2    /* template fallback was used */
};

/* 48-byte packed speech event. Stored in a ring buffer.
 *
 * Field layout chosen for natural alignment and forward-compat reserved
 * padding; a future version bump can repurpose the reserved bytes
 * without breaking forward-incompatible readers (they refuse via
 * pe_sidecar_validate's TOO_NEW check). */
typedef struct {
    uint32_t turn_count;          /*  4: turn this happened on */
    uint64_t clock_ms;            /*  8: pinned engine-clock time of utterance */
    uint32_t target_actor_id;     /*  4: addressee (0 = soliloquy / dream) */
    uint32_t selected_memory_id;  /*  4: surfaced memory id (0 = none) */
    uint32_t output_hash;         /*  4: persona_hash of the rendered prose */
    uint16_t target_topic_id;     /*  2: topic addressed (0xFFFF = none) */
    uint16_t template_id;         /*  2: surface attribution: template */
    uint16_t render_id;           /*  2: surface attribution: render variant */
    uint16_t withheld_intent;     /*  2: PE_SA_* the character almost said */
    uint8_t  speech_act;          /*  1: PE_SA_* — what was said */
    uint8_t  response_act;        /*  1: PE_SA_* — what was being responded to */
    uint8_t  intent_id;           /*  1: PE_INTENT_* selected */
    uint8_t  stance;              /*  1: PE_STANCE_* */
    uint8_t  rhetorical_mode;     /*  1: PE_RHET_* */
    uint8_t  defense_mode;        /*  1: Phase 5 — 0 = none */
    uint8_t  repair_mode;         /*  1: Phase 5 — 0 = none */
    uint8_t  audit_result;        /*  1: PE_AUDIT_* */
    uint8_t  withhold_reason;     /*  1: PE_WR_* — why we withheld */
    uint8_t  regret_marker;       /*  1: set later if dissonance grows */
    uint8_t  _reserved[2];        /*  2: pad to 48 bytes */
} pe_speech_event_t;
/* size check: 4+8+4+4+4+2+2+2+2+1*10+2 = 48 */

typedef struct {
    pe_sidecar_header_t header;       /* 32 bytes */
    uint32_t head;                    /* ring head: next write index */
    uint32_t total_recorded;          /* monotonic count across rollovers */
    uint32_t _reserved[6];            /* zero today; reserved for expansion */
    pe_speech_event_t events[PE_SPEECH_LEDGER_RING_SIZE];
} pe_speech_ledger_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize an in-memory ledger with a valid header and empty ring. */
void pe_speech_ledger_init(pe_speech_ledger_t *led);

/* Load <char_dir>/speech_events.bin if present and valid; otherwise
 * leave the ledger in its initialized state. Returns 0 on success
 * (including the legitimate missing-file case), negative on path error. */
int  pe_speech_ledger_load(pe_speech_ledger_t *led, const char *char_dir);

/* Atomically write the ledger. */
int  pe_speech_ledger_save(const pe_speech_ledger_t *led, const char *char_dir);

/* Append one event to the ring. Always succeeds; oldest entry is
 * overwritten when the ring is full. Increments total_recorded. */
void pe_speech_ledger_record(pe_speech_ledger_t *led,
                             const pe_speech_event_t *ev);

/* Inspection helpers. */
uint32_t                  pe_speech_ledger_count(const pe_speech_ledger_t *led);
const pe_speech_event_t  *pe_speech_ledger_last (const pe_speech_ledger_t *led);

/* Engine-side conveniences: map a current PE_INTENT_* to a default
 * PE_SA_*, and a name lookup for state-trace / debug output. The
 * mapping is intentionally simple in Phase 3; Phase 5's speech-act
 * classifier will refine it from input + plan jointly. */
uint8_t      pe_speech_act_from_intent(uint16_t intent_id);
const char  *pe_speech_act_name(uint8_t sa);

/* Phase 5c: name lookup for the withhold-reason enum (PE_WR_*) for
 * state-JSON inspection. Returns "none" for PE_WR_NONE and "unknown"
 * for out-of-range values. */
const char  *pe_withhold_reason_name(uint8_t reason);

/* Phase 5c: returns nonzero if the speech act is a refusal-class
 * output (the character chose not to say something). Used by the
 * engine to decide whether to fill in withhold_reason for the turn. */
int          pe_speech_act_is_withhold(uint8_t sa);

#ifdef __cplusplus
}
#endif
#endif

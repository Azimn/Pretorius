/* schema_state.h — V4 compressed identity interpretations.
 *
 * Schemas are NOT memories.  They are beliefs derived from memories.
 * Humans operate primarily through compressed social abstractions:
 *   raw memories: "user lied", "user stole item", "user mocked craft"
 *   schema:       USER_UNTRUSTWORTHY (strength 0..1000)
 *
 * The renderer consumes schemas first, raw episodic memories second.
 * That's what makes the character's stance toward the user coherent
 * across long timescales — without it, a character forgives the user
 * the moment the AETHER cold scratch fails to surface the original
 * offending memory.
 *
 * Schemas are derived (Layer 1 internal): no external system may write
 * them directly.  Updates flow ONLY through schema_event_*() which
 * accepts symbolic events from the canonical update path in engine.c.
 */
#ifndef PERSONA_V4_SCHEMA_STATE_H
#define PERSONA_V4_SCHEMA_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Built-in schema slots.  These are the abstractions the engine
 * tracks per-relation.  Cartridges can add character-specific slots
 * later via the dialogue_mask channel; v4 ships with these eight. */
typedef enum {
    SCHEMA_USER_TRUSTWORTHY    = 0,   /* high → user is reliable */
    SCHEMA_USER_HOSTILE        = 1,   /* high → user has shown enmity */
    SCHEMA_USER_INTIMATE       = 2,   /* high → user is close */
    SCHEMA_USER_COMPETENT      = 3,   /* belief about user's skill */
    SCHEMA_USER_DECEPTIVE      = 4,   /* belief user is lying / hiding */
    SCHEMA_RELATIONSHIP_OWED   = 5,   /* user is owed (gratitude debt) */
    SCHEMA_RELATIONSHIP_OWES   = 6,   /* user owes (resentment) */
    SCHEMA_SELF_DIGNITY        = 7,   /* character's self-respect right now */
    SCHEMA_SLOT_COUNT          = 8
} SchemaSlot;

/* Symbolic events that update schemas.  These are NOT pattern matches
 * on raw text — they come from the engine's canonical interpretation
 * after pe_classify_input, pe_update_user_model, and friends. */
typedef enum {
    SCHEMA_EVT_PRAISED_US      = 0,
    SCHEMA_EVT_INSULTED_US     = 1,
    SCHEMA_EVT_THREATENED_US   = 2,
    SCHEMA_EVT_CONFIDED_IN_US  = 3,
    SCHEMA_EVT_LIED            = 4,
    SCHEMA_EVT_KEPT_PROMISE    = 5,
    SCHEMA_EVT_BROKE_PROMISE   = 6,
    SCHEMA_EVT_HELPED_US       = 7,
    SCHEMA_EVT_HURT_US         = 8,
    SCHEMA_EVT_APOLOGIZED      = 9,
    SCHEMA_EVT_COUNT           = 10
} SchemaEvent;

/* SchemaState = per-relation snapshot.  Lives next to the Relation
 * struct in memory/relations.c, persists in the per-interlocutor
 * relation file. */
typedef struct {
    uint32_t version;                     /* schema layout version */
    int16_t  slot[SCHEMA_SLOT_COUNT];     /* −1000..+1000 strength */
    int16_t  evidence[SCHEMA_SLOT_COUNT]; /* count of supporting events */
    uint32_t last_update_turn;            /* for habituation / decay */
    uint32_t turns_since_event[SCHEMA_EVT_COUNT];
    uint8_t  _pad[8];
} SchemaState;

/* Initialize a fresh SchemaState (all slots zeroed). */
void schema_state_init(SchemaState *s);

/* Apply a symbolic event with magnitude (0..255).  Strength updates
 * follow nonlinear curves: high-evidence schemas resist single
 * counterexamples (hysteresis), repeated identical events habituate
 * (diminishing returns), and trait amplifiers (paranoia, sentimentality)
 * scale specific slots.  See affect_curve.c for the math. */
void schema_apply_event(SchemaState *s, SchemaEvent evt, int magnitude,
                        const int16_t *trait_amplifiers /* [5] or NULL */);

/* Decay all schemas one turn forward.  Salience-weighted: slots with
 * high evidence decay slowly; SCHEMA_SELF_DIGNITY decays toward 0
 * faster than relational slots; trust-class slots are sticky. */
void schema_tick(SchemaState *s);

/* Read accessors — renderers consume these via RenderContext. */
int  schema_get(const SchemaState *s, SchemaSlot slot);
int  schema_evidence(const SchemaState *s, SchemaSlot slot);

/* Emit a one-line human-readable summary for instrumentation/debug. */
int  schema_format(const SchemaState *s, char *buf, int cap);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_V4_SCHEMA_STATE_H */

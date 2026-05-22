/* voice.c — v3.2: counterfactual rerank ("the Voice").
 *
 * Pipeline position:
 *
 *   pe_generate_response
 *     ├── build candidate_ids[] + candidate_scores[]   (existing)
 *     ├── pe_voice_rerank   ◀── this file              (NEW)
 *     ├── pick top-3, weighted-sample, return          (existing)
 *
 * For every candidate template the engine just scored, the Voice does a
 * 1-ply counterfactual: "if I said this — what would the speaker say
 * back?  And would that response advance my current goal?"
 *
 * - The intent of the candidate template is mapped to a likely
 *   (predicted_class, predicted_valence) the speaker will respond
 *   with.  Mirror-with-damping heuristic, biased by UserModel.
 *   (Same shape as pe_predict_next_input, but parameterised on a
 *   hypothetical utterance rather than the one just emitted.)
 *
 * - Each goal has a "preferred outcome shape" — a small (valence_pref,
 *   arousal_pref) vector indicating what kind of reply the goal is
 *   secretly hoping for.  Boasting wants praise; threatening wants the
 *   user shaken; withdrawing wants quiet.
 *
 * - Alignment = dot(predicted_reaction, goal_preference).  Scaled and
 *   added to candidate_scores[i].  The natural top-K pick that follows
 *   now reflects "what's the smart thing to say" not just
 *   "what fits my current mood."
 *
 * Determinism: pure function of (Engine state, candidates).  Never
 * mutates eng->state, eng->relation, or any persisted struct.  Safe to
 * replay with the same today_seed and produce byte-identical output.
 */
#include "persona.h"
#include "persona_internal.h"
#include <stddef.h>

/* ---------- predicted-reaction table ---------- *
 *
 * For each candidate intent, what (class, valence) does the speaker
 * likely respond with?  Same six classes the rest of the engine uses:
 *   0=neutral 1=praise 2=insult 3=question 4=threat 5=intimacy
 */
typedef struct {
    uint8_t class;       /* predicted next-input class */
    int8_t  valence;     /* predicted next-input valence (-100..+100) */
    int8_t  arousal;     /* predicted next-input arousal (0..100) */
} predicted_reaction_t;

static predicted_reaction_t reaction_for_intent(uint16_t intent){
    predicted_reaction_t r;
    r.class = 0; r.valence = 0; r.arousal = 30;
    switch (intent){
    case PE_INTENT_FLATTER:    r.class = 1; r.valence = +30; r.arousal = 25; break;
    case PE_INTENT_JOKE:       r.class = 1; r.valence = +20; r.arousal = 35; break;
    case PE_INTENT_BOAST:      r.class = 1; r.valence = +20; r.arousal = 30; break;
    case PE_INTENT_ACCUSE:     r.class = 4; r.valence = -40; r.arousal = 70; break;
    case PE_INTENT_THREATEN:   r.class = 4; r.valence = -50; r.arousal = 75; break;
    case PE_INTENT_PROBE:      r.class = 3; r.valence =   0; r.arousal = 40; break;
    case PE_INTENT_REMINISCE:  r.class = 5; r.valence = +15; r.arousal = 30; break;
    case PE_INTENT_MONOLOGUE:  r.class = 0; r.valence =  +5; r.arousal = 25; break;
    case PE_INTENT_WITHDRAW:   r.class = 0; r.valence = -10; r.arousal = 20; break;
    case PE_INTENT_EVADE:      r.class = 3; r.valence =  -5; r.arousal = 35; break;
    case PE_INTENT_REDIRECT:   r.class = 0; r.valence =   0; r.arousal = 30; break;
    case PE_INTENT_ANSWER:     r.class = 0; r.valence =  +5; r.arousal = 25; break;
    case PE_INTENT_INITIATE:   r.class = 3; r.valence =  +5; r.arousal = 45; break;
    case PE_INTENT_ATTEND:     r.class = 0; r.valence = +10; r.arousal = 20; break;
    case PE_INTENT_CLARIFY:    r.class = 3; r.valence =   0; r.arousal = 35; break;
    case PE_INTENT_PAUSE:      r.class = 0; r.valence = -10; r.arousal = 10; break;
    default: break;
    }
    return r;
}

/* ---------- goal preference table ----------
 *
 * What kind of speaker-reaction does each goal-intent secretly want?
 *   val_pref +1 → wants positive valence response
 *   aro_pref +1 → wants speaker stirred up
 *   aro_pref -1 → wants speaker quieted */
typedef struct { int8_t val_pref; int8_t aro_pref; } goal_pref_t;

static const goal_pref_t GOAL_PREF[PE_INTENT_COUNT] = {
    /* PE_INTENT_ANSWER    */ { +1,  0 },   /* wants comprehension / agreement */
    /* PE_INTENT_EVADE     */ {  0, -1 },   /* wants user to drop it */
    /* PE_INTENT_ACCUSE    */ { -1, +1 },   /* wants user defensive */
    /* PE_INTENT_FLATTER   */ { +1,  0 },   /* wants warmed reply */
    /* PE_INTENT_THREATEN  */ { -1, +1 },   /* wants user shaken */
    /* PE_INTENT_PROBE     */ {  0, +1 },   /* wants engagement */
    /* PE_INTENT_REDIRECT  */ {  0,  0 },   /* topic-driven, neutral */
    /* PE_INTENT_MONOLOGUE */ { +1, -1 },   /* wants attentive silence */
    /* PE_INTENT_REMINISCE */ { +1, -1 },   /* wants gentle attentiveness */
    /* PE_INTENT_WITHDRAW  */ {  0, -1 },   /* wants quiet */
    /* PE_INTENT_JOKE      */ { +1, +1 },   /* wants laughter */
    /* PE_INTENT_BOAST     */ { +1,  0 },   /* wants praise */
    /* PE_INTENT_INITIATE  */ {  0, +1 },   /* wants engagement */
    /* PE_INTENT_ATTEND    */ { +1, -1 },   /* wants user to continue safely */
    /* PE_INTENT_CLARIFY   */ {  0, +1 },   /* wants specificity */
    /* PE_INTENT_PAUSE     */ {  0, -1 },   /* wants quiet */
};

/* ---------- core rerank ----------
 *
 * Read-only over eng->state, eng->relation, eng->goals, eng->templates.
 * Mutates only eng->candidate_scores[] and a single instrumentation
 * field on eng->state (last_voice_delta) so :dump can show the choice.
 */
void pe_voice_rerank(Engine *eng){
    if (eng->candidate_count == 0) return;
    uint16_t goal_idx = eng->state.current_goal;
    if (goal_idx >= eng->goals.count) return;
    const GoalDef *g = &eng->goals.entries[goal_idx];
    if (g->intent_id >= PE_INTENT_COUNT) return;

    int8_t gv_pref = GOAL_PREF[g->intent_id].val_pref;
    int8_t ga_pref = GOAL_PREF[g->intent_id].aro_pref;

    /* UserModel polarity overrides: a hostile interlocutor will mirror
     * negatively no matter what we say.  Damp the optimism of any
     * candidate that assumes warmth. */
    int hostile = (eng->relation.um_belief_about_me < -40) ? 1 : 0;

    int32_t best_delta  = 0;
    uint16_t best_tid    = 0xFFFF;

    for (uint16_t i = 0; i < eng->candidate_count; ++i){
        uint16_t tid = eng->candidate_ids[i];
        if (tid >= eng->templates.count) continue;
        const Template *t = &eng->templates.entries[tid];

        predicted_reaction_t r = reaction_for_intent(t->intent);

        /* Hostile speaker collapses praise/intimacy predictions to
         * neutral/insult and damps positive valence. */
        if (hostile){
            if (r.class == 1 || r.class == 5){ r.class = 2; r.valence = -30; }
            if (r.valence > 0) r.valence /= 2;
        }

        /* Alignment: how well does this predicted reaction match the
         * goal's preferred response shape?  Arousal pref compares against
         * a conversational baseline of 50 so aro_pref=-1 rewards quieter-
         * than-average responses and aro_pref=+1 rewards livelier ones.
         * Range roughly -120..+120. */
        int32_t alignment = 0;
        alignment += (int32_t)r.valence * gv_pref;
        alignment += ((int32_t)r.arousal - 50) * ga_pref;

        /* Scale and apply.  /4 keeps the voice bonus in the same order
         * of magnitude as the existing template scoring bonuses (which
         * top out around ±250). */
        int32_t delta = alignment / 4;
        eng->candidate_scores[i] += delta;

        if (delta >  best_delta){ best_delta = delta;  best_tid = tid; }
        if (delta < -best_delta && best_delta > 0){ /* track magnitude */ }
    }

    /* Instrument: expose the highest-impact rerank for inspection. */
    eng->state.last_voice_delta = (int16_t)best_delta;
    eng->state.last_voice_choice = best_tid;
}

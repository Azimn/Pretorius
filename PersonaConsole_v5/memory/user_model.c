/* user_model.c — v3.0 Theory of Mind layer + predictive coding.
 *
 * Three small functions that together give Pretorius a model of his
 * interlocutor and a notion of being surprised:
 *
 *   pe_update_user_model    EMA-update of the UserModel from this turn's
 *                           input emotion + classification.
 *   pe_predict_next_input   At end-of-turn, predict the class & valence
 *                           of the input we expect next, given what
 *                           Pretorius just said and what we believe
 *                           about the speaker.
 *   pe_compute_surprise     At start-of-turn (after classify), compare
 *                           prediction to reality, write surprise_last
 *                           and feed the residual into acute_spike.
 *
 * All math is integer.  No allocations.  Deterministic.
 */
#include "persona.h"
#include "persona_internal.h"
#include <string.h>

/* ---------- helpers ---------- */
static int clamp_i(int v, int lo, int hi){
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Exponential moving average toward target with factor 1/4. */
static int8_t ema8(int8_t cur, int target){
    int v = ((int)cur * 3 + target) / 4;
    return (int8_t)clamp_i(v, -127, 127);
}
static uint8_t ema_u8(uint8_t cur, int target){
    int v = ((int)cur * 3 + target) / 4;
    return (uint8_t)clamp_i(v, 0, 255);
}

/* ---------- pe_update_user_model ---------- */
void pe_update_user_model(Engine *eng, const EmotionVector *ev){
    Relation *r = &eng->relation;
    int input_class = eng->input_class;
    int input_len   = (int)strlen(eng->lowered);

    /* track speaker's apparent affect via EMA */
    r->um_valence   = ema8(r->um_valence,   ev->valence);
    r->um_arousal   = ema8(r->um_arousal,   ev->arousal);
    r->um_dominance = ema8(r->um_dominance, ev->dominance);

    /* belief-about-me: input class signals what this person thinks of us */
    {
        int belief = (int)r->um_belief_about_me;
        switch (input_class){
        case 1: belief +=  8; break;   /* praise   */
        case 2: belief -= 12; break;   /* insult   */
        case 4: belief -= 16; break;   /* threat   */
        case 5: belief +=  4; break;   /* relent   */
        default: break;
        }
        /* negation flips the polarity: "you are not a fraud" → up not down */
        if (eng->negation_active && (input_class == 2 || input_class == 4))
            belief += 24;   /* undo the swing and tilt the other way */
        r->um_belief_about_me = (int8_t)clamp_i(belief, -128, 127);
    }

    /* knowledge level: long inputs with semicolons / colons / "shall"-class
     * words suggest an educated interlocutor.  Cheap proxy heuristics. */
    {
        int score = input_len * 2;
        if (strchr(eng->lowered, ';')) score += 30;
        if (strchr(eng->lowered, ':')) score += 20;
        if (strstr(eng->lowered, "shall"))     score += 15;
        if (strstr(eng->lowered, "indeed"))    score += 15;
        if (strstr(eng->lowered, "however"))   score += 10;
        if (strstr(eng->lowered, "therefore")) score += 15;
        r->um_knowledge_level = ema_u8(r->um_knowledge_level, clamp_i(score, 0, 255));
    }

    /* engagement: input length is the simplest proxy */
    {
        int e = input_len * 4;
        if (e > 255) e = 255;
        r->um_engagement = ema_u8(r->um_engagement, e);
    }

    r->um_last_intent  = (uint8_t)input_class;
    r->um_last_stance  = (uint8_t)eng->matched_group;
    if (eng->primary_topic != 0xFFFF) r->um_interest_topic = eng->primary_topic;
    if (r->um_update_count < 0xFFFFu) r->um_update_count++;
}

/* ---------- pe_predict_next_input ----------
 * Map (last rhetorical mode) → (most likely next input class, valence).
 * Then modulate by um_belief_about_me. */
void pe_predict_next_input(Engine *eng){
    NPCState *s = &eng->state;
    const Relation *r = &eng->relation;
    uint8_t predicted_class;
    int     predicted_val;

    switch (s->last_rhetorical_mode){
    case PE_RHET_INDICT:      predicted_class = 2; predicted_val = -60; break;
    case PE_RHET_ESCALATE:    predicted_class = 2; predicted_val = -50; break;
    case PE_RHET_GLOAT:       predicted_class = 2; predicted_val = -30; break;
    case PE_RHET_DEFLECT:     predicted_class = 3; predicted_val =   0; break;
    case PE_RHET_HEDGE:       predicted_class = 3; predicted_val =   0; break;
    case PE_RHET_ASSERT:      predicted_class = 3; predicted_val =  10; break;
    case PE_RHET_INTONE:      predicted_class = 0; predicted_val =   0; break;
    case PE_RHET_LAMENT:      predicted_class = 5; predicted_val =  20; break;
    case PE_RHET_ROMANTICIZE: predicted_class = 1; predicted_val =  40; break;
    case PE_RHET_CONFESS:     predicted_class = 5; predicted_val =  30; break;
    default:                  predicted_class = 0; predicted_val =   0; break;
    }

    /* belief modulation: a hostile interlocutor is more likely to insult
     * regardless of what we said; a friendly one tilts toward praise. */
    if (r->um_belief_about_me < -40 && predicted_class != 2){
        predicted_class = 2;
        predicted_val  -= 30;
    } else if (r->um_belief_about_me > 50 && predicted_class != 1){
        predicted_val  += 20;
    }

    s->predicted_input_class   = predicted_class;
    s->predicted_input_valence = (int8_t)clamp_i(predicted_val, -127, 127);
}

/* ---------- pe_compute_surprise ---------- */
void pe_compute_surprise(Engine *eng, const EmotionVector *ev){
    NPCState *s = &eng->state;
    int class_diff = (s->predicted_input_class != (uint8_t)eng->input_class);
    int val_diff   = (int)ev->valence - (int)s->predicted_input_valence;
    uint32_t surprise;

    /* On turn 0, prediction was never made — skip without writing acute_spike. */
    if (s->turn_count <= 1){
        s->surprise_last = 0;
        return;
    }

    if (val_diff < 0) val_diff = -val_diff;

    surprise  = class_diff ? 500 : 0;
    surprise += (uint32_t)val_diff * 2;
    if (surprise > 1000) surprise = 1000;
    s->surprise_last = (uint16_t)surprise;

    /* slow EMA of running surprise — feeds plan.hedging via pe_build_plan */
    s->prediction_error_accum = (uint16_t)
        (((uint32_t)s->prediction_error_accum * 3 + surprise) / 4);

    /* high surprise jolts acute_spike in the direction of the actual input */
    if (surprise > 500){
        int jolt = (int)surprise / 4;
        if (ev->valence < 0) jolt = -jolt;
        int v = (int)s->acute_spike + jolt;
        s->acute_spike = (int16_t)clamp_i(v, -1000, 1000);
    }
}

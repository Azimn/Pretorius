/* plan.c — v2 rhetorical planning layer.
 *
 * Inserted between intent selection and dialogue realization:
 *
 *   behavior (drives+mood+memory+input)
 *      |
 *      v
 *   goal arbitration → intent
 *      |
 *      v
 *   pe_build_plan(eng)  →  UtterancePlan
 *      |
 *      v
 *   pe_generate_response  (picks templates whose rhetorical_mask matches)
 *
 * The plan tells the renderer *how* to speak, not just *what about*.
 */
#include "persona.h"
#include "persona_internal.h"
#include <string.h>

/* map intent → primary rhetorical mode (bias, not lock) */
static uint16_t intent_to_mode(uint16_t intent){
    switch (intent){
    case PE_INTENT_ANSWER:    return PE_RHET_ASSERT;
    case PE_INTENT_EVADE:     return PE_RHET_DEFLECT;
    case PE_INTENT_ACCUSE:    return PE_RHET_INDICT;
    case PE_INTENT_FLATTER:   return PE_RHET_ROMANTICIZE;
    case PE_INTENT_THREATEN:  return PE_RHET_ESCALATE;
    case PE_INTENT_PROBE:     return PE_RHET_HEDGE;
    case PE_INTENT_REDIRECT:  return PE_RHET_DEFLECT;
    case PE_INTENT_MONOLOGUE: return PE_RHET_INTONE;
    case PE_INTENT_REMINISCE: return PE_RHET_LAMENT;
    case PE_INTENT_WITHDRAW:  return PE_RHET_LAMENT;
    case PE_INTENT_JOKE:      return PE_RHET_GLOAT;
    case PE_INTENT_BOAST:     return PE_RHET_GLOAT;
    default:                  return PE_RHET_ASSERT;
    }
}

static uint16_t pick_stance(const Engine *eng){
    /* dominant when vindication/autonomy strong; intimate when communion high & disposition>600;
     * defensive when paranoia/fragility high; condescending when recognition saturates;
     * conspiratorial when intoxicated + confidant. */
    const NPCState *s = &eng->state;
    int vind   = s->drive_values[PE_DRIVE_VINDICATION];
    int aut    = s->drive_values[PE_DRIVE_AUTONOMY];
    int com    = s->drive_values[PE_DRIVE_COMMUNION];
    int rec    = s->drive_values[PE_DRIVE_RECOGNITION];

    if (s->intoxication > 600 && (eng->relation.tags & PE_TAG_CONFIDANT)) return PE_STANCE_CONSPIRATORIAL;
    if (s->paranoia > 600 || s->physical_fragility > 700)                 return PE_STANCE_DEFENSIVE;
    if (vind > 800 || (aut > 800 && eng->input_class == 2))               return PE_STANCE_DOMINANT;
    if (rec > 850)                                                        return PE_STANCE_CONDESCENDING;
    if (com > 600 && eng->relation.disposition > 600)                     return PE_STANCE_INTIMATE;
    return PE_STANCE_NEUTRAL;
}

/* clamp helpers (byte) */
static inline uint8_t clmp_u8(int32_t v){ if (v<0) return 0; if (v>255) return 255; return (uint8_t)v; }

void pe_build_plan(Engine *eng){
    UtterancePlan *p = &eng->plan;
    memset(p, 0, sizeof(*p));

    /* base mode comes from intent — but layered affect can override */
    uint16_t mode = intent_to_mode(eng->state.current_intent);

    /* obsession lock overrides toward INTONE when fixation strong */
    if (eng->state.fixation_topic != 0xFFFF && eng->state.fixation_strength > 600
        && eng->state.fixation_remaining > 0){
        mode = PE_RHET_INTONE;
    }
    /* acute negative spike with high arousal → INDICT (regardless of intent) */
    if (eng->state.acute_spike < -400) mode = PE_RHET_INDICT;
    /* obsession_pressure dominates above 800 — gloat or intone, depending on mood */
    if (eng->state.obsession_pressure > 800 && eng->state.mood >= 0) mode = PE_RHET_GLOAT;

    /* negation in input → temper assertions toward HEDGE/CONFESS */
    if (eng->negation_active && mode == PE_RHET_ASSERT) mode = PE_RHET_HEDGE;

    p->rhetorical_mode = mode;
    p->stance          = pick_stance(eng);

    /* target topic: fixation > primary input topic > obsession pressure target */
    if (eng->state.fixation_topic != 0xFFFF && eng->state.fixation_strength > 400)
        p->target_topic = eng->state.fixation_topic;
    else if (eng->primary_topic != 0xFFFF)
        p->target_topic = eng->primary_topic;
    else {
        /* if obsession_pressure is high, pick a starved obsession topic */
        p->target_topic = 0xFFFF;
        if (eng->state.obsession_pressure > 500){
            for (int i = 0; i < PE_OBSESSION_COUNT; ++i){
                uint16_t o = eng->identity.obsessions[i];
                if (!o) break;
                p->target_topic = o; break;
            }
        }
    }

    /* callback memory: pick highest-match active memory if recall fired strongly */
    p->callback_memory = (eng->active_count > 0 && eng->active_match[0] > 600)
                       ? eng->active_memories[0] : 0xFFFF;

    /* certainty: high recognition + low paranoia + low negation = high
     * (also reduced by acute_spike volatility) */
    {
        int32_t c = 120
                  + (eng->state.drive_values[PE_DRIVE_RECOGNITION] / 8)
                  - (eng->state.paranoia / 8)
                  - (eng->state.acute_spike > 0 ? eng->state.acute_spike/16 : -eng->state.acute_spike/16)
                  - (eng->negation_active ? 60 : 0);
        p->certainty = clmp_u8(c);
    }

    /* verbosity: identity verbosity bits + intoxication boost + exhaustion penalty */
    {
        int32_t verb = PE_VF_GET_VERBOSITY(eng->identity.voice_flags) * 24;
        verb += eng->state.intoxication / 6;        /* drunk → talkative */
        verb -= eng->state.exhaustion / 8;           /* tired → curt */
        verb += eng->state.obsession_pressure / 12;  /* obsession → ramble */
        /* comfort shift: with a confidant, speech tightens (less needed). */
        if (eng->relation.tags & PE_TAG_CONFIDANT){
            verb -= 20;
            if (verb < 0) verb = 0;
        }
        p->verbosity = clmp_u8(verb);
    }

    /* aggression: vindication + irritation_carry + acute_spike (negative) */
    {
        int32_t agg = eng->state.drive_values[PE_DRIVE_VINDICATION] / 6
                    + eng->state.irritation_carry / 5
                    + (eng->state.acute_spike < 0 ? -eng->state.acute_spike / 6 : 0);
        /* suppression_mask hides it (high agreeableness fakes calm even when furious) */
        agg -= eng->state.suppression_mask / 4;
        p->aggression = clmp_u8(agg);
    }

    /* theatricality: extraversion + metaphor flag + intoxication + low fatigue */
    {
        int32_t th = (int32_t)eng->identity.extraversion / 1024;
        if (eng->identity.voice_flags & PE_VF_METAPHOR) th += 60;
        th += eng->state.intoxication / 8;
        th -= eng->state.exhaustion / 6;
        if (eng->state.fixation_strength > 500) th += 40;
        p->theatricality = clmp_u8(th);
    }

    /* hedging: openness + neuroticism + low certainty + negation
     * v3.0: also +prediction_error_accum/8 — when surprised a lot recently,
     * speakers become more cautious / hedging. */
    {
        int32_t hg = ((int32_t)eng->identity.openness     / 2048)
                   + ((int32_t)eng->identity.neuroticism  / 2048);
        if (eng->negation_active) hg += 50;
        if (p->certainty < 100) hg += (100 - p->certainty);
        hg += eng->state.prediction_error_accum / 8;
        /* comfort shift: confidants need no hedging — directness with trust. */
        if (eng->relation.tags & PE_TAG_CONFIDANT){
            hg -= 50;
            if (hg < 0) hg = 0;
        }
        p->hedging = clmp_u8(hg);
    }

    /* v3.0: Theory-of-Mind modulation.  The planner now reads the speaker
     * model and tilts stance / certainty accordingly.
     *
     *   - If they think me a fraud (um_belief_about_me < -40) → DEFENSIVE.
     *   - If they're highly educated (um_knowledge_level > 180) → bump
     *     certainty (Pretorius doesn't dumb himself down for peers).
     *   - If they seem disengaged (um_engagement < 60) → bias toward
     *     INTIMATE/CONFESS to re-engage, or LAMENT if mood is low.
     *   - Their belief never overrides an active fixation lock. */
    {
        const Relation *r = &eng->relation;
        int locked = (eng->state.fixation_topic != 0xFFFF
                   && eng->state.fixation_strength > 600);

        if (!locked){
            if (r->um_belief_about_me < -40)
                p->stance = PE_STANCE_DEFENSIVE;
            if (r->um_knowledge_level > 180){
                int32_t c = (int32_t)p->certainty + 30;
                p->certainty = clmp_u8(c);
            }
            if (r->um_engagement < 60 && r->um_update_count > 2){
                if (eng->state.mood < -100) p->rhetorical_mode = PE_RHET_LAMENT;
                else                        p->stance          = PE_STANCE_INTIMATE;
            }
        }
    }

    /* v3.0: high recent surprise — Pretorius visibly recalibrates.
     * Surprise > 600 nudges rhetorical mode toward HEDGE or CONFESS
     * depending on input valence. */
    if (eng->state.surprise_last > 600){
        if (eng->state.last_input_emotion.valence < 0)
            p->rhetorical_mode = PE_RHET_HEDGE;
        else
            p->rhetorical_mode = PE_RHET_CONFESS;
    }

    /* emotional objective: what feeling to project — biased by stance */
    {
        int32_t v;
        switch (p->stance){
        case PE_STANCE_DOMINANT:        v = -10; break;
        case PE_STANCE_INTIMATE:        v = +50; break;
        case PE_STANCE_DEFENSIVE:       v = -20; break;
        case PE_STANCE_CONDESCENDING:   v = +10; break;
        case PE_STANCE_CONSPIRATORIAL:  v = +40; break;
        default:                        v = eng->state.mood / 10; break;
        }
        if (v < -100) v = -100;
        if (v >  100) v =  100;
        p->emotional_objective = (uint16_t)(v + 200);   /* offset to fit uint16 */
    }

    p->negation_in_play = eng->negation_active ? 1u : 0u;

    /* expose to inspectable state */
    eng->state.last_rhetorical_mode = p->rhetorical_mode;
    eng->state.last_stance          = p->stance;
    eng->state.last_target_topic    = p->target_topic;
    eng->state.last_callback_memory = p->callback_memory;
    eng->state.last_certainty       = p->certainty;
    eng->state.last_verbosity       = p->verbosity;
    eng->state.last_aggression      = p->aggression;
    eng->state.last_theatricality   = p->theatricality;
    eng->state.last_hedging         = p->hedging;
    eng->state.last_negation        = p->negation_in_play;
}

/* vitality.c -- V7.2 compact homunculus vitality synthesis. */
#include "vitality.h"
#include "cartridge.h"
#include "persona_internal.h"
#include <stdio.h>
#include <string.h>

static void pe_vitality_copy(char *dst, size_t n, const char *src){
    if (!dst || n == 0) return;
    if (!src) src = "";
    snprintf(dst, n, "%s", src);
}

static const char *first_nonempty(char slots[PE_VITALITY_SLOT_COUNT][PE_VITALITY_TEXT_LEN]){
    for (uint8_t i = 0; i < PE_VITALITY_SLOT_COUNT; ++i)
        if (slots[i][0]) return slots[i];
    return "";
}

static const char *slot_pick(char slots[PE_VITALITY_SLOT_COUNT][PE_VITALITY_TEXT_LEN],
                             uint32_t seed){
    uint8_t count = 0;
    for (uint8_t i = 0; i < PE_VITALITY_SLOT_COUNT; ++i)
        if (slots[i][0]) ++count;
    if (!count) return "";
    seed %= count;
    for (uint8_t i = 0; i < PE_VITALITY_SLOT_COUNT; ++i){
        if (!slots[i][0]) continue;
        if (seed == 0) return slots[i];
        --seed;
    }
    return first_nonempty(slots);
}

static const char *intent_tactic(uint16_t intent){
    switch (intent){
    case PE_INTENT_PROBE: return "ask one grounded follow-up";
    case PE_INTENT_CLARIFY: return "ask for the missing shape before continuing";
    case PE_INTENT_REMINISCE: return "let memory color the present turn";
    case PE_INTENT_REDIRECT: return "acknowledge briefly, then redirect";
    case PE_INTENT_INITIATE: return "raise the character's own live thread";
    case PE_INTENT_WITHDRAW: return "withhold rather than overexplain";
    case PE_INTENT_PAUSE: return "let silence carry part of the answer";
    case PE_INTENT_THREATEN: return "press the boundary without helpdesk tone";
    case PE_INTENT_FLATTER: return "offer approval without becoming servile";
    default: return "answer directly in character";
    }
}

static const char *affect_posture(int16_t mood){
    if (mood > 350) return "energized";
    if (mood > 100) return "warm";
    if (mood < -350) return "wounded";
    if (mood < -100) return "guarded";
    return "steady";
}

static const char *relation_stance(const pe_relation_dims_t *rel){
    if (!rel) return "neutral";
    if (rel->resentment > 650) return "resentful";
    if (rel->threat > 650) return "watchful";
    if (rel->trust > 650 && rel->intimacy > 600) return "familiar";
    if (rel->admiration > 650) return "receptive";
    return "measured";
}

static const char *schema_climate_stance(const SchemaState *schema){
    if (!schema) return "";
    if (schema->slot[SCHEMA_USER_HOSTILE] > 550) return "expects hostility from repeated evidence";
    if (schema->slot[SCHEMA_USER_DECEPTIVE] > 550) return "tests the user's framing for deception";
    if (schema->slot[SCHEMA_USER_TRUSTWORTHY] > 550) return "expects reliability from accumulated evidence";
    if (schema->slot[SCHEMA_USER_INTIMATE] > 550) return "treats the exchange as familiar from accumulated evidence";
    if (schema->slot[SCHEMA_USER_COMPETENT] > 550) return "expects competence and can move faster";
    return "";
}

static const char *vitality_topic_label(const Engine *eng, uint16_t topic_id,
                                        char *fallback, size_t fallback_n){
    if (eng && topic_id != 0xFFFFu){
        for (uint32_t i = 0; i < eng->topics.count; ++i){
            if ((uint16_t)eng->topics.topics[i].id == topic_id &&
                eng->topics.topics[i].name[0])
                return eng->topics.topics[i].name;
        }
    }
    if (fallback && fallback_n)
        snprintf(fallback, fallback_n, "topic_%u", (unsigned)topic_id);
    return fallback ? fallback : "topic_unknown";
}

void pe_vitality_profile_init(VitalityProfile *vp){
    if (!vp) return;
    memset(vp, 0, sizeof(*vp));
    vp->version = PE_VITALITY_VERSION;
}

void pe_vitality_frame_init(VitalityFrame *vf){
    if (!vf) return;
    memset(vf, 0, sizeof(*vf));
    vf->neutral = 1u;
    pe_vitality_copy(vf->emotional_posture, sizeof(vf->emotional_posture), "steady");
    pe_vitality_copy(vf->social_stance, sizeof(vf->social_stance), "measured");
    pe_vitality_copy(vf->active_desire, sizeof(vf->active_desire), "continue the current exchange");
    pe_vitality_copy(vf->conversational_tactic, sizeof(vf->conversational_tactic), "answer directly in character");
    pe_vitality_copy(vf->unresolved_thread, sizeof(vf->unresolved_thread), "none");
    pe_vitality_copy(vf->style_anchor, sizeof(vf->style_anchor), "use the loaded cartridge voice without assistant phrasing");
    pe_vitality_copy(vf->memory_boundary, sizeof(vf->memory_boundary),
                     "memory facts are durable only when explicitly stored; expressive imagery is present-performance only");
    pe_vitality_copy(vf->avoid_generic, sizeof(vf->avoid_generic),
                     "avoid helpdesk phrasing and generic assistant deference");
}

int pe_vitality_load_optional(Engine *eng, const char *character_dir, int is_cart){
    char path[512];
    VitalityProfile tmp;
    if (!eng) return -1;
    pe_vitality_profile_init(&eng->vitality_profile);
    pe_vitality_frame_init(&eng->vitality_frame);
    memset(&tmp, 0, sizeof(tmp));
    if (is_cart){
        if (pe_cart_load_section(character_dir, "vitality.bin", &tmp, sizeof(tmp)) != 0)
            return 0;
    } else {
        if (!character_dir) return 0;
        if (pe_path_join(path, sizeof(path), character_dir, "vitality.bin") != 0)
            return 0;
        if (pe_read_file(path, &tmp, sizeof(tmp)) != 0)
            return 0;
    }
    if (tmp.version != PE_VITALITY_VERSION)
        return 0;
    eng->vitality_profile = tmp;
    eng->vitality_frame.neutral = 0u;
    return 1;
}

void pe_vitality_synthesize(Engine *eng){
    VitalityFrame *vf;
    VitalityProfile *vp;
    const char *stance, *move, *image, *palette;
    const pe_open_loop_t *loop;
    if (!eng) return;
    vf = &eng->vitality_frame;
    vp = &eng->vitality_profile;
    pe_vitality_frame_init(vf);
    vf->neutral = (vp->version != PE_VITALITY_VERSION ||
                   (!vp->authority_style[0] && !vp->metaphoric_domains[0] &&
                    !vp->social_stances[0][0] && !vp->rhetorical_moves[0][0]));

    stance = slot_pick(vp->social_stances, eng->state.today_seed + eng->state.turn_count);
    move = slot_pick(vp->rhetorical_moves, eng->state.rng_state + eng->state.turn_count);
    image = slot_pick(vp->recurring_images, eng->primary_topic + eng->state.turn_count);
    palette = slot_pick(vp->emotional_palette, (uint32_t)(eng->state.mood + 1000));

    pe_vitality_copy(vf->emotional_posture, sizeof(vf->emotional_posture),
                     palette[0] ? palette : affect_posture(eng->state.mood));
    {
        const char *climate = schema_climate_stance(&eng->schema);
        pe_vitality_copy(vf->social_stance, sizeof(vf->social_stance),
                         climate[0] ? climate :
                         (stance[0] ? stance : relation_stance(&eng->relation_dims)));
    }

    if (eng->state.current_intent == PE_INTENT_INITIATE){
        const char *pre = eng->identity.current_preoccupations[0];
        pe_vitality_copy(vf->active_desire, sizeof(vf->active_desire),
                         pre[0] ? pre : "raise an unfinished concern");
    } else if (eng->plan.target_topic != 0xFFFFu) {
        snprintf(vf->active_desire, sizeof(vf->active_desire),
                 "stay with topic %u", (unsigned)eng->plan.target_topic);
    } else {
        pe_vitality_copy(vf->active_desire, sizeof(vf->active_desire),
                         "continue the current exchange");
    }

    pe_vitality_copy(vf->conversational_tactic, sizeof(vf->conversational_tactic),
                     move[0] ? move : intent_tactic(eng->state.current_intent));

    loop = pe_open_loops_latest_for_actor(&eng->open_loops, eng->relation.user_hash);
    if (loop && pe_open_loop_pressure(loop, eng->state.turn_count) > 0){
        uint16_t pressure = pe_open_loop_pressure(loop, eng->state.turn_count);
        char topic_buf[32];
        const char *topic = vitality_topic_label(eng, loop->target_topic_id,
                                                 topic_buf, sizeof(topic_buf));
        const char *urgency = pressure >= 700u ? "high" :
                              pressure >= 350u ? "medium" : "low";
        snprintf(vf->unresolved_thread, sizeof(vf->unresolved_thread),
                 "active: %s urgency=%s",
                 topic, urgency);
    }

    if (vp->authority_style[0] || vp->metaphoric_domains[0] || image[0]){
        snprintf(vf->style_anchor, sizeof(vf->style_anchor),
                 "%s%s%s%s%s",
                 vp->authority_style[0] ? vp->authority_style : "use the cartridge voice",
                 vp->metaphoric_domains[0] ? "; domains: " : "",
                 vp->metaphoric_domains[0] ? vp->metaphoric_domains : "",
                 image[0] ? "; image: " : "",
                 image[0] ? image : "");
    }
    if (vp->memory_coloring_preferences[0])
        pe_vitality_copy(vf->memory_boundary, sizeof(vf->memory_boundary),
                         vp->memory_coloring_preferences);
    if (vp->forbidden_generic_phrases[0][0])
        pe_vitality_copy(vf->avoid_generic, sizeof(vf->avoid_generic),
                         first_nonempty(vp->forbidden_generic_phrases));
}

int pe_vitality_text_has_assistant_leak(const char *text){
    char lower[512];
    size_t i = 0;
    static const char *bad[] = {
        "sure, i can help with that",
        "here are some suggestions",
        "let me know if you want",
        "as an ai language model",
        "i'd be happy to help",
        "i can help you with that",
        "certainly!",
        "of course!",
        NULL
    };
    if (!text) return 0;
    for (; text[i] && i + 1u < sizeof(lower); ++i){
        unsigned char c = (unsigned char)text[i];
        lower[i] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    lower[i] = 0;
    for (i = 0; bad[i]; ++i)
        if (strstr(lower, bad[i])) return 1;
    return 0;
}

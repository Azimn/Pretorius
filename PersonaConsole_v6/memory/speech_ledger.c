/* speech_ledger.c — V6 Phase 3 implementation. See speech_ledger.h. */
#include "persona.h"            /* PE_INTENT_* enum */
#include "persona_internal.h"   /* pe_path_join / pe_read_file / pe_write_file_atomic */
#include "speech_ledger.h"

#include <string.h>

void pe_speech_ledger_init(pe_speech_ledger_t *led){
    if (!led) return;
    memset(led, 0, sizeof(*led));
    led->header.magic       = PE_SPEECH_LEDGER_MAGIC;
    led->header.version     = PE_SPEECH_LEDGER_VERSION;
    led->header.flags       = PE_SIDECAR_F_OPTIONAL;
    led->header.entry_count = 0;
    led->header.capacity    = PE_SPEECH_LEDGER_RING_SIZE;
}

int pe_speech_ledger_load(pe_speech_ledger_t *led, const char *char_dir){
    if (!led || !char_dir) return -1;
    pe_speech_ledger_init(led);

    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, "speech_events.bin") != 0)
        return -1;

    pe_speech_ledger_t tmp;
    if (pe_read_file(path, &tmp, sizeof(tmp)) != 0)
        return 0;  /* optional sidecar — missing is fine */

    int v = pe_sidecar_validate(&tmp.header, PE_SPEECH_LEDGER_MAGIC,
                                1, PE_SPEECH_LEDGER_VERSION);
    if (v != PE_SIDECAR_OK) {
        /* TOO_NEW (forward-incompatible) or corrupt — keep init defaults
         * rather than half-adopt a malformed file. */
        return 0;
    }
    /* Defensive: clamp head into range before adopting. */
    if (tmp.head >= PE_SPEECH_LEDGER_RING_SIZE) tmp.head = 0;
    *led = tmp;
    return 0;
}

int pe_speech_ledger_save(const pe_speech_ledger_t *led, const char *char_dir){
    if (!led || !char_dir) return -1;
    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, "speech_events.bin") != 0)
        return -1;
    return pe_write_file_atomic(path, led, sizeof(*led));
}

void pe_speech_ledger_record(pe_speech_ledger_t *led,
                             const pe_speech_event_t *ev){
    if (!led || !ev) return;
    if (led->header.magic != PE_SPEECH_LEDGER_MAGIC) pe_speech_ledger_init(led);

    uint32_t slot = led->head % PE_SPEECH_LEDGER_RING_SIZE;
    led->events[slot] = *ev;
    led->head = (led->head + 1u) % PE_SPEECH_LEDGER_RING_SIZE;

    if (led->total_recorded < 0xFFFFFFFFu) led->total_recorded++;
    if (led->header.entry_count < PE_SPEECH_LEDGER_RING_SIZE)
        led->header.entry_count++;
}

uint32_t pe_speech_ledger_count(const pe_speech_ledger_t *led){
    return led ? led->header.entry_count : 0u;
}

const pe_speech_event_t *pe_speech_ledger_last(const pe_speech_ledger_t *led){
    if (!led || led->header.entry_count == 0) return NULL;
    uint32_t last = (led->head + PE_SPEECH_LEDGER_RING_SIZE - 1u)
                  % PE_SPEECH_LEDGER_RING_SIZE;
    return &led->events[last];
}

static const pe_speech_event_t *ledger_scan_back(const pe_speech_ledger_t *led,
                                                 uint32_t actor_id,
                                                 uint16_t topic_id,
                                                 uint8_t speech_act,
                                                 int filter_actor,
                                                 int filter_topic,
                                                 int filter_act){
    if (!led || led->header.entry_count == 0) return NULL;
    uint32_t count = led->header.entry_count;
    if (count > PE_SPEECH_LEDGER_RING_SIZE) count = PE_SPEECH_LEDGER_RING_SIZE;
    for (uint32_t step = 0; step < count; ++step){
        uint32_t idx = (led->head + PE_SPEECH_LEDGER_RING_SIZE - 1u - step)
                     % PE_SPEECH_LEDGER_RING_SIZE;
        const pe_speech_event_t *ev = &led->events[idx];
        if (filter_actor && actor_id != 0 && ev->target_actor_id != actor_id) continue;
        if (filter_topic && topic_id != 0xFFFFu && ev->target_topic_id != topic_id) continue;
        if (filter_act && ev->speech_act != speech_act) continue;
        return ev;
    }
    return NULL;
}

const pe_speech_event_t *pe_speech_ledger_last_speech_act(const pe_speech_ledger_t *led,
                                                          uint8_t speech_act){
    return ledger_scan_back(led, 0, 0xFFFFu, speech_act, 0, 0, 1);
}

const pe_speech_event_t *pe_speech_ledger_last_by_actor_topic(const pe_speech_ledger_t *led,
                                                              uint32_t actor_id,
                                                              uint16_t topic_id,
                                                              uint8_t speech_act){
    return ledger_scan_back(led, actor_id, topic_id, speech_act, 1, 1, 1);
}

const pe_speech_event_t *pe_speech_ledger_last_refusal(const pe_speech_ledger_t *led){
    const pe_speech_event_t *best = NULL;
    const uint8_t acts[] = { PE_SA_REFUSAL, PE_SA_EVASION, PE_SA_DEFLECTION,
                             PE_SA_PAUSE, PE_SA_WITHDRAWAL };
    for (size_t i = 0; i < sizeof(acts)/sizeof(acts[0]); ++i){
        const pe_speech_event_t *ev = pe_speech_ledger_last_speech_act(led, acts[i]);
        if (ev && (!best || ev->turn_count > best->turn_count)) best = ev;
    }
    return best;
}

const pe_speech_event_t *pe_speech_ledger_last_promise(const pe_speech_ledger_t *led){
    return pe_speech_ledger_last_speech_act(led, PE_SA_PROMISE);
}

const pe_speech_event_t *pe_speech_ledger_last_apology(const pe_speech_ledger_t *led){
    return pe_speech_ledger_last_speech_act(led, PE_SA_APOLOGY);
}

const pe_speech_event_t *pe_speech_ledger_last_contradiction_candidate(const pe_speech_ledger_t *led,
                                                                       uint32_t actor_id,
                                                                       uint16_t topic_id){
    const pe_speech_event_t *best = NULL;
    const uint8_t acts[] = { PE_SA_ASSERTION, PE_SA_DISCLOSURE,
                             PE_SA_CONFESSION, PE_SA_CONCESSION };
    for (size_t i = 0; i < sizeof(acts)/sizeof(acts[0]); ++i){
        const pe_speech_event_t *ev =
            pe_speech_ledger_last_by_actor_topic(led, actor_id, topic_id, acts[i]);
        if (ev && (!best || ev->turn_count > best->turn_count)) best = ev;
    }
    return best;
}

int pe_speech_ledger_recent_repeated_act(const pe_speech_ledger_t *led,
                                         uint8_t speech_act,
                                         uint32_t window){
    const pe_speech_event_t *last = pe_speech_ledger_last_speech_act(led, speech_act);
    if (!last) return 0;
    uint32_t hits = 0;
    uint32_t count = led->header.entry_count;
    if (count > PE_SPEECH_LEDGER_RING_SIZE) count = PE_SPEECH_LEDGER_RING_SIZE;
    for (uint32_t step = 0; step < count; ++step){
        uint32_t idx = (led->head + PE_SPEECH_LEDGER_RING_SIZE - 1u - step)
                     % PE_SPEECH_LEDGER_RING_SIZE;
        const pe_speech_event_t *ev = &led->events[idx];
        if (last->turn_count > ev->turn_count + window) break;
        if (ev->speech_act == speech_act && ++hits >= 2u) return 1;
    }
    return 0;
}

/* Map an engine intent to a default speech act. Intentionally simple in
 * Phase 3; Phase 5's speech-act classifier will refine this from input
 * appraisal + plan jointly. The mapping is character-agnostic — no
 * cartridge-specific assumptions. */
uint8_t pe_speech_act_from_intent(uint16_t intent_id){
    switch (intent_id){
    case PE_INTENT_ANSWER:    return PE_SA_ASSERTION;
    case PE_INTENT_EVADE:     return PE_SA_EVASION;
    case PE_INTENT_ACCUSE:    return PE_SA_INSULT;
    case PE_INTENT_FLATTER:   return PE_SA_PRAISE;
    case PE_INTENT_THREATEN:  return PE_SA_THREAT;
    case PE_INTENT_PROBE:     return PE_SA_QUESTION;
    case PE_INTENT_REDIRECT:  return PE_SA_DEFLECTION;
    case PE_INTENT_MONOLOGUE: return PE_SA_ASSERTION;
    case PE_INTENT_REMINISCE: return PE_SA_DISCLOSURE;
    case PE_INTENT_WITHDRAW:  return PE_SA_WITHDRAWAL;
    case PE_INTENT_JOKE:      return PE_SA_ASSERTION;
    case PE_INTENT_BOAST:     return PE_SA_PRAISE;
    case PE_INTENT_INITIATE:  return PE_SA_QUESTION;
    case PE_INTENT_ATTEND:    return PE_SA_ASSERTION;
    case PE_INTENT_CLARIFY:   return PE_SA_QUESTION;
    case PE_INTENT_PAUSE:     return PE_SA_PAUSE;
    default:                  return PE_SA_ASSERTION;
    }
}

static const char *SA_NAMES[PE_SA_COUNT] = {
    "none","assertion","question","request","command","apology","praise",
    "insult","threat","disclosure","refusal","correction","greeting",
    "farewell","evasion","confession","concession","promise","deflection",
    "pause","withdrawal","meta_conversation"
};

const char *pe_speech_act_name(uint8_t sa){
    return (sa < PE_SA_COUNT) ? SA_NAMES[sa] : "unknown";
}

static const char *WR_NAMES[PE_WR_COUNT] = {
    "none","shame","privacy","distrust","taboo","confusion","fatigue","strategy"
};

const char *pe_withhold_reason_name(uint8_t reason){
    return (reason < PE_WR_COUNT) ? WR_NAMES[reason] : "unknown";
}

static const char *EXPR_NAMES[] = {
    "genuine","masked","withheld","redirected"
};

const char *pe_expression_policy_name(uint8_t policy){
    return (policy < (sizeof(EXPR_NAMES) / sizeof(EXPR_NAMES[0])))
         ? EXPR_NAMES[policy] : "unknown";
}

int pe_speech_act_is_withhold(uint8_t sa){
    switch (sa){
    case PE_SA_REFUSAL:
    case PE_SA_PAUSE:
    case PE_SA_EVASION:
    case PE_SA_WITHDRAWAL:
    case PE_SA_DEFLECTION:
        return 1;
    default:
        return 0;
    }
}

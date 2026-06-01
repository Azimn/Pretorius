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

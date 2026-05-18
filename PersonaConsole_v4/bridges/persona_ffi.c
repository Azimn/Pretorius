/* persona_ffi.c — opaque-handle wrappers around the PersonaConsole engine.
 *
 * Hides the Engine struct from external callers.  Allocates the engine on
 * the heap so the public handle is a true void*-style opaque pointer.
 * Heap allocation is fine here: it happens at session open/close, never
 * inside ps_reply.  The engine's "no malloc in process_input" discipline
 * is preserved.
 */
#include "persona_ffi.h"
#include "persona.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PersonaSession {
    Engine eng;
    char   user_id[64];
};

static const char *intent_name(uint16_t i){
    static const char *NAMES[] = {
        "answer","evade","accuse","flatter","threaten","probe",
        "redirect","monologue","reminisce","withdraw","joke","boast"
    };
    if (i < (sizeof(NAMES)/sizeof(NAMES[0]))) return NAMES[i];
    return "unknown";
}

static const char *rhet_name(uint16_t r){
    static const char *NAMES[] = {
        "assert","hedge","deflect","escalate","lament","gloat",
        "indict","romanticize","intone","confess"
    };
    if (r < (sizeof(NAMES)/sizeof(NAMES[0]))) return NAMES[r];
    return "unknown";
}

PersonaSession* ps_open(const char *cartridge_path){
    if (!cartridge_path) return NULL;
    PersonaSession *s = (PersonaSession*)calloc(1, sizeof(PersonaSession));
    if (!s) return NULL;
    if (persona_open(&s->eng, cartridge_path) != 0){
        free(s);
        return NULL;
    }
    snprintf(s->user_id, sizeof(s->user_id), "%s", "anon");
    persona_set_user(&s->eng, s->user_id);
    return s;
}

void ps_close(PersonaSession *s){
    if (!s) return;
    persona_save(&s->eng);
    persona_close(&s->eng);
    free(s);
}

int ps_set_user(PersonaSession *s, const char *user_id){
    if (!s || !user_id) return -1;
    snprintf(s->user_id, sizeof(s->user_id), "%s", user_id);
    return persona_set_user(&s->eng, s->user_id);
}

int ps_reply(PersonaSession *s, const char *input,
             char *out_buf, int out_buf_size){
    if (!s || !input || !out_buf || out_buf_size <= 0) return -1;
    return persona_process_input(&s->eng, s->user_id, input,
                                 out_buf, (size_t)out_buf_size);
}

int ps_save(PersonaSession *s){
    if (!s) return -1;
    return persona_save(&s->eng);
}

int ps_load(PersonaSession *s, const char *new_cart){
    if (!s || !new_cart) return -1;
    char saved_user[64];
    snprintf(saved_user, sizeof(saved_user), "%s", s->user_id);
    persona_save(&s->eng);
    persona_close(&s->eng);
    memset(&s->eng, 0, sizeof(s->eng));
    if (persona_open(&s->eng, new_cart) != 0) return -1;
    snprintf(s->user_id, sizeof(s->user_id), "%s", saved_user);
    persona_set_user(&s->eng, s->user_id);
    return 0;
}

int ps_name(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    return snprintf(out_buf, (size_t)out_buf_size, "%s",
                    s->eng.identity.character_name);
}

int ps_char_dir(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    return snprintf(out_buf, (size_t)out_buf_size, "%s", s->eng.char_dir);
}

int ps_state(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    const Engine *eng = &s->eng;
    const char *today_label = "";
    if (eng->state.today_index < eng->todays.count)
        today_label = eng->todays.entries[eng->state.today_index].label;

    int n = snprintf(out_buf, (size_t)out_buf_size,
        "{\"name\":\"%s\","
        "\"mood\":%d,"
        "\"intent\":\"%s\","
        "\"rhetorical_mode\":\"%s\","
        "\"today\":\"%s\","
        "\"turn_count\":%u,"
        "\"intoxication\":%d,"
        "\"exhaustion\":%d,"
        "\"acute_spike\":%d,"
        "\"obsession_pressure\":%d,"
        "\"voice_delta\":%d,"
        "\"voice_choice\":%u,"
        "\"disposition\":%d,"
        "\"user_id\":\"%s\","
        "\"schema\":{\"trustworthy\":%d,\"hostile\":%d,\"intimate\":%d,"
                    "\"competent\":%d,\"deceptive\":%d,\"owed\":%d,"
                    "\"owes\":%d,\"dignity\":%d}}",
        eng->identity.character_name,
        eng->state.mood,
        intent_name(eng->state.current_intent),
        rhet_name(eng->state.last_rhetorical_mode),
        today_label,
        eng->state.turn_count,
        eng->state.intoxication,
        eng->state.exhaustion,
        eng->state.acute_spike,
        eng->state.obsession_pressure,
        eng->state.last_voice_delta,
        eng->state.last_voice_choice,
        eng->relation.disposition,
        s->user_id,
        (int)eng->schema.slot[SCHEMA_USER_TRUSTWORTHY],
        (int)eng->schema.slot[SCHEMA_USER_HOSTILE],
        (int)eng->schema.slot[SCHEMA_USER_INTIMATE],
        (int)eng->schema.slot[SCHEMA_USER_COMPETENT],
        (int)eng->schema.slot[SCHEMA_USER_DECEPTIVE],
        (int)eng->schema.slot[SCHEMA_RELATIONSHIP_OWED],
        (int)eng->schema.slot[SCHEMA_RELATIONSHIP_OWES],
        (int)eng->schema.slot[SCHEMA_SELF_DIGNITY]);
    return n;
}

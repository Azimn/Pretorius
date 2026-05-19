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
    snprintf(s->user_id, sizeof(s->user_id), "%s", "Someone");
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

static const char *topic_name_by_id(const Engine *eng, uint16_t topic_id){
    if (topic_id == 0xFFFF) return "";
    for (uint32_t i = 0; i < eng->topics.count; ++i){
        if (eng->topics.topics[i].id == topic_id)
            return eng->topics.topics[i].name;
    }
    return "";
}

static const char *idle_topic_name(const Engine *eng){
    const char *planned = topic_name_by_id(eng, eng->state.last_target_topic);
    if (planned[0]) return planned;

    uint16_t best_topic = 0xFFFF;
    uint16_t best_momentum = 0;
    for (int i = 0; i < PE_TOPIC_SLOTS; ++i){
        const TopicState *ts = &eng->state.topic_momentum[i];
        if (ts->topic_id != 0xFFFF && ts->momentum > best_momentum){
            best_topic = ts->topic_id;
            best_momentum = ts->momentum;
        }
    }
    if (best_topic == 0xFFFF || best_momentum < 180) return "";
    return topic_name_by_id(eng, best_topic);
}

int ps_idle_probe(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    const Engine *eng = &s->eng;
    if (eng->state.turn_count == 0) {
        out_buf[0] = 0;
        return 0;
    }

    const char *topic = idle_topic_name(eng);
    const char *address = eng->identity.address_user_as[
        (eng->state.today_seed ^ eng->state.turn_count) % PE_ADDRESS_COUNT
    ];
    if (!address[0]) address = "my dear";

    const char *line = NULL;
    uint32_t seed = eng->state.today_seed
                  ^ (eng->state.turn_count * 2654435761u)
                  ^ ((uint32_t)eng->state.mood << 3);

    if (topic[0]) {
        if (!strcmp(topic, "gin")) {
            topic = "";
        }
    }

    if (topic[0]) {
        if (!strcmp(topic, "Henry")){
            const char *pool[] = {
                "You keep circling Henry. Do you pity him, or judge him?",
                "Before we leave Frankenstein in peace, tell me what you think he feared most.",
                "Henry remains in the room, even when unnamed. What would you ask him?"
            };
            line = pool[seed % (sizeof(pool)/sizeof(pool[0]))];
        } else if (!strcmp(topic, "creation") || !strcmp(topic, "the work")
                || !strcmp(topic, "science")){
            const char *pool[] = {
                "Hm. Would you like to hear about my work?",
                "Tell me what unsettles you most: the method, or the permission?",
                "You have been quiet around the work. Is that caution, or appetite?",
                "If creation could be made deliberate, what would you forbid first?"
            };
            line = pool[seed % (sizeof(pool)/sizeof(pool[0]))];
        } else if (!strcmp(topic, "loneliness")){
            const char *pool[] = {
                "When you say loneliness, do you mean absence, or being misunderstood in company?",
                "There is a particular silence after confession. Are you listening to it too?",
                "Solitude has returned to the table. Shall we dissect it?"
            };
            line = pool[seed % (sizeof(pool)/sizeof(pool[0]))];
        } else if (!strcmp(topic, "ethics") || !strcmp(topic, "God")){
            const char *pool[] = {
                "Is your objection moral, {address}, or merely nervous?",
                "You pause at the border of permission. What frightens you there?",
                "Say the forbidden part plainly. It improves the experiment."
            };
            line = pool[seed % (sizeof(pool)/sizeof(pool[0]))];
        }
    }

    if (!line) {
        if (eng->state.mood < -120) {
            const char *pool[] = {
                "You have gone quiet. Did I wound the thought, or sharpen it?",
                "Come now, {address}. Silence is useful only when it is preparing something.",
                "I can feel the conversation withdrawing. Shall we call it fear or fatigue?"
            };
            line = pool[seed % (sizeof(pool)/sizeof(pool[0]))];
        } else {
            const char *pool[] = {
                "Are you still there? Did I bore you to sleep?",
                "Hm. Would you like to hear about my work?",
                "Before the silence hardens, tell me what you make of all this.",
                "What thought are you refusing to say aloud?",
                "There is a question forming. Be brave enough to give it grammar."
            };
            line = pool[seed % (sizeof(pool)/sizeof(pool[0]))];
        }
    }

    char tmp[PE_TEMPLATE_TEXT];
    const char *slot = strstr(line, "{address}");
    if (slot) {
        size_t head = (size_t)(slot - line);
        snprintf(tmp, sizeof(tmp), "%.*s%s%s",
                 (int)head, line, address, slot + 9);
        line = tmp;
    }
    return snprintf(out_buf, (size_t)out_buf_size, "%s", line);
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

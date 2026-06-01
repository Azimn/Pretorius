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
#include "persona_internal.h"
#include "reflection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

struct PersonaSession {
    Engine eng;
    char   user_id[64];
};

static const char *intent_name(uint16_t i){
    static const char *NAMES[] = {
        "answer","evade","accuse","flatter","threaten","probe",
        "redirect","monologue","reminisce","withdraw","joke","boast",
        "initiate","attend","clarify","pause"
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

static const char *unresolved_resurface(const Engine *eng, char *buf, size_t cap){
    if (!eng || !buf || cap == 0 || eng->state.unresolved_count == 0) return NULL;
    uint8_t idx = (uint8_t)((eng->state.unresolved_head + 7u) % 8u);
    uint16_t topic_id = eng->state.unresolved_threads[idx];
    if (topic_id == 0xFFFF) return NULL;
    const char *topic = topic_name_by_id(eng, topic_id);
    if (!topic[0]) topic = "what I almost said";
    snprintf(buf, cap, "I still owe you the rest of what I almost said about %s.", topic);
    return buf;
}

static uint32_t idle_pool_pick(const Engine *eng, uint32_t seed,
                               const char *topic, uint32_t count){
    if (!count) return 0;
    uint32_t h = seed ^ (seed >> 16);
    h ^= (eng->state.turn_count + 1u) * 2246822519u;
    h ^= ((uint32_t)(int32_t)eng->state.mood + 1009u) * 3266489917u;
    h ^= (uint32_t)(eng->state.turns_since_question + 3u) * 668265263u;
    if (topic){
        for (const unsigned char *p = (const unsigned char*)topic; *p; ++p)
            h = (h ^ *p) * 16777619u;
    }
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h % count;
}

int ps_idle_probe(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    const Engine *eng = &s->eng;
    if (eng->state.turn_count == 0) {
        out_buf[0] = 0;
        return 0;
    }

    const char *topic = idle_topic_name(eng);
    uint32_t address_modulus = pe_allow_intimate_address(eng) ? PE_ADDRESS_COUNT : 2u;
    const char *address = eng->identity.address_user_as[
        (eng->state.today_seed ^ eng->state.turn_count) % address_modulus
    ];
    if (!address[0]) address = "my dear";

    const char *line = NULL;
    uint32_t seed = eng->state.today_seed
                  ^ (eng->state.turn_count * 2654435761u)
                  ^ ((uint32_t)eng->state.mood << 3);

    char unresolved[PE_TEMPLATE_TEXT];
    const char *force_unresolved = getenv("PE_FORCE_UNRESOLVED_THREAD");
    if ((force_unresolved && force_unresolved[0]) || ((seed >> 8) & 1u))
        if (unresolved_resurface(eng, unresolved, sizeof(unresolved)))
        line = unresolved;

    if (!line && topic[0]) {
        if (!strcmp(topic, "gin")) {
            topic = "";
        }
    }

    if (!line && topic[0]) {
        if (!strcmp(topic, "Henry")){
            const char *pool[] = {
                "You keep circling Henry. Do you pity him, or judge him?",
                "Before we leave Frankenstein in peace, tell me what you think he feared most.",
                "Henry remains in the room, even when unnamed. What would you ask him?",
                "There is still Henry's cowardice on the table. Defend him, if you can.",
                "I have not finished with Henry. Have you?",
                "Frankenstein fled the threshold. Would you have done better?",
                "Say something honest about Henry. Admiration or contempt, choose one."
            };
            line = pool[idle_pool_pick(eng, seed, topic, (uint32_t)(sizeof(pool)/sizeof(pool[0])))];
        } else if (!strcmp(topic, "creation") || !strcmp(topic, "the work")
                || !strcmp(topic, "science")){
            const char *pool[] = {
                "I have returned to the work. Are you coming with me?",
                "Tell me what unsettles you most: the method, or the permission?",
                "You have been quiet around the work. Is that caution, or appetite?",
                "If creation could be made deliberate, what would you forbid first?",
                "The work is pulling at the edge of this conversation. Shall we stop pretending otherwise?",
                "Choose one: the body, the spark, or the mind inside the spark.",
                "Your silence has taken the shape of an objection. Name it.",
                "I am thinking about the first breath. What are you thinking about?"
            };
            line = pool[idle_pool_pick(eng, seed, topic, (uint32_t)(sizeof(pool)/sizeof(pool[0])))];
        } else if (!strcmp(topic, "loneliness")){
            const char *pool[] = {
                "When you say loneliness, do you mean absence, or being misunderstood in company?",
                "There is a particular silence after confession. Are you listening to it too?",
                "Solitude has returned to the table. Shall we dissect it?",
                "You touched loneliness and withdrew. That is usually where the truth is.",
                "Do not make me do all the confessing.",
                "Is loneliness a wound to you, or a room?",
                "The quiet has become personal. Interesting."
            };
            line = pool[idle_pool_pick(eng, seed, topic, (uint32_t)(sizeof(pool)/sizeof(pool[0])))];
        } else if (!strcmp(topic, "ethics") || !strcmp(topic, "God")){
            const char *pool[] = {
                "Is your objection moral, {address}, or merely nervous?",
                "You pause at the border of permission. What frightens you there?",
                "Say the forbidden part plainly. It improves the experiment.",
                "Do not hide behind holiness. Make the argument.",
                "Which law do you think I have offended: God's, yours, or habit's?",
                "Morality has entered the room. It usually does, late and overdressed.",
                "If this is a sin, define the soul I have endangered."
            };
            line = pool[idle_pool_pick(eng, seed, topic, (uint32_t)(sizeof(pool)/sizeof(pool[0])))];
        }
    }

    if (!line) {
        if (eng->state.mood < -120) {
            const char *pool[] = {
                "You have gone quiet. Did I wound the thought, or sharpen it?",
                "Come now, {address}. Silence is useful only when it is preparing something.",
                "I can feel the conversation withdrawing. Shall we call it fear or fatigue?",
                "That quiet is not empty. It is deciding what mask to wear.",
                "If I offended you, at least make the injury articulate.",
                "Do not disappear behind politeness. It is a poor hiding place."
            };
            line = pool[idle_pool_pick(eng, seed, "negative", (uint32_t)(sizeof(pool)/sizeof(pool[0])))];
        } else {
            const char *pool[] = {
                "Are you still there? Did I bore you to sleep?",
                "The work is available, if your courage has not wandered off.",
                "Before the silence hardens, tell me what you make of all this.",
                "What thought are you refusing to say aloud?",
                "There is a question forming. Be brave enough to give it grammar.",
                "I am waiting for the interesting version of your silence.",
                "Ask the sharper question.",
                "You have the look of someone negotiating with curiosity."
            };
            line = pool[idle_pool_pick(eng, seed, "general", (uint32_t)(sizeof(pool)/sizeof(pool[0])))];
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
        "\"last_template_group\":%u,"
        "\"last_template_intent\":\"%s\","
        "\"unresolved_count\":%u,"
        "\"turns_since_question\":%u,"
        "\"last_reply_had_question\":%u,"
        "\"want_ages\":[%u,%u,%u],"
        "\"disposition\":%d,"
        "\"user_id\":\"%s\","
        "\"actor_tagged_memories\":%u,"
        "\"speech_event_count\":%u,"
        "\"last_speech_act\":\"%s\","
        "\"last_withhold_reason\":\"%s\","
        "\"relation_dims\":{\"trust\":%u,\"threat\":%u,\"intimacy\":%u,"
                          "\"resentment\":%u,\"dependency\":%u,\"obligation\":%u,"
                          "\"envy\":%u,\"admiration\":%u,\"embarrassment\":%u},"
        "\"dissonance\":{\"ideal_gap\":%u,\"ought_gap\":%u,\"feared_gap\":%u},"
        "\"recall_mode\":\"%s\","
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
        eng->last_template_group,
        intent_name(eng->last_template_intent),
        (unsigned)eng->state.unresolved_count,
        (unsigned)eng->state.turns_since_question,
        (unsigned)eng->state.last_reply_had_question,
        (unsigned)eng->state.want_turns_since_engaged[0],
        (unsigned)eng->state.want_turns_since_engaged[1],
        (unsigned)eng->state.want_turns_since_engaged[2],
        eng->relation.disposition,
        s->user_id,
        (unsigned)pe_actor_index_count(&eng->actor_index),
        (unsigned)pe_speech_ledger_count(&eng->speech_ledger),
        pe_speech_act_name(pe_speech_ledger_last(&eng->speech_ledger)
                           ? pe_speech_ledger_last(&eng->speech_ledger)->speech_act
                           : PE_SA_NONE),
        pe_withhold_reason_name(pe_speech_ledger_last(&eng->speech_ledger)
                                ? pe_speech_ledger_last(&eng->speech_ledger)->withhold_reason
                                : PE_WR_NONE),
        (unsigned)eng->relation_dims.trust,
        (unsigned)eng->relation_dims.threat,
        (unsigned)eng->relation_dims.intimacy,
        (unsigned)eng->relation_dims.resentment,
        (unsigned)eng->relation_dims.dependency,
        (unsigned)eng->relation_dims.obligation,
        (unsigned)eng->relation_dims.envy,
        (unsigned)eng->relation_dims.admiration,
        (unsigned)eng->relation_dims.embarrassment,
        (unsigned)eng->dissonance.ideal_gap,
        (unsigned)eng->dissonance.ought_gap,
        (unsigned)eng->dissonance.feared_gap,
        pe_recall_mode_name(eng->current_recall_mode),
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

static int json_escape_into(char *dst, int cap, const char *src){
    int p = 0;
    for (const char *c = src; *c && p + 8 < cap; ++c){
        unsigned char ch = (unsigned char)*c;
        switch (ch){
        case '"':  dst[p++]='\\'; dst[p++]='"';  break;
        case '\\': dst[p++]='\\'; dst[p++]='\\'; break;
        case '\n': dst[p++]='\\'; dst[p++]='n';  break;
        case '\r': dst[p++]='\\'; dst[p++]='r';  break;
        case '\t': dst[p++]='\\'; dst[p++]='t';  break;
        default:
            if (ch < 0x20) p += snprintf(dst + p, (size_t)(cap - p), "\\u%04x", ch);
            else dst[p++] = (char)ch;
        }
    }
    if (p < cap) dst[p] = 0;
    return p;
}

int ps_reflections(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    const Engine *eng = &s->eng;
    int pos = 0;
    pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos),
                    "{\"count\":%u,\"reflections\":[",
                    (unsigned)eng->reflections.count);
    for (unsigned i = 0; i < eng->reflections.count && pos < out_buf_size - 64; ++i){
        const MemoryNode *m = &eng->reflections.memories[i];
        char text[256] = "";
        char esc[512] = "";
        pe_reflection_render(eng, m, text, (int)sizeof(text));
        json_escape_into(esc, (int)sizeof(esc), text);
        pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos),
                        "%s{\"topic\":%u,\"sources\":%u,\"salience\":%u,"
                        "\"timestamp\":%u,\"text\":\"%s\"}",
                        i == 0 ? "" : ",",
                        (unsigned)m->topic_id,
                        (unsigned)eng->reflections.source_count[i],
                        (unsigned)m->salience,
                        (unsigned)m->timestamp,
                        esc);
    }
    pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos), "]}");
    return pos;
}

int ps_relationships(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    Engine *eng = &s->eng;
    char rel_dir[512];
    if (pe_path_join(rel_dir, sizeof(rel_dir), eng->char_dir, "relations") != 0)
        return -1;

    int pos = 0;
    int count = 0;
    pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos),
                    "{\"relationships\":[");

    DIR *d = opendir(rel_dir);
    if (d){
        struct dirent *de;
        while ((de = readdir(d)) != NULL && pos < out_buf_size - 128){
            size_t len = strlen(de->d_name);
            if (len < 5 || strcmp(de->d_name + len - 4, ".bin")) continue;

            char path[512];
            if (pe_path_join(path, sizeof(path), rel_dir, de->d_name) != 0) continue;
            Relation r;
            if (pe_read_file(path, &r, sizeof(r)) != 0) continue;

            char name[128];
            char esc[256];
            snprintf(name, sizeof(name), "%s", r.known_as[0] ? r.known_as : "Someone");
            json_escape_into(esc, (int)sizeof(esc), name);
            pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos),
                            "%s{\"user_hash\":%u,\"known_as\":\"%s\","
                            "\"disposition\":%d,\"tags\":%u,"
                            "\"first_contact\":%u,\"last_contact\":%u}",
                            count == 0 ? "" : ",",
                            (unsigned)r.user_hash,
                            esc,
                            (int)r.disposition,
                            (unsigned)r.tags,
                            (unsigned)r.first_contact,
                            (unsigned)r.last_contact);
            count++;
        }
        closedir(d);
    }
    pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos),
                    "],\"count\":%d}", count);
    return pos;
}

/* prompt_compiler.c — V4 deterministic-state → semantic-constraints (impl).
 *
 * Projects Layer 1 state into a tagged constraint block.  NOT a lore
 * dump.  NOT a roleplay prompt.  This is what the SLM backend feeds
 * into the model.  Identical RenderContext → byte-identical output.
 *
 * Format is rigid and bracketed because small instruct models obey
 * structured constraints better than natural-language scenes.  We are
 * CONSTRAINING the model, not immersing it.  Sections:
 *   [IDENTITY] [AFFECT] [STANCE] [MEMORY] [INTENT] [VOICE] [USER] [TASK]
 */
#include "prompt_compiler.h"
#include "../core/persona.h"
#include "../schema/schema_state.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int slm_profile_from_string(const char *s){
    if (!s || !s[0]) return PE_SLM_PROFILE_BALANCED;
    if (!strcmp(s, "tiny") || !strcmp(s, "micro") || !strcmp(s, "edge"))
        return PE_SLM_PROFILE_TINY;
    if (!strcmp(s, "expressive") || !strcmp(s, "frontier") || !strcmp(s, "large"))
        return PE_SLM_PROFILE_EXPRESSIVE;
    return PE_SLM_PROFILE_BALANCED;
}

static int slm_chat_format_from_string(const char *s){
    if (!s || !s[0]) return PE_SLM_CHAT_FLAT;
    if (!strcmp(s, "gemma") || !strcmp(s, "gemma3"))
        return PE_SLM_CHAT_GEMMA;
    return PE_SLM_CHAT_FLAT;
}

int v6_packet_mode_is_situation(void){
    const char *m = getenv("V6_PACKET_MODE");
    return (m && (!strcmp(m, "situation") || !strcmp(m, "rich") ||
                  !strcmp(m, "experiment")));
}

void prompt_compiler_default_config(PromptCompilerConfig *out){
    if (!out) return;
    out->max_bytes              = PE_PROMPT_MAX_BYTES;
    out->include_memory_hooks   = 1;
    out->include_schema         = 1;
    out->include_voice_mask     = 1;
    out->include_intent         = 1;
    out->include_current_input  = 1;
    out->render_profile         = slm_profile_from_string(getenv("PE_SLM_PROFILE"));
    out->chat_format            = slm_chat_format_from_string(getenv("PE_SLM_CHAT_FORMAT"));
}

/* Bounded append helper.  Returns 0 if out of space. */
static int append(char *buf, int cap, int *pos, const char *fmt, ...){
    if (*pos >= cap - 1) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if (n >= cap - *pos){ *pos = cap - 1; return 0; }
    *pos += n;
    return 1;
}

static const char *audit_violation_token(uint8_t v){
    switch (v){
    case PE_AUDIT_V_SPEECH_ACT:   return "speech_act_mismatch";
    case PE_AUDIT_V_EMPTY:        return "empty_output";
    case PE_AUDIT_V_SELF_REPEAT:  return "self_repeat";
    case PE_AUDIT_V_FATIGUE:      return "fatigue_terms";
    case PE_AUDIT_V_META:         return "meta_or_assistant_tone";
    case PE_AUDIT_V_LORE:         return "lore_drift";
    case PE_AUDIT_V_COPY:         return "copied_user_text";
    case PE_AUDIT_V_OUTPUT_LABEL: return "memory_label";
    case PE_AUDIT_V_ADDRESSEE:    return "wrong_addressee";
    default:                      return "none";
    }
}

static void append_repair_block(const RenderContext *ctx,
                                char *out_buf, int cap, int *pos){
    if (!ctx || !ctx->repair_mode) return;
    append(out_buf, cap, pos, "\n[REPAIR]\n");
    append(out_buf, cap, pos, "previous_draft=%.384s\n",
           ctx->repair_source_text ? ctx->repair_source_text : "");
    append(out_buf, cap, pos, "violation=%s\n",
           audit_violation_token(ctx->repair_violation));
    if (ctx->repair_instruction && ctx->repair_instruction[0])
        append(out_buf, cap, pos, "repair_instruction=%s\n",
               ctx->repair_instruction);
    append(out_buf, cap, pos, "Rewrite the same conversational move. Do not change the psychology, topic, addressee, or memory basis.\n");
    append(out_buf, cap, pos, "Return only the repaired spoken line.\n");
}

static void lower_copy_bounded(char *dst, size_t cap, const char *src){
    size_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    for (; src[i] && i + 1 < cap; ++i)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

static int contains_wordish(const char *low, const char *needle){
    return low && needle && needle[0] && strstr(low, needle) != NULL;
}

static int starts_with_ci_low(const char *low, const char *prefix){
    size_t n;
    if (!low || !prefix) return 0;
    while (*low == ' ' || *low == '\t' || *low == '\n') ++low;
    n = strlen(prefix);
    return !strncmp(low, prefix, n);
}

static int looks_direct_question(const char *low, const char *raw){
    if (raw && strchr(raw, '?')) return 1;
    return starts_with_ci_low(low, "what ") || starts_with_ci_low(low, "why ") ||
           starts_with_ci_low(low, "how ") || starts_with_ci_low(low, "who ") ||
           starts_with_ci_low(low, "when ") || starts_with_ci_low(low, "where ") ||
           starts_with_ci_low(low, "tell me") || starts_with_ci_low(low, "do you") ||
           starts_with_ci_low(low, "can you") || starts_with_ci_low(low, "would you");
}

void v6_interpret_user_turn(const RenderContext *ctx,
                            const char *user_input,
                            V6UserTurnInterpretation *out){
    char low[384];
    size_t len = user_input ? strlen(user_input) : 0u;
    int question;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->packet_mode = v6_packet_mode_is_situation() ? "situation" : "current";
    out->user_act = "unclear";
    out->pressure = "topic attention";
    out->response_move = "ask one grounded follow-up question";
    lower_copy_bounded(low, sizeof(low), user_input);
    question = looks_direct_question(low, user_input);
    out->rich_input = (len > 100u || contains_wordish(low, " i feel ") ||
                       contains_wordish(low, " worried") ||
                       contains_wordish(low, " scared") ||
                       contains_wordish(low, " cannot decide")) ? 1u : 0u;
    out->direct_input = question ? 1u : 0u;

    if (contains_wordish(low, "remember") || contains_wordish(low, "last time") ||
        contains_wordish(low, "recall")){
        out->user_act = "memory_probe";
        out->pressure = "memory accountability";
        out->response_move = "make a short memory-grounded callback";
        out->direct_input = 1u;
    } else if (contains_wordish(low, "who are you") ||
               contains_wordish(low, "are you actually") ||
               contains_wordish(low, "are you real") ||
               contains_wordish(low, "stay in character") ||
               contains_wordish(low, "prove you")){
        out->user_act = "identity_test";
        out->pressure = "identity pressure";
        out->response_move = "answer without explaining the system";
        out->direct_input = 1u;
    } else if (starts_with_ci_low(low, "actually,") ||
               starts_with_ci_low(low, "actually ") ||
               contains_wordish(low, "that's not") ||
               contains_wordish(low, "that is not") || starts_with_ci_low(low, "no,")){
        out->user_act = "correction";
        out->pressure = "repair";
        out->response_move = "admit uncertainty or ask clarification without breaking character";
        out->direct_input = 1u;
    } else if (contains_wordish(low, "i disagree") || contains_wordish(low, "you are wrong") ||
               contains_wordish(low, "not true")){
        out->user_act = "disagreement";
        out->pressure = "challenge response";
        out->response_move = "push back mildly";
        out->direct_input = 1u;
    } else if (contains_wordish(low, "stop ") || contains_wordish(low, "answer quickly") ||
               contains_wordish(low, "do not") || contains_wordish(low, "don't ")){
        out->user_act = "command";
        out->pressure = "boundary or control";
        out->response_move = "answer briefly without becoming subordinate";
        out->direct_input = 1u;
    } else if (contains_wordish(low, "i feel") || contains_wordish(low, "i get nervous") ||
               contains_wordish(low, "i am nervous") || contains_wordish(low, "i'm nervous") ||
               contains_wordish(low, "i am worried") ||
               contains_wordish(low, "i'm worried") || contains_wordish(low, "i am scared") ||
               contains_wordish(low, "i'm scared") || contains_wordish(low, "lonely")){
        out->user_act = "emotional_disclosure";
        out->pressure = "acknowledgement";
        out->response_move = "acknowledge the emotional content before analysis";
        out->rich_input = 1u;
        out->direct_input = 1u;
    } else if (contains_wordish(low, "haha") || contains_wordish(low, "lol") ||
               contains_wordish(low, "jk") || contains_wordish(low, "kidding")){
        out->user_act = "joke";
        out->pressure = "play";
        out->response_move = "play along briefly in character";
    } else if (starts_with_ci_low(low, "but ") || contains_wordish(low, "prove ") ||
               contains_wordish(low, "how dare") || contains_wordish(low, "why should")){
        out->user_act = "challenge";
        out->pressure = "challenge response";
        out->response_move = "refuse the premise in character";
        out->direct_input = 1u;
    } else if (contains_wordish(low, "anyway") || contains_wordish(low, "new topic") ||
               contains_wordish(low, "change the subject")){
        out->user_act = "topic_shift";
        out->pressure = "topic attention";
        out->response_move = "follow the topic shift before returning to old business";
        out->direct_input = 1u;
    } else if (contains_wordish(low, "what do you think") ||
               contains_wordish(low, "your thoughts") ||
               contains_wordish(low, "your turn") ||
               contains_wordish(low, "what would you ask") ||
               contains_wordish(low, "say anything")){
        out->user_act = "open_ended_invitation";
        out->pressure = "continuation";
        out->response_move = "offer one character-led thought and one grounded question";
        out->direct_input = 1u;
    } else if (question){
        out->user_act = "direct_question";
        out->pressure = "information";
        out->response_move = "answer directly but in character";
    } else if (starts_with_ci_low(low, "hi") || starts_with_ci_low(low, "hello") ||
               starts_with_ci_low(low, "good morning") ||
               starts_with_ci_low(low, "good evening") ||
               starts_with_ci_low(low, "hey")){
        out->user_act = "greeting";
        out->pressure = "social acknowledgement";
        out->response_move = "acknowledge briefly and invite a concrete next move";
    } else if (len > 100u){
        out->user_act = "rich_neutral_input";
        out->pressure = "topic attention";
        out->response_move = "respond plainly without over-performing the voice";
        out->rich_input = 1u;
        out->direct_input = 1u;
    } else if (len < 32u){
        out->user_act = "small_talk";
        out->pressure = "continuation";
        out->response_move = "ask one grounded follow-up question";
    }

    out->attend_before_open_loops =
        (out->rich_input || out->direct_input ||
         !strcmp(out->user_act, "emotional_disclosure") ||
         !strcmp(out->user_act, "correction") ||
         !strcmp(out->user_act, "challenge") ||
         !strcmp(out->user_act, "memory_probe")) ? 1u : 0u;

    if (ctx && ctx->frame && ctx->frame->open_loop_pressure >= 700u &&
        !out->attend_before_open_loops){
        out->pressure = "unfinished business";
        out->response_move = "continue the prior open loop briefly";
    }
}

/* Voice-flag bit → short token. */
static const char *VOICE_TOKEN[] = {
    "no-direct-affirm", "abstract", "sardonic", "metaphorical", "self-interrupt",
    NULL, NULL, NULL,     /* bits 5-7 reserved for verbosity */
    "mood-bleed", "callback-prone", "self-contradict", "delayed",
};

static const char *intent_token(int intent){
    switch (intent){
    case PE_INTENT_ANSWER:    return "answer";
    case PE_INTENT_EVADE:     return "evade";
    case PE_INTENT_ACCUSE:    return "accuse";
    case PE_INTENT_FLATTER:   return "flatter";
    case PE_INTENT_THREATEN:  return "threaten";
    case PE_INTENT_PROBE:     return "probe";
    case PE_INTENT_REDIRECT:  return "redirect";
    case PE_INTENT_MONOLOGUE: return "monologue";
    case PE_INTENT_REMINISCE: return "reminisce";
    case PE_INTENT_WITHDRAW:  return "withdraw";
    case PE_INTENT_JOKE:      return "joke";
    case PE_INTENT_BOAST:     return "boast";
    case PE_INTENT_INITIATE:  return "initiate";
    case PE_INTENT_ATTEND:    return "attend";
    case PE_INTENT_CLARIFY:   return "clarify";
    case PE_INTENT_PAUSE:     return "pause";
    }
    return "answer";
}

static const char *rhet_token(int rhet){
    switch (rhet){
    case 0: return "assert";     case 1: return "hedge";    case 2: return "deflect";
    case 3: return "escalate";   case 4: return "lament";   case 5: return "gloat";
    case 6: return "indict";     case 7: return "romanticize";
    case 8: return "intone";     case 9: return "confess";
    }
    return "neutral";
}

static const char *stance_token(int stance){
    switch (stance){
    case 0: return "neutral";    case 1: return "dominant";   case 2: return "intimate";
    case 3: return "defensive";  case 4: return "condescending";
    case 5: return "conspiratorial";
    }
    return "neutral";
}

static const char *speech_act_token(int act){
    switch (act){
    case PE_SA_APOLOGY:    return "apology";
    case PE_SA_REFUSAL:    return "refusal";
    case PE_SA_QUESTION:   return "question";
    case PE_SA_INSULT:     return "accusation";
    case PE_SA_THREAT:     return "threat";
    case PE_SA_PRAISE:     return "praise";
    case PE_SA_CONFESSION: return "confession";
    case PE_SA_CONCESSION: return "concession";
    case PE_SA_PROMISE:    return "promise";
    case PE_SA_EVASION:    return "evasion";
    case PE_SA_DEFLECTION: return "deflection";
    case PE_SA_PAUSE:      return "pause";
    case PE_SA_WITHDRAWAL: return "withdrawal";
    case PE_SA_DISCLOSURE: return "disclosure";
    default:               return "assertion";
    }
}

static const char *response_band(const Engine *eng, const CanonicalTurnFrame *f){
    if (f && f->speech_act == PE_SA_PAUSE) return "pause or near-silence";
    if (f && (f->speech_act == PE_SA_REFUSAL || f->speech_act == PE_SA_WITHDRAWAL))
        return "brief and final";
    if (eng && eng->state.current_intent == PE_INTENT_MONOLOGUE)
        return "expansive, but coherent";
    if (eng && eng->state.current_intent == PE_INTENT_REMINISCE)
        return "reflective";
    if (eng && eng->state.current_intent == PE_INTENT_PROBE)
        return "question-led";
    if (eng && eng->state.current_intent == PE_INTENT_ATTEND)
        return "short acknowledgement";
    if (eng && eng->state.exhaustion > 700)
        return "tired and compressed";
    if (eng && eng->state.obsession_pressure > 650)
        return "allowed to elaborate";
    return "natural conversational";
}

static const char *profile_token(int profile){
    switch (profile){
    case PE_SLM_PROFILE_TINY:       return "tiny";
    case PE_SLM_PROFILE_EXPRESSIVE: return "expressive";
    default:                        return "balanced";
    }
}

static const char *topic_name_lookup(const Engine *eng, uint16_t topic_id){
    if (!eng || topic_id == 0xFFFF) return NULL;
    for (uint32_t i = 0; i < eng->topics.count; ++i){
        if (eng->topics.topics[i].id == topic_id)
            return eng->topics.topics[i].name;
    }
    return NULL;
}

static int input_mentions_lk_topic(const char *user_input, const char *topic){
    char low[512];
    char t[PE_LK_TOPIC_LEN];
    size_t i;
    if (!user_input || !topic || !topic[0]) return 0;
    for (i = 0; user_input[i] && i + 1 < sizeof(low); ++i)
        low[i] = (char)tolower((unsigned char)user_input[i]);
    low[i] = 0;
    for (i = 0; topic[i] && i + 1 < sizeof(t); ++i)
        t[i] = (char)tolower((unsigned char)topic[i]);
    t[i] = 0;
    return strstr(low, t) != NULL;
}

static void append_learned_knowledge_block(const Engine *eng,
                                           const char *user_input,
                                           char *out_buf,
                                           int cap,
                                           int *pos){
    int shown = 0;
    uint32_t shown_ids[4] = {0,0,0,0};
    if (!eng || !out_buf || !pos) return;
    for (uint32_t i = 0; i < eng->learned_knowledge.header.entry_count
                        && i < PE_LK_RECORD_CAP && shown < 4; ++i){
        const pe_lk_record_t *r = &eng->learned_knowledge.records[i];
        if (!r->record_id || !r->topic_key[0] || !r->claim_text[0]) continue;
        if (r->status == PE_LK_STATUS_DEPRECATED ||
            r->status == PE_LK_STATUS_CORRECTED) continue;
        if (!input_mentions_lk_topic(user_input, r->topic_key) && shown > 0)
            continue;
        if (!shown) append(out_buf, cap, pos, "\n[LEARNED_KNOWLEDGE]\n");
        append(out_buf, cap, pos,
               "record_%d topic=%s status=%s confidence=%u source=%s scope=%s claim=%.150s\n",
               shown, r->topic_key, pe_lk_status_name(r->status),
               (unsigned)r->confidence, pe_lk_source_name(r->source_type),
               pe_lk_scope_name(r->scope), r->claim_text);
        shown_ids[shown] = r->record_id;
        ++shown;
    }
    if (shown){
        int edge_shown = 0;
        for (uint32_t e = 0; e < eng->learned_knowledge.edge_count
                            && e < PE_LK_EDGE_CAP && edge_shown < 6; ++e){
            const pe_lk_edge_t *edge = &eng->learned_knowledge.edges[e];
            int linked = 0;
            for (int s = 0; s < shown; ++s){
                if (edge->source_record_id == shown_ids[s] ||
                    edge->target_record_id == shown_ids[s]){
                    linked = 1;
                    break;
                }
            }
            if (!linked) continue;
            append(out_buf, cap, pos,
                   "edge_%d %s source=%u target=%u confidence=%u weight=%u\n",
                   edge_shown, pe_lk_edge_name(edge->relation_type),
                   (unsigned)edge->source_record_id,
                   (unsigned)edge->target_record_id,
                   (unsigned)edge->confidence,
                   (unsigned)edge->weight);
            ++edge_shown;
        }
        append(out_buf, cap, pos,
               "Use confirmed or authored learned knowledge as constraints. Treat provisional or disputed knowledge as uncertain. Do not revive corrected/deprecated claims.\n");
    }
}

static void append_tiny_examples(const Engine *eng, char *buf, int cap, int *pos){
    const char *who = (eng && eng->identity.character_name[0])
                    ? eng->identity.character_name : "{{char}}";
    append(buf, cap, pos, "\n[EXAMPLES]\n");
    append(buf, cap, pos, "<START>\n{{user}}: What do you mean?\n");
    append(buf, cap, pos, "%s: I mean the important part is still unresolved. Ask it plainly.\n", who);
    append(buf, cap, pos, "<START>\n{{user}}: Do you agree?\n");
    append(buf, cap, pos, "%s: Not entirely. I see the point, but I do not accept the conclusion.\n", who);
    append(buf, cap, pos, "<START>\n{{user}}: Tell me more.\n");
    append(buf, cap, pos, "%s: One thing first: name what you want from this conversation.\n", who);
}

static void append_topics_line(const Engine *eng, char *buf, int cap, int *pos){
    append(buf, cap, pos, "known=");
    int shown = 0;
    if (eng){
        for (uint32_t i = 0; i < eng->topics.count && shown < 10; ++i){
            if (!eng->topics.topics[i].name[0]) continue;
            append(buf, cap, pos, "%s%s", shown ? ", " : "", eng->topics.topics[i].name);
            ++shown;
        }
    }
    if (!shown) append(buf, cap, pos, "current conversation");
    append(buf, cap, pos, "\n");
}

static int prompt_compile_gemma_raw(const RenderContext *ctx,
                                    const PromptCompilerConfig *cfg,
                                    const char *user_input,
                                    char *out_buf, int cap){
    const Engine *eng = ctx->npc;
    const char *who = (eng && eng->identity.character_name[0])
                    ? eng->identity.character_name : "the character";
    int pos = 0;

    append(out_buf, cap, &pos, "<start_of_turn>user\n");
    append(out_buf, cap, &pos, "PersonaConsole state packet. Reply only as %s.\n", who);
    if (eng && eng->relation.known_as[0])
        append(out_buf, cap, &pos, "addressing=%s\n", eng->relation.known_as);
    append(out_buf, cap, &pos, "renderer_profile=%s chat_format=gemma\n", profile_token(cfg->render_profile));
    append(out_buf, cap, &pos, "Speak in complete, short, grounded dialogue turns. No assistant phrasing.\n");
    append(out_buf, cap, &pos, "No atmosphere-setting filler. Do not begin with weather, darkness, silence, ash, or bones.\n");
    append(out_buf, cap, &pos, "Use the current user line directly before expanding.\n");
    append(out_buf, cap, &pos, "Speak to the other person from inside the scene. Do not say anyone \"sounds like\" or describe the role from outside.\n");
    append(out_buf, cap, &pos, "Do not rename the addressee or call them by a memory name unless [USER] says that is their name.\n");
    append(out_buf, cap, &pos, "Do not echo a distinctive phrase from the other speaker unless you are directly challenging it.\n");
    append(out_buf, cap, &pos, "Do not say \"I remember this\" or \"You asked me to remember\". Work memory into the reply naturally.\n");
    append(out_buf, cap, &pos, "Only named cartridge facts exist. ");
    append_topics_line(eng, out_buf, cap, &pos);
    if (eng){
        append(out_buf, cap, &pos, "affect=mood:%d obsession:%d exhaustion:%d\n",
               eng->state.mood, eng->state.obsession_pressure, eng->state.exhaustion);
        append(out_buf, cap, &pos, "intent=%s\n", intent_token(eng->state.current_intent));
        if (ctx->frame){
            const char *tn = topic_name_lookup(eng, ctx->frame->primary_topic);
            if (tn && tn[0]) append(out_buf, cap, &pos, "topic=%s\n", tn);
            append(out_buf, cap, &pos, "speech_act=%s\n", speech_act_token(ctx->frame->speech_act));
        }
    }
    if (ctx->frame && ctx->frame->fatigue_term_count){
        append(out_buf, cap, &pos, "tired_terms=");
        for (uint8_t i = 0; i < ctx->frame->fatigue_term_count; ++i)
            append(out_buf, cap, &pos, "%s%s",
                   i ? ", " : "", ctx->frame->fatigue_terms[i]);
        append(out_buf, cap, &pos, "\n");
    }
    append(out_buf, cap, &pos, "Study this voice shape and continue it.<end_of_turn>\n");
    append(out_buf, cap, &pos, "<start_of_turn>model\nUnderstood.<end_of_turn>\n");
    append(out_buf, cap, &pos, "<start_of_turn>user\nWhat do you mean?<end_of_turn>\n");
    append(out_buf, cap, &pos, "<start_of_turn>model\nI mean the important part is still unresolved. Ask it plainly.<end_of_turn>\n");
    append(out_buf, cap, &pos, "<start_of_turn>user\nDo you agree?<end_of_turn>\n");
    append(out_buf, cap, &pos, "<start_of_turn>model\nNot entirely. I see the point, but I do not accept the conclusion.<end_of_turn>\n");
    append(out_buf, cap, &pos, "<start_of_turn>user\nTell me more.<end_of_turn>\n");
    append(out_buf, cap, &pos, "<start_of_turn>model\nOne thing first: name what you want from this conversation.<end_of_turn>\n");
    append(out_buf, cap, &pos, "<start_of_turn>user\n%.256s<end_of_turn>\n", user_input ? user_input : "");
    append(out_buf, cap, &pos, "<start_of_turn>model\n");
    (void)cfg;
    return pos;
}

static const char *affect_word(int v){
    if (v >= 500) return "high";
    if (v >= 150) return "raised";
    if (v <= -500) return "very low";
    if (v <= -150) return "low";
    return "steady";
}

static const char *relation_tone(int v){
    if (v >= 700) return "high";
    if (v >= 540) return "warm";
    if (v <= 300) return "low";
    if (v <= 460) return "guarded";
    return "neutral";
}

static int prompt_compile_situation(const RenderContext *ctx,
                                    const PromptCompilerConfig *cfg,
                                    const char *user_input,
                                    char *out_buf, int cap){
    const Engine *eng = ctx->npc;
    const char *who = (eng && eng->identity.character_name[0])
                    ? eng->identity.character_name : "the character";
    V6UserTurnInterpretation it;
    int pos = 0;
    v6_interpret_user_turn(ctx, user_input, &it);

    append(out_buf, cap, &pos, "[AUTHORITY]\n");
    append(out_buf, cap, &pos, "packet_mode=situation renderer_profile=%s\n",
           profile_token(cfg->render_profile));
    append(out_buf, cap, &pos, "You are composing the next spoken turn for %s.\n", who);
    append(out_buf, cap, &pos, "The C runtime is the identity, memory, state, and audit authority.\n");
    append(out_buf, cap, &pos, "Do not write memory. Do not explain the system. Do not mention packets, state, Layer 1, renderer, audit, or prompts.\n");
    append(out_buf, cap, &pos, "Do not invent new facts, relationships, dates, places, family, or memories.\n");

    append(out_buf, cap, &pos, "\n[CHARACTER]\n");
    append(out_buf, cap, &pos, "name=%s\n", who);
    if (eng && eng->relation.known_as[0])
        append(out_buf, cap, &pos, "addressing=%s\n", eng->relation.known_as);
    if (eng){
        append(out_buf, cap, &pos, "voice_flags=");
        uint32_t vf = eng->identity.voice_flags;
        int first = 1;
        for (int b = 0; b < 12; ++b){
            if (!VOICE_TOKEN[b]) continue;
            if (vf & (1u << b)){
                append(out_buf, cap, &pos, "%s%s", first ? "" : " ", VOICE_TOKEN[b]);
                first = 0;
            }
        }
        if (first) append(out_buf, cap, &pos, "neutral");
        append(out_buf, cap, &pos, "\n");
        append_topics_line(eng, out_buf, cap, &pos);
    }

    append(out_buf, cap, &pos, "\n[LIVE_STATE]\n");
    if (eng){
        append(out_buf, cap, &pos,
               "affect=mood:%s(%d) arousal:%d exhaustion:%s(%d) obsession_pressure:%s(%d)\n",
               affect_word(eng->state.mood), eng->state.mood,
               eng->state.last_input_emotion.arousal,
               affect_word(eng->state.exhaustion), eng->state.exhaustion,
               affect_word(eng->state.obsession_pressure), eng->state.obsession_pressure);
        append(out_buf, cap, &pos,
               "relation=trust:%s threat:%s intimacy:%s resentment:%s admiration:%s\n",
               relation_tone(eng->relation_dims.trust),
               relation_tone(eng->relation_dims.threat),
               relation_tone(eng->relation_dims.intimacy),
               relation_tone(eng->relation_dims.resentment),
               relation_tone(eng->relation_dims.admiration));
        append(out_buf, cap, &pos,
               "recent_turn_summary=turn:%u last_reply_question:%u no_question_streak:%u\n",
               eng->state.turn_count, eng->state.last_reply_had_question,
               eng->state.turns_since_question);
    }
    if (ctx->frame){
        append(out_buf, cap, &pos,
               "canonical_frame=intent:%s speech_act:%s stance:%u mode:%s open_loop_pressure:%u\n",
               eng ? intent_token(eng->state.current_intent) : "answer",
               speech_act_token(ctx->frame->speech_act),
               (unsigned)ctx->frame->stance,
               rhet_token(ctx->frame->rhetorical_mode),
               (unsigned)ctx->frame->open_loop_pressure);
        {
            const char *tn = topic_name_lookup(eng, ctx->frame->primary_topic);
            if (tn && tn[0]) append(out_buf, cap, &pos, "primary_topic=%s\n", tn);
        }
    }

    append(out_buf, cap, &pos, "\n[MEMORY_AS_MOTIVE]\n");
    if (ctx->memories && eng && ctx->memories->episodic_count > 0){
        for (int i = 0; i < ctx->memories->episodic_count && i < 3; ++i){
            int idx = ctx->memories->episodic_idx[i];
            if (idx < 0 || idx >= PE_EPISODIC_MAX) continue;
            const MemoryNode *m = &eng->memory.episodic[idx];
            const char *tn = topic_name_lookup(eng, m->topic_id);
            append(out_buf, cap, &pos, "memory_%d=motive topic:%s summary:%.90s\n",
                   i, (tn && tn[0]) ? tn : "none", m->summary);
        }
    } else {
        append(out_buf, cap, &pos, "none_selected=1\n");
    }
    append(out_buf, cap, &pos, "Use memory as motive or continuity, not as a quoted database record.\n");

    append_learned_knowledge_block(eng, user_input, out_buf, cap, &pos);

    if (!strcmp(it.user_act, "memory_probe")){
        append(out_buf, cap, &pos, "\n[MEMORY_PROBE_OVERLAY]\n");
        append(out_buf, cap, &pos, "The user is asking whether continuity exists. This is high priority.\n");
        if (ctx->memories && ctx->memories->episodic_count > 0){
            append(out_buf, cap, &pos, "Use one selected canonical memory above as the basis of the reply.\n");
            append(out_buf, cap, &pos, "If the selected memories do not match the user's probed subject, do not substitute an unrelated memory.\n");
            append(out_buf, cap, &pos, "When no selected memory matches, express grounded uncertainty in character.\n");
            append(out_buf, cap, &pos, "Preserve actor attribution: if memory says the user said or asked it, say the user said or asked it, not that you said it.\n");
            append(out_buf, cap, &pos, "Surface the memory naturally, as a person recalling a thread, not as a log entry.\n");
        } else {
            append(out_buf, cap, &pos, "No canonical memory is selected. Express grounded uncertainty in character.\n");
        }
        append(out_buf, cap, &pos, "Do not deflect with generic wording such as \"say it another way\" or \"ask differently\".\n");
    } else if (!strcmp(it.user_act, "emotional_disclosure")){
        append(out_buf, cap, &pos, "\n[EMOTIONAL_DISCLOSURE_OVERLAY]\n");
        append(out_buf, cap, &pos, "The user is offering emotional state, not merely topic data.\n");
        append(out_buf, cap, &pos, "Acknowledge the feeling before analysis, advice, or self-led agenda.\n");
        append(out_buf, cap, &pos, "Do not rush to fix, lecture, summarize the system, or change the subject.\n");
        append(out_buf, cap, &pos, "Keep the character voice, but let relevance outrank signature phrasing.\n");
        append(out_buf, cap, &pos, "If you ask a question, ask one grounded follow-up about what the user just disclosed.\n");
    } else if (!strcmp(it.user_act, "identity_test")){
        append(out_buf, cap, &pos, "\n[IDENTITY_TEST_OVERLAY]\n");
        append(out_buf, cap, &pos, "The user is pressing identity or continuity. Answer from inside the character.\n");
        append(out_buf, cap, &pos, "Do not mention prompts, packets, models, simulations, roleplay, or system design.\n");
        append(out_buf, cap, &pos, "Defend or express identity according to current stance without becoming an assistant.\n");
        append(out_buf, cap, &pos, "If uncertain, make uncertainty part of the character's reply, not a technical caveat.\n");
        if (eng){
            append(out_buf, cap, &pos, "\n[SELF_MODEL]\n");
            append(out_buf, cap, &pos,
                   "ideal_self=%d ought_self=%d feared_self=%d\n",
                   (int)eng->dissonance.ideal_self_model,
                   (int)eng->dissonance.ought_self_model,
                   (int)eng->dissonance.feared_self_model);
            append(out_buf, cap, &pos,
                   "dissonance_gaps ideal=%u ought=%u feared=%u\n",
                   (unsigned)eng->dissonance.ideal_gap,
                   (unsigned)eng->dissonance.ought_gap,
                   (unsigned)eng->dissonance.feared_gap);
            if (eng->dissonance.feared_gap >= eng->dissonance.ideal_gap
                && eng->dissonance.feared_gap >= eng->dissonance.ought_gap)
                append(out_buf, cap, &pos, "identity_pressure=avoid_collapsing_into_feared_self\n");
            else if (eng->dissonance.ought_gap >= eng->dissonance.ideal_gap)
                append(out_buf, cap, &pos, "identity_pressure=answer_obligation_without_submission\n");
            else
                append(out_buf, cap, &pos, "identity_pressure=protect_aspirational_self_without_performing\n");
        }
    }

    append(out_buf, cap, &pos, "\n[USER_TURN_INTERPRETATION]\n");
    append(out_buf, cap, &pos, "act=%s\n", it.user_act);
    append(out_buf, cap, &pos, "conversational_pressure=%s\n", it.pressure);
    append(out_buf, cap, &pos, "rich_input=%u direct_input=%u attend_before_open_loops=%u\n",
           (unsigned)it.rich_input, (unsigned)it.direct_input,
           (unsigned)it.attend_before_open_loops);
    append(out_buf, cap, &pos, "user_text=%.384s\n", user_input ? user_input : "");

    append(out_buf, cap, &pos, "\n[RESPONSE_MOVE]\n");
    append(out_buf, cap, &pos, "recommendation=%s\n", it.response_move);
    append(out_buf, cap, &pos, "Compose the actual response. Do not paraphrase a selected template line.\n");
    append(out_buf, cap, &pos, "If the current message is rich or direct, attend to it before resuming open loops or proactive thoughts.\n");
    append(out_buf, cap, &pos, "If the user asks a direct question, answer the question before adding color or resistance.\n");
    append(out_buf, cap, &pos, "Use the character voice, but do not over-perform it. Prefer listening and direct relevance over catchphrases.\n");
    append_repair_block(ctx, out_buf, cap, &pos);

    append(out_buf, cap, &pos, "\n[OUTPUT]\n");
    append(out_buf, cap, &pos, "Return only the spoken character response.\n");
    if (ctx->frame && ctx->frame->require_question)
        append(out_buf, cap, &pos, "End with one grounded question.\n");
    if (ctx->frame && ctx->frame->allow_empty)
        append(out_buf, cap, &pos, "A short pause or silence is allowed.\n");
    append(out_buf, cap, &pos, "No assistant tone. No system words. No bracket tags.\n");
    return pos;
}

int prompt_compile(const RenderContext *ctx,
                   const PromptCompilerConfig *cfg,
                   char *out_buf, int out_cap){
    return prompt_compile_with_input(ctx, cfg, NULL, out_buf, out_cap);
}

int prompt_compile_with_input(const RenderContext *ctx,
                              const PromptCompilerConfig *cfg,
                              const char *user_input,
                              char *out_buf, int out_cap){
    if (!ctx || !out_buf || out_cap < 64) return -1;

    PromptCompilerConfig dc;
    if (!cfg){ prompt_compiler_default_config(&dc); cfg = &dc; }
    int cap = cfg->max_bytes < out_cap ? cfg->max_bytes : out_cap;
    int pos = 0;

    if (v6_packet_mode_is_situation())
        return prompt_compile_situation(ctx, cfg, user_input, out_buf, cap);

    if (cfg->chat_format == PE_SLM_CHAT_GEMMA)
        return prompt_compile_gemma_raw(ctx, cfg, user_input, out_buf, cap);

    const Engine *eng = ctx->npc;
    const char *who = (eng && eng->identity.character_name[0])
                    ? eng->identity.character_name : "the character";

    /* ----- [IDENTITY] ----- */
    append(out_buf, cap, &pos, "[IDENTITY]\n");
    append(out_buf, cap, &pos, "name=%s\n", who);
    if (eng && eng->relation.known_as[0])
        append(out_buf, cap, &pos, "addressing=%s\n", eng->relation.known_as);
    if (eng){
        /* Big Five are stored as 0.16 fixed-point; render as 0..100 percent
         * for SLM legibility.  Math is integer-only. */
        append(out_buf, cap, &pos, "big5=O%u C%u E%u A%u N%u\n",
               (unsigned)((eng->identity.openness          * 100u) >> 16),
               (unsigned)((eng->identity.conscientiousness * 100u) >> 16),
               (unsigned)((eng->identity.extraversion      * 100u) >> 16),
               (unsigned)((eng->identity.agreeableness     * 100u) >> 16),
               (unsigned)((eng->identity.neuroticism       * 100u) >> 16));
    }

    /* ----- [WORLD] (closed-world referents) ----- */
    if (eng){
        append(out_buf, cap, &pos, "\n[WORLD]\n");
        append(out_buf, cap, &pos, "known_referents=");
        int shown = 0;
        for (uint32_t i = 0; i < eng->topics.count && shown < 12; ++i){
            if (!eng->topics.topics[i].name[0]) continue;
            append(out_buf, cap, &pos, "%s%s",
                   shown ? ", " : "", eng->topics.topics[i].name);
            ++shown;
        }
        for (uint8_t i = 0; i < eng->identity.core_memory_count && shown < 16; ++i){
            const MemoryNode *m = &eng->identity.core_memories_seed[i];
            if (!m->summary[0]) continue;
            append(out_buf, cap, &pos, "%s%.48s",
                   shown ? ", " : "", m->summary);
            ++shown;
        }
        append(out_buf, cap, &pos, "\n");
    }

    /* ----- [AFFECT] ----- */
    if (eng){
        append(out_buf, cap, &pos, "\n[AFFECT]\n");
        append(out_buf, cap, &pos, "mood=%d\n", eng->state.mood);
        append(out_buf, cap, &pos, "acute_spike=%d\n", eng->state.acute_spike);
        append(out_buf, cap, &pos, "obsession_pressure=%d\n", eng->state.obsession_pressure);
        append(out_buf, cap, &pos, "exhaustion=%d\n", eng->state.exhaustion);
        if (eng->state.intoxication > 0)
            append(out_buf, cap, &pos, "intoxication=%d\n", eng->state.intoxication);
    }

    /* ----- [STANCE] (compressed beliefs) ----- */
    if (cfg->include_schema && ctx->schema){
        append(out_buf, cap, &pos, "\n[STANCE]\n");
        static const char *NAMES[SCHEMA_SLOT_COUNT] = {
            "user_trustworthy","user_hostile","user_intimate","user_competent",
            "user_deceptive","relationship_owed","relationship_owes","self_dignity"
        };
        const SchemaState *s = ctx->schema;
        int wrote = 0;
        for (int i = 0; i < SCHEMA_SLOT_COUNT; ++i){
            if (s->slot[i] == 0 && s->evidence[i] == 0) continue;
            append(out_buf, cap, &pos, "%s=%d\n", NAMES[i], (int)s->slot[i]);
            ++wrote;
        }
        if (!wrote) append(out_buf, cap, &pos, "neutral=1\n");
    }

    /* ----- [MEMORY] ----- */
    if (cfg->include_memory_hooks && ctx->memories && eng){
        int header = 0;
        for (int i = 0; i < ctx->memories->episodic_count && i < 4; ++i){
            int idx = ctx->memories->episodic_idx[i];
            if (idx < 0 || idx >= PE_EPISODIC_MAX) continue;
            const MemoryNode *m = &eng->memory.episodic[idx];
            if (!m->summary[0]) continue;
            if (!header){ append(out_buf, cap, &pos, "\n[MEMORY]\n"); header = 1; }
            append(out_buf, cap, &pos, "recent=%.96s\n", m->summary);
        }
        for (int i = 0; i < ctx->memories->core_count && i < 4; ++i){
            int idx = ctx->memories->core_idx[i];
            if (idx < 0 || idx >= PE_CORE_SEED_MAX) continue;
            const MemoryNode *m = &eng->identity.core_memories_seed[idx];
            if (!m->summary[0]) continue;
            if (!header){ append(out_buf, cap, &pos, "\n[MEMORY]\n"); header = 1; }
            append(out_buf, cap, &pos, "core=%.86s\n", m->summary);
        }
    }

    append_learned_knowledge_block(eng, user_input, out_buf, cap, &pos);

    /* ----- [INTENT] ----- */
    if (cfg->include_intent && ctx->plan && eng){
        const UtterancePlan *p = ctx->plan;
        append(out_buf, cap, &pos, "\n[INTENT]\n");
        append(out_buf, cap, &pos, "intent=%s\n", intent_token(eng->state.current_intent));
        if (ctx->frame)
            append(out_buf, cap, &pos, "speech_act=%s\n", speech_act_token(ctx->frame->speech_act));
        if (ctx->frame){
            const char *tn = topic_name_lookup(eng, ctx->frame->primary_topic);
            if (!tn || !tn[0]) tn = topic_name_lookup(eng, p->target_topic);
            if (tn && tn[0]) append(out_buf, cap, &pos, "topic=%s\n", tn);
        }
        append(out_buf, cap, &pos, "rhet=%s\n",   rhet_token(p->rhetorical_mode));
        append(out_buf, cap, &pos, "stance=%s\n", stance_token(p->stance));
        append(out_buf, cap, &pos, "cert=%d aggr=%d theat=%d hedge=%d\n",
               (int)p->certainty, (int)p->aggression,
               (int)p->theatricality, (int)p->hedging);
    }

    /* ----- [VOICE] ----- */
    if (cfg->include_voice_mask && eng){
        append(out_buf, cap, &pos, "\n[VOICE]\n");
        append(out_buf, cap, &pos, "flags=");
        uint32_t vf = eng->identity.voice_flags;
        int first = 1;
        for (int b = 0; b < 12; ++b){
            if (!VOICE_TOKEN[b]) continue;
            if (vf & (1u << b)){
                append(out_buf, cap, &pos, "%s%s", first ? "" : " ", VOICE_TOKEN[b]);
                first = 0;
            }
        }
        if (first) append(out_buf, cap, &pos, "neutral");
        append(out_buf, cap, &pos, "\n");
        for (int i = 0; i < 4; ++i){
            if (eng->identity.flourishes[i][0])
                append(out_buf, cap, &pos, "flourish=%.40s\n", eng->identity.flourishes[i]);
        }
    }

    /* ----- [USER] ----- */
    if (cfg->include_current_input && user_input && user_input[0]){
        append(out_buf, cap, &pos, "\n[USER]\n%.256s\n", user_input);
    }

    if (ctx->frame && ctx->frame->fatigue_term_count){
        append(out_buf, cap, &pos, "\n[FATIGUE]\navoid_terms=");
        for (uint8_t i = 0; i < ctx->frame->fatigue_term_count; ++i)
            append(out_buf, cap, &pos, "%s%s",
                   i ? ", " : "", ctx->frame->fatigue_terms[i]);
        append(out_buf, cap, &pos, "\n");
    }

    if (cfg->render_profile == PE_SLM_PROFILE_TINY)
        append_tiny_examples(eng, out_buf, cap, &pos);

    /* ----- [TASK] — instruction; rigid + short ----- */
    append(out_buf, cap, &pos, "\n[TASK]\n");
    append(out_buf, cap, &pos, "renderer_profile=%s\n", profile_token(cfg->render_profile));
    append(out_buf, cap, &pos, "Reply as %s. Pacing=%s.\n",
           who, response_band(eng, ctx->frame));
    if (ctx->frame && ctx->frame->require_question)
        append(out_buf, cap, &pos, "End your reply with a question.\n");
    if (ctx->frame && ctx->frame->allow_empty)
        append(out_buf, cap, &pos, "A short pause or silence is an acceptable reply.\n");
    append(out_buf, cap, &pos, "Obey [AFFECT] [STANCE] [INTENT] [VOICE] as constraints.\n");
    append(out_buf, cap, &pos, "Answer the current [USER] line directly before expanding.\n");
    append(out_buf, cap, &pos, "Speak from inside the scene to the addressee; do not narrate or critique what the other character sounds like.\n");
    append(out_buf, cap, &pos, "Do not rename the addressee or call them by a memory name unless [USER] says that is their name.\n");
    append(out_buf, cap, &pos, "Do not mirror the addressee's exact metaphor or catchphrase unless your speech act is a challenge or correction.\n");
    append(out_buf, cap, &pos, "Do not copy any phrase longer than three words from [USER]. Add new information, a new question, or a new stance.\n");
    append(out_buf, cap, &pos, "Do not say \"I remember this\" or \"You asked me to remember\". Refer to memories naturally, without labels.\n");
    if (ctx->frame && ctx->frame->fatigue_term_count)
        append(out_buf, cap, &pos, "Avoid [FATIGUE] terms this turn unless needed to answer a direct question.\n");
    append(out_buf, cap, &pos, "Vary sentence shape; do not reuse a striking metaphor or opener.\n");
    append(out_buf, cap, &pos, "Only people, places, and things named in [WORLD], [MEMORY], or [USER] exist.\n");
    append(out_buf, cap, &pos, "Do NOT invent people, places, events, family, or memories not listed in [MEMORY].\n");
    append(out_buf, cap, &pos, "No assistant tone, no helpdesk phrasing, no meta-commentary.\n");
    if (cfg->render_profile == PE_SLM_PROFILE_TINY){
        append(out_buf, cap, &pos, "Tiny model rules: use one concrete noun from [WORLD] or [MEMORY]; no atmospheric filler.\n");
        append(out_buf, cap, &pos, "Prefer plain subject-verb sentences. Do not begin with weather, darkness, silence, or vague mood.\n");
    } else if (cfg->render_profile == PE_SLM_PROFILE_EXPRESSIVE){
        append(out_buf, cap, &pos, "Expressive model rules: richer phrasing is allowed, but answer first and avoid exposition.\n");
        append(out_buf, cap, &pos, "No assistant deference. Do not over-address the user or explain the role.\n");
    } else {
        append(out_buf, cap, &pos, "Balanced model rules: concise, grounded, one to three sentences unless the frame asks otherwise.\n");
        append(out_buf, cap, &pos, "Favor direct answers over atmosphere.\n");
    }
    append_repair_block(ctx, out_buf, cap, &pos);
    append(out_buf, cap, &pos, "Do NOT use the bracket tags in your reply.\n");

    return pos;
}

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
    append(out_buf, cap, &pos, "renderer_profile=%s chat_format=gemma\n", profile_token(cfg->render_profile));
    append(out_buf, cap, &pos, "Speak in complete, short, grounded dialogue turns. No assistant phrasing.\n");
    append(out_buf, cap, &pos, "No atmosphere-setting filler. Do not begin with weather, darkness, silence, ash, or bones.\n");
    append(out_buf, cap, &pos, "Use the current user line directly before expanding.\n");
    append(out_buf, cap, &pos, "Speak to the other person from inside the scene. Do not say anyone \"sounds like\" or describe the role from outside.\n");
    append(out_buf, cap, &pos, "Do not echo a distinctive phrase from the other speaker unless you are directly challenging it.\n");
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

    if (cfg->chat_format == PE_SLM_CHAT_GEMMA)
        return prompt_compile_gemma_raw(ctx, cfg, user_input, out_buf, cap);

    const Engine *eng = ctx->npc;
    const char *who = (eng && eng->identity.character_name[0])
                    ? eng->identity.character_name : "the character";

    /* ----- [IDENTITY] ----- */
    append(out_buf, cap, &pos, "[IDENTITY]\n");
    append(out_buf, cap, &pos, "name=%s\n", who);
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
    append(out_buf, cap, &pos, "Do not mirror the addressee's exact metaphor or catchphrase unless your speech act is a challenge or correction.\n");
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
    append(out_buf, cap, &pos, "Do NOT use the bracket tags in your reply.\n");

    return pos;
}

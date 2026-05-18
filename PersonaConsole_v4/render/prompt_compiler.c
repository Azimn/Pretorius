/* prompt_compiler.c — V4 deterministic-state → semantic-constraints (impl).
 *
 * Projects Layer 1 state into a structured constraint block.  No lore
 * dumps.  No roleplay prompting.  This is what the SLM backend feeds
 * into the model.  Identical RenderContext → byte-identical output.
 */
#include "prompt_compiler.h"
#include "../core/persona.h"
#include "../schema/schema_state.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void prompt_compiler_default_config(PromptCompilerConfig *out){
    if (!out) return;
    out->max_bytes              = PE_PROMPT_MAX_BYTES;
    out->include_memory_hooks   = 1;
    out->include_schema         = 1;
    out->include_voice_mask     = 1;
    out->include_intent         = 1;
    out->include_current_input  = 1;
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

/* Voice-flag bit → short token suitable for a constraint block. */
static const char *VOICE_TOKEN[] = {
    /* matches persona.h PE_VOICE_FLAG_* bit positions */
    "no-direct-affirm",
    "abstract",
    "sardonic",
    "metaphorical",
    "self-interrupt",
    /* bits 5-7 reserved for verbosity */
    NULL, NULL, NULL,
    "mood-bleed",
    "callback-prone",
    "self-contradict",
    "delayed",
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
    }
    return "answer";
}

static const char *rhet_token(int rhet){
    switch (rhet){
    case 0: return "assert";
    case 1: return "hedge";
    case 2: return "deflect";
    case 3: return "escalate";
    case 4: return "lament";
    case 5: return "gloat";
    case 6: return "indict";
    case 7: return "romanticize";
    case 8: return "intone";
    case 9: return "confess";
    }
    return "neutral";
}

static const char *stance_token(int stance){
    switch (stance){
    case 0: return "neutral";
    case 1: return "dominant";
    case 2: return "intimate";
    case 3: return "defensive";
    case 4: return "condescending";
    case 5: return "conspiratorial";
    }
    return "neutral";
}

/* Public entry point. */
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

    const Engine *eng = ctx->npc;

    /* ---- IDENTITY (compact, not lore) ---- */
    append(out_buf, cap, &pos, "IDENTITY:\n  name: %s\n",
           (eng && eng->identity.character_name[0]) ? eng->identity.character_name : "unknown");
    if (eng){
        append(out_buf, cap, &pos, "  big5: O=%u C=%u E=%u A=%u N=%u\n",
               eng->identity.openness, eng->identity.conscientiousness,
               eng->identity.extraversion, eng->identity.agreeableness,
               eng->identity.neuroticism);
    }

    /* ---- CURRENT AFFECT ---- */
    if (eng){
        append(out_buf, cap, &pos, "CURRENT_AFFECT:\n");
        append(out_buf, cap, &pos, "  mood:        %d\n", eng->state.mood);
        append(out_buf, cap, &pos, "  arousal_acute_spike: %d\n", eng->state.acute_spike);
        append(out_buf, cap, &pos, "  obsession_pressure:  %d\n", eng->state.obsession_pressure);
        append(out_buf, cap, &pos, "  exhaustion:  %d\n", eng->state.exhaustion);
        if (eng->state.intoxication > 0)
            append(out_buf, cap, &pos, "  intoxication: %d\n", eng->state.intoxication);
    }

    /* ---- SCHEMA (relational stance) ---- */
    if (cfg->include_schema && ctx->schema){
        char sbuf[512];
        int n = schema_format(ctx->schema, sbuf, sizeof(sbuf));
        if (n > 0) append(out_buf, cap, &pos, "RELATIONAL_STANCE:\n  %s\n", sbuf);
    }

    /* ---- ACTIVE MEMORY HOOKS ---- */
    if (cfg->include_memory_hooks && ctx->memories && eng){
        int wrote_header = 0;
        for (int i = 0; i < ctx->memories->episodic_count && i < 4; ++i){
            int idx = ctx->memories->episodic_idx[i];
            if (idx < 0 || idx >= PE_EPISODIC_MAX) continue;
            const MemoryNode *m = &eng->memory.episodic[idx];
            if (!m->summary[0]) continue;
            if (!wrote_header){ append(out_buf, cap, &pos, "ACTIVE_MEMORY_HOOKS:\n"); wrote_header = 1; }
            append(out_buf, cap, &pos, "  - %.96s\n", m->summary);
        }
        for (int i = 0; i < ctx->memories->core_count && i < 4; ++i){
            int idx = ctx->memories->core_idx[i];
            if (idx < 0 || idx >= PE_CORE_SEED_MAX) continue;
            const MemoryNode *m = &eng->identity.core_memories_seed[idx];
            if (!m->summary[0]) continue;
            if (!wrote_header){ append(out_buf, cap, &pos, "ACTIVE_MEMORY_HOOKS:\n"); wrote_header = 1; }
            append(out_buf, cap, &pos, "  - (core) %.86s\n", m->summary);
        }
    }

    /* ---- VOICE MASK ---- */
    if (cfg->include_voice_mask && eng){
        append(out_buf, cap, &pos, "VOICE_MASK:\n");
        uint32_t vf = eng->identity.voice_flags;
        for (int b = 0; b < 12; ++b){
            if (!VOICE_TOKEN[b]) continue;
            if (vf & (1u << b))
                append(out_buf, cap, &pos, "  - %s\n", VOICE_TOKEN[b]);
        }
        /* signature flourishes — let the renderer see them, not just emit them */
        for (int i = 0; i < 4; ++i){
            if (eng->identity.flourishes[i][0])
                append(out_buf, cap, &pos, "  - flourish: %.40s\n", eng->identity.flourishes[i]);
        }
    }

    /* ---- INTENT + PLAN ---- */
    if (cfg->include_intent && ctx->plan && eng){
        const UtterancePlan *p = ctx->plan;
        append(out_buf, cap, &pos, "INTENT:\n  primary: %s\n  rhetorical: %s\n  stance: %s\n",
               intent_token(eng->state.current_intent),
               rhet_token(p->rhetorical_mode),
               stance_token(p->stance));
        append(out_buf, cap, &pos, "  certainty=%d aggression=%d theatricality=%d hedging=%d\n",
               (int)p->certainty, (int)p->aggression,
               (int)p->theatricality, (int)p->hedging);
    }

    /* ---- USER INPUT (only when explicitly requested) ---- */
    if (cfg->include_current_input && user_input && user_input[0]){
        append(out_buf, cap, &pos, "USER_INPUT:\n  %.256s\n", user_input);
    }

    return pos;
}

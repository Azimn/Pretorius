/* turn_drama_test.c -- V7 turn drama synthesis. */
#include "../../core/persona.h"
#include "../../core/persona_internal.h"
#include "../../core/turn_drama.h"
#include "../../render/prompt_compiler.h"
#include "../../render/render_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)

static int has(const char *s, const char *needle){
    return s && needle && strstr(s, needle) != NULL;
}

static void reset_env(void){
    unsetenv("V6_PACKET_MODE");
    unsetenv("PE_OLLAMA_MODEL");
    unsetenv("PE_API_URL");
}

static void seed_engine(Engine *eng){
    memset(eng, 0, sizeof(*eng));
    snprintf(eng->identity.character_name, sizeof(eng->identity.character_name),
             "Test Character");
    eng->frame.speech_act = PE_SA_ASSERTION;
    eng->frame.forbid_meta = 1;
    eng->state.current_intent = PE_INTENT_ATTEND;
}

int main(void){
    Engine eng;
    TurnDrama td;
    V6UserTurnInterpretation it;
    RenderContext ctx;
    PromptCompilerConfig cfg;
    char prompt[PE_PROMPT_MAX_BYTES];
    int n;

    printf("--- V7 turn drama synthesis ---\n");

    seed_engine(&eng);
    memset(&it, 0, sizeof(it));
    it.user_act = "direct_question";
    it.response_move = "answer directly but in character";
    pe_synthesize_turn_drama(&eng, &it, &td);
    CHECK(has(td.surface_goal, "answer the direct question"),
          "direct question maps to answer-first surface goal");

    seed_engine(&eng);
    eng.state.imprint.pattern_confirmed = 1u;
    eng.state.imprint.dominant_slot = PE_BELIEF_DISRESPECT;
    it.user_act = "small_talk";
    pe_synthesize_turn_drama(&eng, &it, &td);
    CHECK(has(td.hidden_pressure, "disrespect"),
          "confirmed disrespect imprint becomes hidden pressure");

    seed_engine(&eng);
    eng.state.sovereign_override = 1u;
    pe_synthesize_turn_drama(&eng, &it, &td);
    CHECK(has(td.hidden_pressure, "unresolved matter"),
          "sovereign override becomes hidden pressure when no belief wins");

    seed_engine(&eng);
    it.user_act = "memory_probe";
    pe_synthesize_turn_drama(&eng, &it, &td);
    CHECK(has(td.forbidden_failure, "inventing a memory"),
          "memory probe forbids invented memory");

    seed_engine(&eng);
    it.user_act = "emotional_disclosure";
    pe_synthesize_turn_drama(&eng, &it, &td);
    CHECK(has(td.forbidden_failure, "analyzing the disclosure"),
          "emotional disclosure forbids analysis before acknowledgement");

    seed_engine(&eng);
    it.user_act = "direct_question";
    pe_synthesize_turn_drama(&eng, &it, &eng.state.turn_drama);
    memset(&ctx, 0, sizeof(ctx));
    ctx.npc = &eng;
    ctx.frame = &eng.frame;
    ctx.relation = &eng.relation;
    ctx.schema = &eng.schema;
    prompt_compiler_default_config(&cfg);
    setenv("V6_PACKET_MODE", "situation", 1);
    n = prompt_compile_with_input(&ctx, &cfg, "What happens next?",
                                  prompt, sizeof(prompt));
    CHECK(n > 0, "situation prompt compiles");
    CHECK(has(prompt, "[TURN_DRAMA]"), "situation packet emits turn drama block");
    CHECK(has(prompt, "surface_goal="), "turn drama block includes surface goal");
    CHECK(has(prompt, "hidden_pressure="), "turn drama block includes hidden pressure");
    CHECK(has(prompt, "relationship_move="), "turn drama block includes relationship move");
    CHECK(has(prompt, "forbidden_failure="), "turn drama block includes forbidden failure");
    reset_env();

    n = prompt_compile_with_input(&ctx, &cfg, "What happens next?",
                                  prompt, sizeof(prompt));
    CHECK(n > 0, "default prompt compiles");
    CHECK(!has(prompt, "[TURN_DRAMA]"),
          "non-situation packet omits turn drama block");

    if (fails){
        printf("FAILED -- %d turn drama assertion(s)\n", fails);
        return 1;
    }
    printf("PASSED -- V7 turn drama synthesis is deterministic and packetized\n");
    return 0;
}

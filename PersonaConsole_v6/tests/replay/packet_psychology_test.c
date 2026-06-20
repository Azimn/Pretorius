/* packet_psychology_test.c -- V6 situation packet psychology overlays. */
#include "../../render/prompt_compiler.h"
#include "../../core/persona.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)

static int has(const char *s, const char *needle){
    return s && needle && strstr(s, needle) != NULL;
}

int main(void){
    static Engine eng;
    char prompt[PE_PROMPT_MAX_BYTES];
    PromptCompilerConfig cfg;
    RenderContext ctx;
    V6UserTurnInterpretation it;
    int n;

    printf("--- V6 packet psychology ---\n");
    memset(&eng, 0, sizeof(eng));
    memset(&ctx, 0, sizeof(ctx));
    memset(&it, 0, sizeof(it));

    snprintf(eng.identity.character_name, sizeof(eng.identity.character_name), "Dr. Septimus Pretorius");
    snprintf(eng.relation.known_as, sizeof(eng.relation.known_as), "Kiki");
    eng.relation.disposition = 620;
    eng.relation_dims.trust = 260;
    eng.relation_dims.threat = 740;
    eng.relation_dims.intimacy = 120;
    eng.state.mood = -300;
    eng.state.exhaustion = 40;
    eng.state.obsession_pressure = 80;
    eng.state.last_input_emotion.arousal = 45;
    eng.expression_policy = PE_EXPR_MASKED;

    eng.topics.count = 1;
    eng.topics.topics[0].id = 7;
    snprintf(eng.topics.topics[0].name, sizeof(eng.topics.topics[0].name), "electricity");

    eng.theory_of_mind.believed_valence = -220;
    eng.theory_of_mind.believed_arousal = 510;
    eng.theory_of_mind.believed_goal_topic = 7;
    eng.theory_of_mind.confidence = 440;
    eng.theory_of_mind.mismatch_count = 3;

    eng.frame.primary_topic = 7;
    eng.frame.speech_act = PE_SA_ASSERTION;
    eng.frame.rhetorical_mode = 0;
    eng.frame.forbid_meta = 1;

    ctx.npc = &eng;
    ctx.frame = &eng.frame;
    ctx.relation = &eng.relation;

    prompt_compiler_default_config(&cfg);
    setenv("V6_PACKET_MODE", "situation", 1);
    n = prompt_compile_with_input(&ctx, &cfg,
                                  "I am fine. Stop reading me like that.",
                                  prompt, sizeof(prompt));
    unsetenv("V6_PACKET_MODE");

    CHECK(n > 0, "situation packet compiles");
    CHECK(has(prompt, "[PSYCHOLOGY]"), "packet includes psychology section");
    CHECK(has(prompt, "theory_of_mind=belief_not_fact"),
          "believed-user mood is labeled as belief, not fact");
    CHECK(has(prompt, "believed_user_valence=-220(negative)"),
          "believed user valence appears");
    CHECK(has(prompt, "believed_user_arousal=510(raised)"),
          "believed user arousal appears");
    CHECK(has(prompt, "believed_goal_topic=electricity"),
          "believed goal topic appears by topic name");
    CHECK(has(prompt, "mismatch_count=3"),
          "ToM mismatch count appears");
    CHECK(has(prompt, "contagion_adjusted_affect=post_contagion:negative"),
          "contagion-adjusted affect appears as post-contagion mood");
    CHECK(has(prompt, "baseline_disposition=620"),
          "baseline disposition is distinct from contagion-adjusted affect");
    CHECK(has(prompt, "expression_policy=masked"),
          "masked expression policy appears");
    CHECK(has(prompt, "Do not state hidden mood, raw valence, private thought"),
          "masked policy instructs renderer not to leak internal state");
    CHECK(!has(prompt, "-300"),
          "masked packet does not reveal raw internal valence");

    v6_interpret_user_turn(&ctx, "I am fine. Stop reading me like that.", &it);
    CHECK(!strcmp(it.pressure, "uncertain user read"),
          "high mismatch changes conversational pressure");
    CHECK(!strcmp(it.response_move, "check your read of the user before pressing the prior agenda"),
          "high mismatch changes recommended response move");

    if (fails){
        printf("FAILED -- %d packet psychology assertion(s)\n", fails);
        return 1;
    }
    printf("PASSED -- V6 situation packet psychology is surfaced and bounded\n");
    return 0;
}

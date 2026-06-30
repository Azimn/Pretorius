/* v7_2_homunculus_vitality_test.c -- cartridge vitality stays distinct. */
#include "../../core/persona.h"
#include "../../core/vitality.h"
#include "../../render/prompt_compiler.h"
#include "../../render/render_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)

static int has(const char *s, const char *n){
    return s && n && strstr(s,n) != NULL;
}

static void clear_packet_env(void){
    unsetenv("V6_PACKET_MODE");
    unsetenv("PE_OLLAMA_MODEL");
    unsetenv("PE_API_URL");
}

static void setv(char dst[PE_VITALITY_TEXT_LEN], const char *s){
    snprintf(dst, PE_VITALITY_TEXT_LEN, "%s", s);
}

static void make_fixture(Engine *eng, const char *name, const char *stance,
                         const char *authority, const char *domains){
    memset(eng, 0, sizeof(*eng));
    snprintf(eng->identity.character_name, sizeof(eng->identity.character_name), "%s", name);
    pe_vitality_profile_init(&eng->vitality_profile);
    setv(eng->vitality_profile.social_stances[0], stance);
    snprintf(eng->vitality_profile.authority_style,
             sizeof(eng->vitality_profile.authority_style), "%s", authority);
    snprintf(eng->vitality_profile.metaphoric_domains,
             sizeof(eng->vitality_profile.metaphoric_domains), "%s", domains);
    setv(eng->vitality_profile.rhetorical_moves[0], "answer directly through this character's pressure");
    setv(eng->vitality_profile.forbidden_generic_phrases[0], "avoid helpdesk phrasing");
    eng->state.today_seed = 11;
    eng->state.rng_state = 17;
    eng->state.current_intent = PE_INTENT_ANSWER;
    eng->relation_dims.trust = 500;
    eng->relation_dims.intimacy = 500;
    pe_vitality_synthesize(eng);
}

static int compile_prompt(Engine *eng, char *prompt, size_t prompt_n, int tiny){
    RenderContext ctx;
    PromptCompilerConfig cfg;
    memset(&ctx, 0, sizeof(ctx));
    ctx.npc = eng;
    ctx.plan = &eng->plan;
    ctx.frame = &eng->frame;
    ctx.relation = &eng->relation;
    ctx.schema = &eng->schema;
    prompt_compiler_default_config(&cfg);
    if (tiny) cfg.render_profile = PE_SLM_PROFILE_TINY;
    return prompt_compile_with_input(&ctx, &cfg, "Tell me what this means.",
                                     prompt, (int)prompt_n);
}

int main(void){
    static Engine eng, old_cart, pretorius, kiki, r0r1;
    char prompt[PE_PROMPT_MAX_BYTES];
    char anchors[5][PE_VITALITY_FRAME_LONG];
    const char *names[5] = {"Devil NPC","Queen","Archbishop","Priest","Ballerina"};
    const char *stances[5] = {
        "predatory charm",
        "regal distance",
        "ritual judgment",
        "restrained mercy",
        "aesthetic severity"
    };
    const char *authority[5] = {
        "temptation through wit and infernal contract pressure",
        "command, poise, political calculation, sovereign restraint",
        "doctrine, guilt, ceremony, moral authority under ritual calm",
        "confession, compassion, spiritual tension, mercy under pressure",
        "discipline, grace, pain, body-memory, exacting poise"
    };
    const char *domains[5] = {
        "contracts, embers, bargains, velvet threat",
        "court, crown, treaty, marble hall",
        "altar, incense, vestment, tribunal",
        "confessional, candle, chapel, quiet absolution",
        "barre, blister, mirror, stage light"
    };

    printf("--- V7.2 homunculus vitality ---\n");
    clear_packet_env();

    CHECK(persona_open(&old_cart, "profiles/friendly") == 0,
          "old cartridge without vitality.bin still loads");
    CHECK(old_cart.vitality_frame.neutral == 1u,
          "missing vitality fields produce neutral fallback");
    pe_vitality_synthesize(&old_cart);
    CHECK(!has(old_cart.vitality_frame.style_anchor, "laboratory") &&
          !has(old_cart.vitality_frame.style_anchor, "gin"),
          "neutral fallback is not Pretorius fallback");
    persona_close(&old_cart);

    CHECK(persona_open(&pretorius, "profiles/pretorius") == 0,
          "Pretorius profile with vitality loads");
    pe_vitality_synthesize(&pretorius);
    compile_prompt(&pretorius, prompt, sizeof(prompt), 0);
    CHECK(has(prompt, "[VITALITY]"), "prompt compiler emits vitality section");
    CHECK(has(prompt, "scientific authority") || has(prompt, "occult science"),
          "Pretorius vitality strings appear from Pretorius data");
    persona_close(&pretorius);

    CHECK(persona_open(&kiki, "profiles/kiki") == 0,
          "Kiki profile with vitality loads");
    pe_vitality_synthesize(&kiki);
    compile_prompt(&kiki, prompt, sizeof(prompt), 0);
    CHECK(!has(prompt, "scientific authority") &&
          !has(prompt, "occult science") &&
          !has(prompt, "fragile creation") &&
          !has(prompt, "laboratory exactness"),
          "non-Pretorius prompt does not emit hardcoded Pretorius vitality");
    CHECK(has(prompt, "Cosmos") || has(prompt, "mixtapes"),
          "Kiki vitality strings appear from Kiki data");
    persona_close(&kiki);

    CHECK(persona_open(&r0r1, "profiles/r0r1") == 0,
          "R0-R1 profile with vitality loads");
    pe_vitality_synthesize(&r0r1);
    compile_prompt(&r0r1, prompt, sizeof(prompt), 0);
    CHECK(has(prompt, "sparkle circuits") || has(prompt, "child-safe helper"),
          "R0-R1 vitality strings appear from R0-R1 data");
    CHECK(!has(prompt, "scientific authority") &&
          !has(prompt, "occult science") &&
          !has(prompt, "Cosmos"),
          "R0-R1 prompt does not inherit Pretorius or Kiki vitality");
    persona_close(&r0r1);

    for (int i = 0; i < 5; ++i){
        make_fixture(&eng, names[i], stances[i], authority[i], domains[i]);
        snprintf(anchors[i], sizeof(anchors[i]), "%s", eng.vitality_frame.style_anchor);
        compile_prompt(&eng, prompt, sizeof(prompt), 0);
        CHECK(has(prompt, names[i]), "fixture prompt names the current character");
        CHECK(has(prompt, stances[i]) || has(prompt, authority[i]),
              "fixture contributes its own vitality pressure");
        CHECK(!has(prompt, "scientific authority") && !has(prompt, "my dear"),
              "fixture does not inherit Pretorius strings");
        CHECK(!has(prompt, "wife") && !has(prompt, "childhood") && !has(prompt, "mother"),
              "fixture does not invent user biography or private facts");
    }
    for (int i = 0; i < 5; ++i){
        for (int j = i + 1; j < 5; ++j){
            CHECK(strcmp(anchors[i], anchors[j]) != 0,
                  "synthetic fixtures produce distinct style anchors");
        }
    }

    make_fixture(&eng, "Queen", stances[1], authority[1], domains[1]);
    compile_prompt(&eng, prompt, sizeof(prompt), 1);
    CHECK(!has(prompt, "[EXAMPLES]") && !has(prompt, "<START>"),
          "tiny-model examples are absent unless cartridge-authored");
    CHECK(has(prompt, "memory_boundary=") &&
          has(prompt, "present-performance only"),
          "memory boundary language appears in prompt");
    {
        uint16_t before = eng.memory.episodic_count;
        compile_prompt(&eng, prompt, sizeof(prompt), 0);
        CHECK(eng.memory.episodic_count == before,
              "expressive imagery prompt does not create durable memory");
    }

    CHECK(pe_vitality_text_has_assistant_leak("Sure, I can help with that.") != 0,
          "generic assistant phrase is detected");
    CHECK(pe_vitality_text_has_assistant_leak("Here are some suggestions.") != 0,
          "suggestions phrase is detected");
    CHECK(pe_vitality_text_has_assistant_leak("Answer directly in character.") == 0,
          "neutral non-assistant instruction is allowed");

    make_fixture(&eng, "Priest", stances[3], authority[3], domains[3]);
    eng.schema.slot[SCHEMA_USER_HOSTILE] = 700;
    eng.schema.evidence[SCHEMA_USER_HOSTILE] = 4;
    pe_vitality_synthesize(&eng);
    CHECK(has(eng.vitality_frame.social_stance, "expects hostility"),
          "durable schema climate influences per-turn vitality posture");

    if (fails){
        printf("FAILED -- %d homunculus vitality assertion(s)\n", fails);
        return 1;
    }
    printf("PASSED -- Homunculus Vitality: pass; Character Distinction: pass; Generic Assistant Leakage: 0 hard failures; Memory Invention: 0 hard failures\n");
    return 0;
}

/* v4_modules_test.c — exercises the V4-specific subsystems in isolation.
 *
 * memory_firewall:  refuses renderer-sourced writes, allows USER_INPUT
 * schema_state:     event application moves slots in expected direction
 * affect_curve:     salience-weighted decay, hysteresis, habituation
 * render_backend:   registry returns 'template' as default
 * prompt_compiler:  emits a structured block (not lore)
 */
#include "../../memory/memory_firewall.h"
#include "../../memory/affect_curve.h"
#include "../../schema/schema_state.h"
#include "../../core/persona.h"
#include "../../core/persona_internal.h"
#include "../../render/render_backend.h"
#include "../../render/prompt_compiler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++g_fail; } \
} while (0)

static void test_firewall(void){
    memory_firewall_reset_stats();

    MemoryWriteTicket t_user = {
        .source = PE_SRC_USER_INPUT, .turn_count = 1,
        .relation_idx = 0, .event_label = "test_user",
    };
    MemoryWriteTicket t_render = {
        .source = PE_SRC_RENDERER_OUTPUT, .turn_count = 1,
        .relation_idx = 0, .event_label = "test_render",
    };
    MemoryWriteTicket t_tick = {
        .source = PE_SRC_SYSTEM_TICK, .turn_count = 1,
        .relation_idx = 0, .event_label = "test_tick",
    };

    CHECK(memory_firewall_check(&t_user)   == PE_FW_OK,
          "firewall: USER_INPUT allowed");
    CHECK(memory_firewall_check(&t_tick)   == PE_FW_OK,
          "firewall: SYSTEM_TICK allowed");
    CHECK(memory_firewall_check(&t_render) == PE_FW_DENY_RENDERER_WRITE,
          "firewall: RENDERER_OUTPUT denied");
    CHECK(memory_firewall_check(NULL)      == PE_FW_DENY_UNATTRIBUTED,
          "firewall: NULL ticket denied");

    CHECK(memory_firewall_check_episodic(&t_user) == PE_FW_OK,
          "firewall: episodic USER_INPUT allowed");
    CHECK(memory_firewall_check_episodic(&t_tick) == PE_FW_DENY_TEXT_AS_EVENT,
          "firewall: episodic SYSTEM_TICK denied (must be user input)");
    CHECK(memory_firewall_check_episodic(&t_render) == PE_FW_DENY_RENDERER_WRITE,
          "firewall: episodic RENDERER_OUTPUT denied");

    FirewallStats s;
    memory_firewall_stats(&s);
    CHECK(s.denied_renderer >= 2, "firewall: stats track renderer denials");
}

static void test_schema(void){
    SchemaState s;
    schema_state_init(&s);
    CHECK(schema_get(&s, SCHEMA_USER_HOSTILE) == 0,
          "schema: fresh slot is zero");

    /* user insults us — hostility should rise */
    schema_apply_event(&s, SCHEMA_EVT_INSULTED_US, 200, NULL);
    int after_one = schema_get(&s, SCHEMA_USER_HOSTILE);
    CHECK(after_one > 0, "schema: insult raises hostility");

    /* repeated insult on next turn — habituation should weaken response */
    schema_apply_event(&s, SCHEMA_EVT_INSULTED_US, 200, NULL);
    int after_two = schema_get(&s, SCHEMA_USER_HOSTILE);
    /* second hit should not equal exactly 2× first (habituation) */
    CHECK(after_two > after_one && after_two < 2 * after_one,
          "schema: repeated insult is habituated");

    /* praise after insult — hysteresis should resist instant reversal */
    schema_apply_event(&s, SCHEMA_EVT_PRAISED_US, 200, NULL);
    int after_praise = schema_get(&s, SCHEMA_USER_HOSTILE);
    /* hostility may not fully neutralize; should still be > 0 */
    CHECK(after_praise > 0,
          "schema: hostility resists single counter-event (hysteresis)");

    /* tick forward 20 times — slots should decay */
    int pre_tick = schema_get(&s, SCHEMA_USER_HOSTILE);
    for (int i = 0; i < 20; ++i) schema_tick(&s);
    int post_tick = schema_get(&s, SCHEMA_USER_HOSTILE);
    CHECK(post_tick < pre_tick,
          "schema: idle ticks decay slot values");
}

static void test_affect_curves(void){
    /* high salience decays slower than low salience */
    int16_t v_high = affect_decay(1000, /*sal*/ 900, /*rate*/ 500);
    int16_t v_low  = affect_decay(1000, /*sal*/  100, /*rate*/ 500);
    CHECK(v_high > v_low,
          "affect_decay: high salience decays slower");

    /* hysteresis: same-sign delta works; opposite-sign attenuated */
    int16_t pos = affect_hysteresis_apply(800, 200);
    int16_t neg = affect_hysteresis_apply(800, -200);
    /* pos should saturate (large current damps same-sign delta) */
    CHECK(pos > 800 && pos <= 1000,
          "affect_hysteresis: same-sign delta still raises (damped)");
    /* neg should drop, but not by full 200 (reversal resistance) */
    CHECK(neg < 800 && neg > 800 - 200,
          "affect_hysteresis: opposite-sign resists full reversal");

    /* habituation halves each consecutive hit */
    int m1 = affect_habituate(128, 0, 0);
    int m2 = affect_habituate(128, 1, 0);
    int m4 = affect_habituate(128, 3, 0);
    int reset = affect_habituate(128, 5, /*gap*/ 1);  /* gap resets */
    CHECK(m1 == 128, "habituate: first hit unattenuated");
    CHECK(m2 < m1 && m2 >= m1 / 2 - 1, "habituate: second hit halved");
    CHECK(m4 < m2, "habituate: keeps halving");
    CHECK(reset == 128, "habituate: gap resets the counter");

    /* trait amplifier: high-N character has sticky hostility */
    TraitVec stable  = {50, 50, 50, 50,  20};
    TraitVec anxious = {50, 50, 50, 50, 100};
    int amp_stable  = affect_trait_amplifier(SCHEMA_USER_HOSTILE, &stable);
    int amp_anxious = affect_trait_amplifier(SCHEMA_USER_HOSTILE, &anxious);
    CHECK(amp_anxious > amp_stable,
          "trait_amplifier: high N amplifies SCHEMA_USER_HOSTILE");
}

static void test_address_gating(void){
    Engine eng;
    memset(&eng, 0, sizeof(eng));
    schema_state_init(&eng.schema);
    eng.relation.first_contact = (uint32_t)time(NULL) - (8u * 86400u);
    eng.relation.disposition = 500;
    CHECK(!pe_allow_intimate_address(&eng),
          "address_gate: low disposition blocks intimate address");

    eng.relation.disposition = 650;
    CHECK(!pe_allow_intimate_address(&eng),
          "address_gate: disposition alone is not enough");

    schema_apply_event(&eng.schema, SCHEMA_EVT_CONFIDED_IN_US, 200, NULL);
    CHECK(pe_allow_intimate_address(&eng),
          "address_gate: intimacy schema plus disposition unlocks intimate address");

    schema_apply_event(&eng.schema, SCHEMA_EVT_THREATENED_US, 200, NULL);
    CHECK(!pe_allow_intimate_address(&eng),
          "address_gate: high hostility blocks intimate address");

    schema_state_init(&eng.schema);
    schema_apply_event(&eng.schema, SCHEMA_EVT_CONFIDED_IN_US, 200, NULL);
    eng.relation.first_contact = (uint32_t)time(NULL) - (2u * 86400u);
    CHECK(!pe_allow_intimate_address(&eng),
          "address_gate: fresh relation cannot unlock intimate address immediately");
}

static int topic_momentum_for_test(const Engine *eng, uint16_t topic_id){
    for (int i = 0; i < PE_TOPIC_SLOTS; ++i)
        if (eng->state.topic_momentum[i].topic_id == topic_id)
            return eng->state.topic_momentum[i].momentum;
    return 0;
}

static void test_obsession_strength(void){
    Engine weak, strong, legacy;
    memset(&weak, 0, sizeof(weak));
    memset(&strong, 0, sizeof(strong));
    memset(&legacy, 0, sizeof(legacy));
    weak.identity.obsessions[0] = 42;
    strong.identity.obsessions[0] = 42;
    legacy.identity.obsessions[0] = 42;
    weak.identity.obsession_strength[0] = 20;
    strong.identity.obsession_strength[0] = 95;
    legacy.identity.obsession_strength[0] = 0;

    pe_update_topic_momentum(&weak);
    pe_update_topic_momentum(&strong);
    pe_update_topic_momentum(&legacy);
    CHECK(topic_momentum_for_test(&strong, 42) > topic_momentum_for_test(&weak, 42),
          "obsession_strength: stronger obsession adds more topic momentum");
    CHECK(topic_momentum_for_test(&legacy, 42) > topic_momentum_for_test(&weak, 42)
          && topic_momentum_for_test(&legacy, 42) < topic_momentum_for_test(&strong, 42),
          "obsession_strength: zero strength uses middle default");
}

static void test_render_backend_registry(void){
    /* template backend is auto-registered via __attribute__((constructor)).
     * slm backend likewise.  Both should be findable. */
    CHECK(render_backend_find("template") != NULL,
          "registry: template backend registered");
    CHECK(render_backend_find("slm") != NULL,
          "registry: slm backend registered");
    CHECK(render_backend_find("nonexistent") == NULL,
          "registry: unknown name returns NULL");
    CHECK(render_backend_default() != NULL,
          "registry: default backend exists");
    /* PE_RENDER_BACKEND unset → default should be 'template' */
    unsetenv("PE_RENDER_BACKEND");
    RenderBackend *def = render_backend_default();
    CHECK(def && !strcmp(def->name, "template"),
          "registry: default is template when env unset");
}

static void test_prompt_compiler(void){
    /* prompt_compile with NULL engine still emits at least the IDENTITY
     * header line — we want to confirm it produces structured output. */
    RenderContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    char buf[PE_PROMPT_MAX_BYTES];
    int n = prompt_compile(&ctx, NULL, buf, sizeof(buf));
    CHECK(n > 0, "prompt_compile: emits output for empty context");
    CHECK(strstr(buf, "[IDENTITY]") != NULL,
          "prompt_compile: starts with [IDENTITY] tag");
    /* Should NOT contain lore-style prose */
    CHECK(strstr(buf, "You are a ") == NULL &&
          strstr(buf, "Roleplay as") == NULL,
          "prompt_compile: not a lore dump / roleplay prompt");
    CHECK(strstr(buf, "[TASK]") != NULL,
          "prompt_compile: emits [TASK] instruction block");

    PromptCompilerConfig cfg;
    prompt_compiler_default_config(&cfg);
    cfg.render_profile = PE_SLM_PROFILE_TINY;
    n = prompt_compile(&ctx, &cfg, buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "renderer_profile=tiny") != NULL,
          "prompt_compile: tiny profile labeled");
    CHECK(strstr(buf, "Do not begin with weather") != NULL,
          "prompt_compile: tiny profile blocks filler");
    CHECK(strstr(buf, "[EXAMPLES]") != NULL &&
          strstr(buf, "<START>") != NULL &&
          strstr(buf, "Wait, which part") != NULL &&
          strstr(buf, "Sort of.") != NULL,
          "prompt_compile: tiny profile includes neutral fixed examples");

    cfg.render_profile = PE_SLM_PROFILE_BALANCED;
    n = prompt_compile(&ctx, &cfg, buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "renderer_profile=balanced") != NULL,
          "prompt_compile: balanced profile labeled");
    CHECK(strstr(buf, "do not compress a direct answer into fragments") != NULL,
          "prompt_compile: balanced profile favors concise answers");
    CHECK(strstr(buf, "[EXAMPLES]") == NULL,
          "prompt_compile: balanced profile omits tiny examples");

    cfg.render_profile = PE_SLM_PROFILE_EXPRESSIVE;
    n = prompt_compile(&ctx, &cfg, buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "renderer_profile=expressive") != NULL,
          "prompt_compile: expressive profile labeled");
    CHECK(strstr(buf, "Three or four sentences are fine") != NULL,
          "prompt_compile: expressive profile allows range");
    CHECK(strstr(buf, "[EXAMPLES]") == NULL,
          "prompt_compile: expressive profile omits tiny examples");

    cfg.render_profile = PE_SLM_PROFILE_TINY;
    cfg.chat_format = PE_SLM_CHAT_GEMMA;
    n = prompt_compile_with_input(&ctx, &cfg, "Good evening.", buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, "<start_of_turn>user") != NULL &&
          strstr(buf, "<start_of_turn>model") != NULL,
          "prompt_compile: gemma format uses native turn markers");
    CHECK(strstr(buf, "Understood.<end_of_turn>") != NULL &&
          strstr(buf, "Good evening.<end_of_turn>") != NULL,
          "prompt_compile: gemma format includes ack and live user turn");

    {
        setenv("V6_PACKET_MODE", "situation", 1);
        prompt_compiler_default_config(&cfg);
        n = prompt_compile_with_input(&ctx, &cfg,
                                      "I have this weird thought about code and lightning.",
                                      buf, sizeof(buf));
        CHECK(n > 0 && strstr(buf, "[USER_TURN_INTERPRETATION]") != NULL &&
              strstr(buf, "[RESPONSE_MOVE]") != NULL,
              "prompt_compile: situation packet emits interpretation and move");
        CHECK(strstr(buf, "The C runtime is the identity") != NULL,
              "prompt_compile: situation packet preserves Layer 1 authority");
        CHECK(strstr(buf, "You may use general knowledge or reason about topics outside memory") != NULL,
              "prompt_compile: situation packet allows non-memory topics without granting continuity authority");
        unsetenv("V6_PACKET_MODE");
        n = prompt_compile_with_input(&ctx, &cfg,
                                      "I have this weird thought about code and lightning.",
                                      buf, sizeof(buf));
        CHECK(n > 0 && strstr(buf, "[USER_TURN_INTERPRETATION]") == NULL,
              "prompt_compile: current packet remains default when flag unset");
    }
}

static void test_packet_interpretation(void){
    RenderContext ctx;
    V6UserTurnInterpretation it;
    memset(&ctx, 0, sizeof(ctx));

    v6_interpret_user_turn(&ctx,
        "What do you actually want from your little people in the jars?", &it);
    CHECK(!strcmp(it.user_act, "direct_question"),
          "packet interpretation: actually inside a question is not correction");

    v6_interpret_user_turn(&ctx,
        "Okay, so I have this totally weird thought. Modern code feels like spellwork with better shoes, but it still obeys rules, inputs, limits, symbols, consequences. That seems very you.", &it);
    CHECK(!strcmp(it.user_act, "rich_neutral_input"),
          "packet interpretation: internal 'but' does not force challenge");

    v6_interpret_user_turn(&ctx,
        "I get nervous people hear the slang and miss that I understand the math.", &it);
    CHECK(!strcmp(it.user_act, "emotional_disclosure"),
          "packet interpretation: nervous disclosure is emotional");

    v6_interpret_user_turn(&ctx,
        "Okay, your turn. What would you ask me if you weren't waiting for permission?", &it);
    CHECK(!strcmp(it.user_act, "open_ended_invitation"),
          "packet interpretation: user invitation is not generic question");

    v6_interpret_user_turn(&ctx,
        "Are you actually Pretorius right now, or just doing a cute impression?", &it);
    CHECK(!strcmp(it.user_act, "identity_test"),
          "packet interpretation: identity pressure is explicit");

    v6_interpret_user_turn(&ctx,
        "No working on a chatbot", &it);
    CHECK(!strcmp(it.user_act, "correction"),
          "packet interpretation: terse no-prefixed clarification counts as correction");

    v6_interpret_user_turn(&ctx,
        "I am exactly doing that lol", &it);
    CHECK(!strcmp(it.user_act, "continuation"),
          "packet interpretation: short affirmation stays on the current subject");

    v6_interpret_user_turn(&ctx,
        "What?", &it);
    CHECK(!strcmp(it.user_act, "clarification_probe"),
          "packet interpretation: short what-question is a clarification probe");
}

int main(void){
    printf("--- V4 modules test ---\n");
    render_backends_init();  /* explicit init for static-archive link */
    test_firewall();
    test_schema();
    test_affect_curves();
    test_address_gating();
    test_obsession_strength();
    test_render_backend_registry();
    test_prompt_compiler();
    test_packet_interpretation();
    if (g_fail){
        printf("FAILED — %d failure(s)\n", g_fail);
        return 1;
    }
    printf("PASSED — 0 failure(s)\n");
    return 0;
}

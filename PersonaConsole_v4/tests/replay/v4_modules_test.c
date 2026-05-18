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
#include "../../render/render_backend.h"
#include "../../render/prompt_compiler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

    char buf[1024];
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
}

int main(void){
    printf("--- V4 modules test ---\n");
    render_backends_init();  /* explicit init for static-archive link */
    test_firewall();
    test_schema();
    test_affect_curves();
    test_render_backend_registry();
    test_prompt_compiler();
    if (g_fail){
        printf("FAILED — %d failure(s)\n", g_fail);
        return 1;
    }
    printf("PASSED — 0 failure(s)\n");
    return 0;
}

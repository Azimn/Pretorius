/* belief_ledger_test.c -- V7 ghost-bond belief ledger. */
#include "../../core/persona.h"
#include "../../core/persona_internal.h"
#include "../../memory/belief_ledger.h"
#include "../../render/prompt_compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)

static int all_zero(const pe_belief_ledger_t *bl){
    for (int i = 0; i < PE_BELIEF_COUNT; ++i)
        if (bl->slots[i].pressure != 0 || bl->slots[i].evidence_count != 0) return 0;
    return 1;
}

static MemoryNode node(int8_t valence, int8_t arousal, int8_t dominance,
                       uint8_t salience, uint8_t core, uint16_t type){
    MemoryNode m;
    memset(&m, 0, sizeof(m));
    m.id = 1;
    m.type = type;
    m.salience = salience;
    m.emotion.valence = valence;
    m.emotion.arousal = arousal;
    m.emotion.dominance = dominance;
    m.core_memory = core;
    m.memory_type = core ? MEM_CORE : MEM_EPISODIC;
    snprintf(m.summary, sizeof(m.summary), "test memory");
    return m;
}

int main(void){
    printf("--- V7 belief ledger ---\n");

    pe_belief_ledger_t bl;
    pe_belief_ledger_init(&bl, 0xBEEFu);

    MemoryNode angry = node(-80, 75, 40, 120, 0, 0);
    pe_belief_absorb_memory_trace(&bl, &angry, 1);
    CHECK(bl.slots[PE_BELIEF_DISRESPECT].pressure > 0,
          "ghost bond: high-anger memory leaves disrespect trace");

    pe_belief_ledger_init(&bl, 0xBEEFu);
    MemoryNode dull = node(0, 10, 0, 12, 0, 0);
    pe_belief_absorb_memory_trace(&bl, &dull, 2);
    CHECK(all_zero(&bl), "safe forget: low-salience low-arousal memory leaves no trace");

    MemoryNode core = node(-90, 90, 80, 200, 1, 0);
    pe_belief_absorb_memory_trace(&bl, &core, 3);
    CHECK(all_zero(&bl), "core memory guard: core memories never feed decay scars");

    pe_belief_ledger_init(&bl, 0xBEEFu);
    pe_belief_absorb_memory_trace(&bl, &angry, 4);
    pe_belief_absorb_memory_trace(&bl, &angry, 5);
    pe_belief_absorb_memory_trace(&bl, &angry, 6);
    CHECK(bl.slots[PE_BELIEF_DISRESPECT].evidence_count >= 3 &&
          bl.slots[PE_BELIEF_DISRESPECT].pressure > 200,
          "pattern confirmation substrate: three strong insults exceed threshold");
    Engine eng;
    memset(&eng, 0, sizeof(eng));
    eng.belief_ledger = bl;
    pe_synthesize_imprint(&eng);
    CHECK(eng.state.imprint.pattern_confirmed == 1 &&
          eng.state.imprint.dominant_slot == PE_BELIEF_DISRESPECT,
          "experience imprint marks dominant confirmed pattern");

    int16_t before = bl.slots[PE_BELIEF_DISRESPECT].pressure;
    uint16_t evidence = bl.slots[PE_BELIEF_DISRESPECT].evidence_count;
    pe_belief_ledger_decay(&bl, 365u * 24u);
    CHECK(bl.slots[PE_BELIEF_DISRESPECT].pressure < before &&
          bl.slots[PE_BELIEF_DISRESPECT].pressure > 0 &&
          bl.slots[PE_BELIEF_DISRESPECT].evidence_count == evidence,
          "decay reduces pressure without erasing confirmed evidence");

    const char *dir = "tmp/belief_ledger_test";
    system("rm -rf tmp/belief_ledger_test");
    CHECK(pe_belief_ledger_save(&bl, dir) == 0, "belief ledger saves");
    pe_belief_ledger_t loaded;
    pe_belief_ledger_init(&loaded, 0);
    CHECK(pe_belief_ledger_load(&loaded, dir, 0xBEEFu) == 0 &&
          loaded.slots[PE_BELIEF_DISRESPECT].pressure == bl.slots[PE_BELIEF_DISRESPECT].pressure &&
          loaded.slots[PE_BELIEF_DISRESPECT].evidence_count == bl.slots[PE_BELIEF_DISRESPECT].evidence_count,
          "belief ledger persists and reloads");

    setenv("V6_PACKET_MODE", "situation", 1);
    eng.belief_ledger = bl;
    pe_synthesize_imprint(&eng);
    RenderContext ctx;
    PromptCompilerConfig cfg;
    char prompt[4096];
    memset(&ctx, 0, sizeof(ctx));
    prompt_compiler_default_config(&cfg);
    ctx.npc = &eng;
    int n = prompt_compile_with_input(&ctx, &cfg, "hello", prompt, sizeof(prompt));
    CHECK(n > 0 && strstr(prompt, "[IMPRINT]") != NULL &&
          strstr(prompt, "dominant=disrespect") != NULL,
          "situation packet surfaces compact imprint block");
    unsetenv("V6_PACKET_MODE");

    if (fails){
        printf("FAILED -- %d belief ledger checks\n", fails);
        return 1;
    }
    printf("PASSED -- V7 belief ledger\n");
    return 0;
}

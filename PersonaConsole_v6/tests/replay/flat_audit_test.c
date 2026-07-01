/* flat_audit_test.c -- V7 anti-flatness and echo audit checks. */
#include "../../core/persona.h"
#include "../../core/persona_internal.h"
#include "../../render/prompt_compiler.h"

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
    snprintf(eng->state.turn_drama.hidden_pressure,
             sizeof(eng->state.turn_drama.hidden_pressure),
             "accumulated disrespect from this user is active");
}

int main(void){
    Engine eng;
    uint8_t v;
    const char *repair;

    printf("--- V7 anti-flatness audit ---\n");

    seed_engine(&eng);
    setenv("V6_PACKET_MODE", "situation", 1);
    v = pe_audit_evaluate_for_test(&eng, "Tell me something.", "I understand.", 1);
    CHECK(v == PE_AUDIT_V_FLAT,
          "short pressured output without memory or style triggers flat audit");

    seed_engine(&eng);
    snprintf(eng.vitality_profile.recurring_images[0],
             sizeof(eng.vitality_profile.recurring_images[0]),
             "stars signal wires");
    v = pe_audit_evaluate_for_test(&eng, "Tell me something.",
                                   "The stars make that less simple.", 1);
    CHECK(v != PE_AUDIT_V_FLAT,
          "vitality image word prevents flat audit");

    seed_engine(&eng);
    v = pe_audit_evaluate_for_test(&eng,
        "Please explain dungeon master rules clearly tonight",
        "Dungeon master rules are clearly tonight's business.", 1);
    CHECK(v == PE_AUDIT_V_ECHO,
          "repeating four nontrivial user words in the opener triggers echo audit");

    seed_engine(&eng);
    snprintf(eng.state.turn_drama.hidden_pressure,
             sizeof(eng.state.turn_drama.hidden_pressure), "none");
    v = pe_audit_evaluate_for_test(&eng,
        "Please explain dungeon master rules clearly tonight",
        "Yes. We can build the table role step by step.", 1);
    CHECK(v != PE_AUDIT_V_ECHO,
          "different wording with same meaning does not trigger echo audit");

    CHECK(!pe_audit_violation_is_hard_for_test(PE_AUDIT_V_FLAT),
          "flat audit violation is soft");
    CHECK(!pe_audit_violation_is_hard_for_test(PE_AUDIT_V_ECHO),
          "echo audit violation is soft");
    repair = pe_repair_instruction_for_test(PE_AUDIT_V_FLAT);
    CHECK(has(repair, "too thin"), "flat repair instruction is specific");
    repair = pe_repair_instruction_for_test(PE_AUDIT_V_ECHO);
    CHECK(has(repair, "without mirroring"), "echo repair instruction is specific");

    reset_env();

    if (fails){
        printf("FAILED -- %d flat audit assertion(s)\n", fails);
        return 1;
    }
    printf("PASSED -- V7 flatness and echo audits are bounded and soft\n");
    return 0;
}

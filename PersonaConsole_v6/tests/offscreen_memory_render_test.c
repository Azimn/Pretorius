#include "persona.h"
#include "persona_internal.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int cond, const char *msg){
    if (!cond){
        printf("not ok: %s\n", msg);
        failures++;
    } else {
        printf("ok:   %s\n", msg);
    }
}

int main(void){
    Engine eng;
    char out[PE_TEMPLATE_TEXT];
    Template *t;
    MemoryNode *m;

    memset(&eng, 0, sizeof(eng));
    snprintf(eng.identity.character_name, sizeof(eng.identity.character_name), "Tester");
    snprintf(eng.identity.address_user_as[0], PE_ADDRESS_LEN, "my dear");
    snprintf(eng.identity.address_user_as[1], PE_ADDRESS_LEN, "my friend");
    eng.state.current_intent = PE_INTENT_REMINISCE;
    eng.state.mood = 0;
    eng.state.rng_state = 0x12345678u;
    eng.matched_group = 0xFFFF;
    eng.input_class = 0;
    eng.plan.callback_memory = 0;
    eng.plan.certainty = 255;
    eng.plan.aggression = 0;
    eng.plan.theatricality = 0;
    eng.plan.rhetorical_mode = PE_RHET_ASSERT;
    eng.plan.stance = PE_STANCE_NEUTRAL;

    eng.templates.count = 1;
    t = &eng.templates.entries[0];
    t->id = 1;
    t->group = 0xFFFF;
    t->intent = PE_INTENT_REMINISCE;
    t->mood_min = -1000;
    t->mood_max = 1000;
    t->drive_bias_id = -1;
    t->base_score = 1000;
    t->source = PE_TEMPLATE_SRC_CARTRIDGE;
    snprintf(t->text, sizeof(t->text), "{memory}. Yes, that comes back.");

    eng.memory.episodic_count = 1;
    m = &eng.memory.episodic[0];
    m->id = 42;
    m->memory_type = MEM_EPISODIC;
    m->topic_id = 1;
    m->salience = 90;
    snprintf(m->summary, sizeof(m->summary),
             "[offscreen] I pursued the work by convincing the gin to last.");
    eng.active_count = 1;
    eng.active_memories[0] = 0;
    eng.active_match[0] = 900;

    pe_generate_response(&eng, "continue", out, sizeof(out));

    check(strstr(out, "[offscreen]") == NULL,
          "offscreen memory marker is not rendered to dialogue");
    check(strstr(out, "I pursued the work") != NULL,
          "offscreen memory content still surfaces after marker strip");

    if (failures) return 1;
    printf("PASSED -- offscreen memory rendering clean\n");
    return 0;
}

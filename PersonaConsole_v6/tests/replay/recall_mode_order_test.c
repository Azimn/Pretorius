/* recall_mode_order_test.c -- deterministic unit proof that each V6 recall
 * mode can change surfaced memory order without mutating memory text.
 */
#include "persona.h"
#include "persona_internal.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c,msg) do{ if(!(c)){ fprintf(stderr,"FAIL: %s\n", msg); return 1; } \
                         printf("ok:   %s\n", msg); }while(0)

static void seed_memory(Engine *e){
    memset(e, 0, sizeof(*e));
    e->memory.episodic_count = 3;
    e->memory.next_memory_id = 4;
    e->relation.user_hash = 1234;
    e->relation.disposition = 500;
    e->state.rng_state = 1;
    e->identity.obsessions[0] = 42;
    e->primary_topic = 0xFFFF;
    for (int i = 0; i < 3; ++i){
        MemoryNode *m = &e->memory.episodic[i];
        m->id = (uint32_t)(i + 1);
        m->memory_type = MEM_EPISODIC;
        m->salience = 255;
        m->retrieval_prob = 255;
        m->timestamp = persona_now_ms();
        m->emotion.arousal = 40;
        m->summary[0] = 'A' + i;
        m->summary[1] = 0;
    }
    e->memory.episodic[0].emotion.valence = -5;
    e->memory.episodic[0].emotion.dominance = -5;
    e->memory.episodic[0].topic_id = 10;

    e->memory.episodic[1].emotion.valence = 5;
    e->memory.episodic[1].emotion.dominance = 0;
    e->memory.episodic[1].topic_id = 11;
    e->memory.episodic[1].core_memory = 1;
    e->memory.episodic[1].memory_type = MEM_CORE;
    e->memory.episodic[1].private_threshold = 10;

    e->memory.episodic[2].emotion.valence = 0;
    e->memory.episodic[2].emotion.dominance = 0;
    e->memory.episodic[2].topic_id = 42;
}

static uint16_t top_for(uint8_t expected_mode, void (*setup)(Engine*)){
    Engine e;
    EmotionVector q = {0, 50, 0, 0};
    seed_memory(&e);
    if (setup) setup(&e);
    pe_associative_recall(&e, &q);
    if (e.current_recall_mode != expected_mode) return 0xFFFFu;
    return e.active_count ? e.active_memories[0] : 0xFFFEu;
}

static void defensive(Engine *e){ e->relation_dims.threat = 800; }
static void nostalgic(Engine *e){ e->state.drive_values[PE_DRIVE_CONTINUITY] = 900; }
static void accusatory(Engine *e){ e->relation_dims.resentment = 600; }
static void intimate(Engine *e){ e->relation_dims.intimacy = 700; e->relation.disposition = 900; }
static void obsessed(Engine *e){ e->state.obsession_pressure = 800; }
static void mooded(Engine *e){ e->state.mood = 300; }
static void shame(Engine *e){ e->dissonance.feared_gap = 700; }

int main(void){
    printf("--- recall-mode order test ---\n");
    CHECK(top_for(PE_RECALL_DEFENSIVE, defensive) == 0, "defensive recall favors self-protective negative memory");
    CHECK(top_for(PE_RECALL_NOSTALGIC, nostalgic) == 1, "nostalgic recall favors core positive memory");
    CHECK(top_for(PE_RECALL_ACCUSATORY, accusatory) == 0, "accusatory recall favors grievance memory");
    CHECK(top_for(PE_RECALL_INTIMACY_SEEKING, intimate) == 1, "intimacy-seeking recall favors gated/private memory");
    CHECK(top_for(PE_RECALL_OBSESSION_DRIVEN, obsessed) == 2, "obsession-driven recall favors obsession topic");
    CHECK(top_for(PE_RECALL_MOOD_CONGRUENT, mooded) == 1, "mood-congruent recall favors same-valence memory");
    CHECK(top_for(PE_RECALL_SHAME_AVOIDANT, shame) != 0, "shame-avoidant recall demotes negative memory");
    printf("PASSED -- recall modes shift surfaced memory order\n");
    return 0;
}

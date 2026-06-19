/* learned_knowledge_test.c -- V6 learned knowledge sidecar unit suite. */
#include "../../core/persona.h"
#include "../../memory/learned_knowledge.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); fails++; } \
} while (0)

static pe_lk_write_t W(const char *topic, const char *claim,
                       uint8_t scope, uint8_t src, uint8_t tier,
                       uint8_t status, uint8_t auth, uint16_t conf,
                       uint32_t actor, uint32_t corrects){
    pe_lk_write_t w;
    memset(&w, 0, sizeof(w));
    w.topic_key = topic;
    w.claim_text = claim;
    w.scope = scope;
    w.source_type = src;
    w.source_tier = tier;
    w.status = status;
    w.authority_rank = auth;
    w.confidence = conf;
    w.source_actor_id = actor;
    w.source_actor_name = actor == 111 ? "Kiki" : actor == 222 ? "Mira" : "";
    w.correction_of_record_id = corrects;
    return w;
}

static uint32_t upsertW(pe_learned_knowledge_t *lk, const char *topic, const char *claim,
                        uint8_t scope, uint8_t src, uint8_t tier,
                        uint8_t status, uint8_t auth, uint16_t conf,
                        uint32_t actor, uint32_t corrects, uint32_t now){
    pe_lk_write_t w = W(topic, claim, scope, src, tier, status, auth, conf, actor, corrects);
    return pe_lk_upsert(lk, &w, now);
}

static void test_authority_and_corrections(void){
    pe_learned_knowledge_t lk;
    pe_lk_init(&lk);
    uint32_t model = upsertW(&lk, "electricity",
        "Model claimed electricity is positive charge flow.",
        PE_LK_SCOPE_REAL_WORLD, PE_LK_SRC_MODEL, PE_LK_TIER_SLM,
        PE_LK_STATUS_PROVISIONAL, 20, 420, 0, 0, 10);
    uint32_t user = upsertW(&lk, "electricity",
        "In metal wires, current is mostly electrons drifting through a conductor.",
        PE_LK_SCOPE_REAL_WORLD, PE_LK_SRC_USER, PE_LK_TIER_OFFLINE,
        PE_LK_STATUS_CONFIRMED, 80, 880, 111, model, 11);
    int amb = 0;
    const pe_lk_record_t *best = pe_lk_resolve(&lk, "electricity",
        PE_LK_SCOPE_REAL_WORLD, 111, &amb);
    CHECK(model != 0 && user != 0, "model and user records created");
    CHECK(best && best->record_id == user, "user correction outranks model claim");
    CHECK(!amb, "clear authority winner is not ambiguous");
    CHECK(lk.records[0].status == PE_LK_STATUS_CORRECTED, "corrected model record marked corrected");
    CHECK(lk.edge_count >= 1 && lk.edges[0].relation_type == PE_LK_EDGE_CORRECTS,
          "correction edge persisted in memory");
    CHECK(pe_lk_output_repeats_corrected_claim(&lk,
          "Electricity is positive charge flow.", "electricity"),
          "audit helper catches repeated corrected claim");
}

static void test_sources_scopes_and_conflicts(void){
    pe_learned_knowledge_t lk;
    pe_lk_init(&lk);
    upsertW(&lk, "vampire_lore", "Sunlight destroys vampires.",
        PE_LK_SCOPE_CARTRIDGE_CANON, PE_LK_SRC_CARTRIDGE, PE_LK_TIER_SYSTEM,
        PE_LK_STATUS_CARTRIDGE_AUTHORED, 100, 1000, 0, 0, 1);
    upsertW(&lk, "vampire_lore", "A model guessed vampires enjoy sun.",
        PE_LK_SCOPE_CARTRIDGE_CANON, PE_LK_SRC_MODEL, PE_LK_TIER_SLM,
        PE_LK_STATUS_PROVISIONAL, 15, 300, 0, 0, 2);
    upsertW(&lk, "market", "The inn rumor says the bridge is closed.",
        PE_LK_SCOPE_SIMULATION_WORLD, PE_LK_SRC_USER, PE_LK_TIER_OFFLINE,
        PE_LK_STATUS_PROVISIONAL, 35, 450, 111, 0, 3);
    upsertW(&lk, "market", "World state says the bridge is open.",
        PE_LK_SCOPE_SIMULATION_WORLD, PE_LK_SRC_WORLD, PE_LK_TIER_SYSTEM,
        PE_LK_STATUS_WORLD_AUTHORED, 95, 950, 0, 0, 4);
    upsertW(&lk, "favorite_color", "Kiki likes electric blue.",
        PE_LK_SCOPE_ACTOR_SPECIFIC, PE_LK_SRC_USER, PE_LK_TIER_OFFLINE,
        PE_LK_STATUS_CONFIRMED, 70, 800, 111, 0, 5);
    upsertW(&lk, "relationship_rule", "Mira hates being called kid.",
        PE_LK_SCOPE_RELATIONSHIP_SPECIFIC, PE_LK_SRC_USER, PE_LK_TIER_OFFLINE,
        PE_LK_STATUS_CONFIRMED, 70, 800, 222, 0, 6);
    upsertW(&lk, "session_hint", "This scene is a rehearsal.",
        PE_LK_SCOPE_SESSION_LOCAL, PE_LK_SRC_SYSTEM, PE_LK_TIER_SYSTEM,
        PE_LK_STATUS_CONFIRMED, 50, 600, 0, 0, 7);
    int amb = 0;
    const pe_lk_record_t *canon = pe_lk_resolve(&lk, "vampire_lore",
        PE_LK_SCOPE_CARTRIDGE_CANON, 0, &amb);
    const pe_lk_record_t *world = pe_lk_resolve(&lk, "market",
        PE_LK_SCOPE_SIMULATION_WORLD, 0, &amb);
    const pe_lk_record_t *actor_wrong = pe_lk_resolve(&lk, "favorite_color",
        PE_LK_SCOPE_ACTOR_SPECIFIC, 222, &amb);
    const pe_lk_record_t *rel_global = pe_lk_resolve(&lk, "relationship_rule",
        PE_LK_SCOPE_REAL_WORLD, 0, &amb);
    const pe_lk_record_t *session_global = pe_lk_resolve(&lk, "session_hint",
        PE_LK_SCOPE_REAL_WORLD, 0, &amb);
    CHECK(canon && canon->source_type == PE_LK_SRC_CARTRIDGE,
          "cartridge-authored knowledge outranks model within canon scope");
    CHECK(world && world->source_type == PE_LK_SRC_WORLD,
          "world-authored claim outranks rumor in simulation scope");
    CHECK(actor_wrong == NULL, "actor-specific claim does not leak to another actor");
    CHECK(rel_global == NULL, "relationship-specific claim does not become world knowledge");
    CHECK(session_global == NULL, "session-local claim does not survive as global knowledge");
}

static void test_dispute_reinforcement_capacity_and_persistence(void){
    pe_learned_knowledge_t lk;
    pe_lk_init(&lk);
    uint32_t a = upsertW(&lk, "ambiguous", "Claim A.",
        PE_LK_SCOPE_REAL_WORLD, PE_LK_SRC_USER, PE_LK_TIER_OFFLINE,
        PE_LK_STATUS_DISPUTED, 50, 500, 111, 0, 1);
    uint32_t b = upsertW(&lk, "ambiguous", "Claim B.",
        PE_LK_SCOPE_REAL_WORLD, PE_LK_SRC_USER, PE_LK_TIER_OFFLINE,
        PE_LK_STATUS_DISPUTED, 50, 500, 222, 0, 2);
    pe_lk_record_edge(&lk, a, PE_LK_EDGE_CONTRADICTS, b, 800, 200, 2);
    int amb = 0;
    const pe_lk_record_t *best = pe_lk_resolve(&lk, "ambiguous",
        PE_LK_SCOPE_REAL_WORLD, 0, &amb);
    CHECK(best != NULL && amb, "conflicting equal-authority claims produce ambiguity");
    upsertW(&lk, "reinforce", "Same claim.",
        PE_LK_SCOPE_REAL_WORLD, PE_LK_SRC_USER, PE_LK_TIER_OFFLINE,
        PE_LK_STATUS_CONFIRMED, 70, 700, 111, 0, 3);
    upsertW(&lk, "reinforce", "Same claim.",
        PE_LK_SCOPE_REAL_WORLD, PE_LK_SRC_USER, PE_LK_TIER_OFFLINE,
        PE_LK_STATUS_CONFIRMED, 70, 900, 111, 0, 4);
    best = pe_lk_resolve(&lk, "reinforce", PE_LK_SCOPE_REAL_WORLD, 0, NULL);
    CHECK(best && best->reinforcement_count == 1 && best->confidence == 900,
          "reinforcement updates existing claim without duplication");
    for (int i = 0; i < 90; ++i){
        char topic[32], claim[64];
        snprintf(topic, sizeof(topic), "cap_%02d", i);
        snprintf(claim, sizeof(claim), "capacity claim %02d", i);
        upsertW(&lk, topic, claim, PE_LK_SCOPE_REAL_WORLD,
            PE_LK_SRC_MODEL, PE_LK_TIER_SLM, PE_LK_STATUS_PROVISIONAL,
            10, 100, 0, 0, (uint32_t)(10 + i));
    }
    CHECK(lk.header.entry_count == PE_LK_RECORD_CAP, "capacity remains bounded");
}

static void test_load_save_and_corruption(void){
    char dir_template[] = "/tmp/pe_lk_XXXXXX";
    char *dir = mkdtemp(dir_template);
    CHECK(dir != NULL, "temp directory created");
    pe_learned_knowledge_t lk, loaded;
    pe_lk_init(&lk);
    uint32_t rec = upsertW(&lk, "persist", "Persistent claim.",
        PE_LK_SCOPE_REAL_WORLD, PE_LK_SRC_USER, PE_LK_TIER_OFFLINE,
        PE_LK_STATUS_CONFIRMED, 70, 777, 111, 0, 1);
    pe_lk_record_edge(&lk, rec, PE_LK_EDGE_SUPPORTS, rec, 300, 120, 1);
    CHECK(pe_lk_save(&lk, dir) == 0, "learned sidecar saved");
    CHECK(pe_lk_load(&loaded, dir) == 0, "learned sidecar loaded");
    CHECK(loaded.header.entry_count == 1 && loaded.edge_count == 1,
          "records and evidence/support edges persist");
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/learned_knowledge.bin", dir);
        FILE *f = fopen(path, "wb");
        fputs("bad", f);
        fclose(f);
    }
    pe_lk_init(&loaded);
    CHECK(pe_lk_load(&loaded, dir) == 0, "corrupt sidecar fails closed without crash");
    CHECK(loaded.header.entry_count == 0, "corrupt sidecar does not masquerade as valid");
}

int main(void){
    printf("--- V6 learned knowledge test ---\n");
    test_authority_and_corrections();
    test_sources_scopes_and_conflicts();
    test_dispute_reinforcement_capacity_and_persistence();
    test_load_save_and_corruption();
    if (fails){
        printf("FAILED -- %d failure(s)\n", fails);
        return 1;
    }
    printf("PASSED -- learned knowledge graph foundation\n");
    return 0;
}

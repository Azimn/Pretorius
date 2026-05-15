/* aether_pe_test.c — end-to-end: PersonaConsole + AETHER integration.
 *
 * Hypothesis under test:
 *   1. Committing more than PE_EPISODIC_MAX memories causes the engine to
 *      demote the evicted ones into AETHER (eng->aether->wal grows).
 *   2. After consolidation, AETHER recall via pe_associative_recall surfaces
 *      a cold-promoted memory whose text matches the query.
 *
 * Approach: open the engine on the pretorius cartridge (must be compiled
 * already — run `make character` first), set up a synthetic interlocutor
 * with full disposition so disclosure gates don't interfere, and drive the
 * commit + recall paths directly without going through process_input.
 */
#include "persona.h"
#include "persona_internal.h"
#include "aether.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)){ printf("FAIL: %s\n", msg); failures++; } \
    else        { printf("ok:   %s\n", msg); } \
} while (0)

/* Reset character runtime state between test runs. */
static void clean_runtime(const char *dir){
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "rm -f %s/state.bin %s/memory.bin %s/chapters.bin && "
             "rm -rf %s/relations %s/aether",
             dir, dir, dir, dir, dir);
    (void)!system(cmd);
}

int main(void){
    const char *DIR = "characters/pretorius";
    clean_runtime(DIR);

    Engine eng;
    int rc = persona_open(&eng, DIR);
    CHECK(rc == 0, "persona_open succeeds");
    if (rc != 0) return 1;

    CHECK(eng.aether != NULL, "AETHER opened by persona_open");

    /* Make the test interlocutor a confidant so private memories aren't
     * gated out of recall (cold-promoted nodes already bypass the gate,
     * but working-memory recall during dev printf still benefits). */
    persona_set_user(&eng, "test_user");
    eng.relation.disposition = 900;
    eng.relation.um_engagement = 200;

    /* ---------- 1. Plant a distinctive low-salience memory, then flood
     * with high-salience generic memories.  Eviction will pick the
     * low-salience one early — the special memory ends up in AETHER
     * while working memory holds only generic facts whose SimHash is
     * far from the special memory's. ---------- */
    EmotionVector ev;
    ev.valence = 30; ev.arousal = 50; ev.dominance = 20; ev._pad = 0;

    pe_commit_memory(&eng,
        "the unusual artefact discovered beneath the apothecary's floor",
        &ev, 0xFFFF, /*salience*/ 80, 0);

    const int N = 100;
    for (int i = 0; i < N; ++i){
        char summary[96];
        snprintf(summary, sizeof(summary),
                 "another routine evening conversation about Tuesday number %d",
                 i);
        pe_commit_memory(&eng, summary, &ev, 0xFFFF, /*salience*/ 200, 0);
    }

    CHECK(eng.memory.episodic_count == PE_EPISODIC_MAX,
          "working memory saturates at PE_EPISODIC_MAX after flood");

    aether_stats_t st;
    aether_stats(eng.aether, &st);
    int demoted = (int)st.wal_event_count;
    /* Pretorius ships core seeds; we committed 1 special + N generics.
     * Capacity for non-core is (PE_EPISODIC_MAX - core_count), so total
     * evictions are (1 + N) - non_core_cap. */
    int core_count = eng.identity.core_memory_count;
    int non_core_cap = PE_EPISODIC_MAX - core_count;
    int expected   = (1 + N) - non_core_cap;
    printf("note: AETHER WAL holds %d demoted events (expected ~%d)\n",
           demoted, expected);
    CHECK(demoted >= expected - 5 && demoted <= expected + 5,
          "AETHER WAL count matches (1 + N - non-core-capacity)");

    /* ---------- 2. Force consolidation: drain WAL into bucket files ---------- */
    rc = aether_consolidate(eng.aether, 0 /*incremental*/);
    CHECK(rc == 0, "aether_consolidate returns 0");
    aether_stats(eng.aether, &st);
    CHECK(st.wal_event_count == 0, "WAL drained by consolidation");

    /* ---------- 3. Query for a demoted memory via pe_associative_recall ---------- */
    /* Pick an early-flooded fact — it was evicted long before flood end. */
    /* Diagnostic: direct AETHER query with the unique special-phrase text.
     * If this returns 0, the bucket scan can't find it.  If >=1, the cold
     * recall path should also fire (working memory has only generic
     * Tuesday-evening text and won't match the special phrase well). */
    aether_event_t direct[5];
    int direct_n = aether_query_by_text(eng.aether,
        "the unusual artefact discovered beneath the apothecary's floor",
        direct, 5, 0);
    printf("note: direct AETHER query for exact-text returns %d result(s)\n",
           direct_n);

    const char *query = "the unusual artefact discovered beneath the apothecary's floor";
    pe_prep_input(&eng, query);

    /* Realistic recall scenario: the user asks about the artefact in a
     * different emotional register than the routine memories were
     * committed under.  This depresses working-memory match scores via
     * VAD distance, forcing the AETHER fallback to fire. */
    EmotionVector qev = {0};
    qev.valence = -80; qev.arousal = 90; qev.dominance = -40;
    pe_associative_recall(&eng, &qev);

    printf("note: cold_scratch_count=%u active_count=%u, active_match[0]=%u\n",
           eng.cold_scratch_count, eng.active_count,
           eng.active_count > 0 ? eng.active_match[0] : 0);

    CHECK(eng.cold_scratch_count > 0,
          "cold_scratch populated by AETHER fallback");

    int cold_in_active = 0;
    int found_artefact = 0;
    for (uint16_t i = 0; i < eng.active_count; ++i){
        if (eng.active_memories[i] >= PE_EPISODIC_MAX){
            cold_in_active = 1;
            const MemoryNode *n = pe_active_node(&eng, eng.active_memories[i]);
            if (n && strstr(n->summary, "artefact")) found_artefact = 1;
        }
    }
    CHECK(cold_in_active, "active_memories contains a sentinel (cold) index");
    CHECK(found_artefact, "cold-promoted node summary contains 'artefact'");

    /* ---------- 5. Top-K ordering: highest match first ---------- */
    if (eng.active_count >= 2){
        int monotone = 1;
        for (uint16_t i = 1; i < eng.active_count; ++i)
            if (eng.active_match[i] > eng.active_match[i - 1]) monotone = 0;
        CHECK(monotone, "active_match is sorted descending");
    }

    persona_close(&eng);
    clean_runtime(DIR);

    printf("\n%s — %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

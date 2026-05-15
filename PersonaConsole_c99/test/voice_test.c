/* voice_test.c — counterfactual rerank assertions.
 *
 * Plants two candidates with sharply different intents and verifies
 * pe_voice_rerank shifts scores in the expected direction relative to
 * the current goal's preferred-reaction shape.
 */
#include "persona.h"
#include "persona_internal.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)){ printf("FAIL: %s\n", msg); failures++; } \
    else        { printf("ok:   %s\n", msg); } \
} while (0)

static void rm_rf(const char *path){
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    (void)!system(cmd);
}

/* Find first template id with the given intent.  Returns 0xFFFF if none. */
static uint16_t find_template_by_intent(const Engine *eng, uint8_t intent){
    for (uint32_t i = 0; i < eng->templates.count; ++i)
        if (eng->templates.entries[i].intent == intent) return (uint16_t)i;
    return 0xFFFF;
}

static uint16_t find_goal_by_intent(const Engine *eng, uint16_t intent){
    for (uint32_t i = 0; i < eng->goals.count; ++i)
        if (eng->goals.entries[i].intent_id == intent) return (uint16_t)i;
    return 0xFFFF;
}

int main(void){
    const char *DIR = "characters/pretorius";
    rm_rf("characters/pretorius/state.bin");
    rm_rf("characters/pretorius/memory.bin");
    rm_rf("characters/pretorius/chapters.bin");
    rm_rf("characters/pretorius/relations");
    rm_rf("characters/pretorius/aether");

    Engine eng;
    int rc = persona_open(&eng, DIR);
    CHECK(rc == 0, "persona_open succeeds");
    if (rc != 0) return 1;
    persona_set_user(&eng, "voice_tester");

    /* Find candidates of two markedly different intents.
     *   BOAST   — speaker reaction predicted: praise, valence ~+20
     *   ACCUSE  — speaker reaction predicted: threat, valence ~-40 */
    uint16_t boast_tid  = find_template_by_intent(&eng, PE_INTENT_BOAST);
    uint16_t accuse_tid = find_template_by_intent(&eng, PE_INTENT_ACCUSE);
    CHECK(boast_tid  != 0xFFFF, "found a BOAST template in Pretorius");
    CHECK(accuse_tid != 0xFFFF, "found an ACCUSE template in Pretorius");
    if (boast_tid == 0xFFFF || accuse_tid == 0xFFFF){
        persona_close(&eng);
        return 1;
    }

    /* --- scenario 1: goal = boast (wants praise, valence +1, arousal 0)
     *
     *  Candidate BOAST predicts praise +20  → alignment +20*1 + 30*0 = +20
     *  Candidate ACCUSE predicts threat -40 → alignment -40*1 + 70*0 = -40
     *  Boast should rise; accuse should fall. */
    {
        eng.candidate_count = 2;
        eng.candidate_ids[0]    = boast_tid;
        eng.candidate_scores[0] = 100;
        eng.candidate_ids[1]    = accuse_tid;
        eng.candidate_scores[1] = 100;

        uint16_t boast_goal = find_goal_by_intent(&eng, PE_INTENT_BOAST);
        CHECK(boast_goal != 0xFFFF, "found a boast-intent goal");
        eng.state.current_goal = boast_goal;
        /* Reset UserModel so the hostile damping path doesn't fire. */
        eng.relation.um_belief_about_me = 0;

        pe_voice_rerank(&eng);

        CHECK(eng.candidate_scores[0] > eng.candidate_scores[1],
              "boast goal → BOAST candidate now scores higher than ACCUSE");
        CHECK(eng.candidate_scores[0] > 100,
              "boast goal → BOAST candidate score increased");
        CHECK(eng.candidate_scores[1] < 100,
              "boast goal → ACCUSE candidate score decreased");
    }

    /* --- scenario 2: goal = vindicate / accuse (val_pref -1, aro_pref +1)
     *
     *  Candidate BOAST predicts praise (+20, aro 30) → -20 + 30 = +10
     *  Candidate ACCUSE predicts threat (-40, aro 70)→ +40 + 70 = +110
     *  Accuse should rise much more than boast. */
    {
        uint16_t accuse_goal = find_goal_by_intent(&eng, PE_INTENT_ACCUSE);
        if (accuse_goal == 0xFFFF){
            printf("note: no ACCUSE-intent goal in Pretorius; skipping scenario 2\n");
        } else {
            eng.candidate_count = 2;
            eng.candidate_ids[0]    = boast_tid;
            eng.candidate_scores[0] = 100;
            eng.candidate_ids[1]    = accuse_tid;
            eng.candidate_scores[1] = 100;
            eng.state.current_goal  = accuse_goal;
            eng.relation.um_belief_about_me = 0;

            pe_voice_rerank(&eng);

            CHECK(eng.candidate_scores[1] > eng.candidate_scores[0],
                  "vindicate goal → ACCUSE outranks BOAST");
        }
    }

    /* --- scenario 3: hostile speaker.
     *
     *  The hostile-collapse branch should damp BOAST's praise prediction
     *  toward insult.  With goal=boast (val_pref +1), boost shrinks. */
    {
        eng.candidate_count = 1;
        eng.candidate_ids[0]    = boast_tid;
        eng.candidate_scores[0] = 100;
        uint16_t boast_goal = find_goal_by_intent(&eng, PE_INTENT_BOAST);
        eng.state.current_goal  = boast_goal;

        eng.relation.um_belief_about_me = -80;
        pe_voice_rerank(&eng);
        int32_t hostile_score = eng.candidate_scores[0];

        /* reset and rerun with friendly speaker */
        eng.candidate_scores[0] = 100;
        eng.relation.um_belief_about_me = 0;
        pe_voice_rerank(&eng);
        int32_t friendly_score = eng.candidate_scores[0];

        CHECK(friendly_score > hostile_score,
              "hostile speaker damps the voice's expected praise reward");
    }

    /* --- scenario 4: determinism — same inputs → same delta */
    {
        eng.candidate_count = 1;
        eng.candidate_ids[0]    = boast_tid;
        eng.candidate_scores[0] = 0;
        eng.state.current_goal  = find_goal_by_intent(&eng, PE_INTENT_BOAST);
        eng.relation.um_belief_about_me = 0;
        pe_voice_rerank(&eng);
        int32_t a = eng.candidate_scores[0];

        eng.candidate_scores[0] = 0;
        pe_voice_rerank(&eng);
        int32_t b = eng.candidate_scores[0];
        CHECK(a == b, "rerank is deterministic across replays");
    }

    /* --- scenario 5: instrumentation field exposed for :dump */
    {
        eng.candidate_count = 2;
        eng.candidate_ids[0]    = boast_tid;
        eng.candidate_scores[0] = 100;
        eng.candidate_ids[1]    = accuse_tid;
        eng.candidate_scores[1] = 100;
        eng.state.current_goal = find_goal_by_intent(&eng, PE_INTENT_BOAST);
        eng.relation.um_belief_about_me = 0;
        pe_voice_rerank(&eng);
        CHECK(eng.state.last_voice_choice == boast_tid,
              "last_voice_choice records the best-aligned candidate");
        CHECK(eng.state.last_voice_delta > 0,
              "last_voice_delta records the alignment bonus");
    }

    persona_close(&eng);
    rm_rf("characters/pretorius/state.bin");
    rm_rf("characters/pretorius/memory.bin");
    rm_rf("characters/pretorius/chapters.bin");
    rm_rf("characters/pretorius/relations");
    rm_rf("characters/pretorius/aether");

    printf("\n%s — %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

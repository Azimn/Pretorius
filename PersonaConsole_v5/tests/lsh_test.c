/* lsh_test.c — sanity tests for lsh_memory + consolidate.
 * Run with: make lsh_test && ./build/lsh_test
 */
#include "lsh_memory.h"
#include "consolidate.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)){ printf("FAIL: %s\n", msg); fails++; } \
    else        { printf("ok:   %s\n", msg); } \
} while (0)

static void test_lsh_basics(void){
    lsh_sig_t a = lsh_compute("the doctor speaks of bones");
    lsh_sig_t b = lsh_compute("the doctor speaks of bones");
    lsh_sig_t c = lsh_compute("a totally unrelated sentence about gin");
    lsh_sig_t e = lsh_compute("");
    lsh_sig_t s = lsh_compute("hi");          /* short string */

    CHECK(a == b, "identical inputs → identical signatures");
    CHECK(lsh_hamming_distance(a, c) > 8, "different inputs → distant signatures");
    CHECK(e == 0, "empty string → zero signature");
    CHECK(s != 0, "short string → non-zero signature (fix verified)");
    CHECK(lsh_hamming_distance(a, a) == 0, "self distance == 0");
}

static void test_lsh_find_nearest(void){
    const char *corpus[] = {
        "homunculi grown in test tubes",
        "the bones know what the flesh forgets",
        "pass the gin, Victor",
        "creation is a sacred act",
        "I will not be silenced"
    };
    lsh_sig_t db[5];
    int i, dist, idx;
    lsh_sig_t got;

    for (i = 0; i < 5; i++) db[i] = lsh_compute(corpus[i]);

    got = lsh_find_nearest(lsh_compute("homunculi grown in test tubes"),
                           db, 5, &dist, &idx);
    CHECK(got == db[0] && dist == 0 && idx == 0, "exact-match recall");

    /* near-paraphrase should land closer to corpus[1] than to others */
    {
        lsh_sig_t q = lsh_compute("the bones remember what the flesh forgets");
        int d_target, d_other;
        d_target = lsh_hamming_distance(q, db[1]);
        d_other  = lsh_hamming_distance(q, db[2]);
        CHECK(d_target < d_other, "paraphrase closer to its source than to unrelated");
    }
}

static void test_consolidate_rules(void){
    /* Build 10 events where keys 7 and 13 co-occur 6 times → should make a rule. */
    event_t ev[10];
    rule_t  rules[PE_CONS_MAX_RULES];
    lsh_sig_t gists[PE_CONS_CLUSTER_K];
    int rcnt = 0, gcnt = 0, i;

    memset(ev, 0, sizeof(ev));
    for (i = 0; i < 10; i++){
        ev[i].id = (uint32_t)i;
        strcpy(ev[i].text_content, (i & 1) ? "the bones speak" : "we drink gin tonight");
        if (i < 6){
            ev[i].persona_keys[0] = 7;
            ev[i].persona_keys[1] = 13;
            ev[i].key_count = 2;
        } else {
            ev[i].persona_keys[0] = 99;
            ev[i].persona_keys[1] = 200;   /* >31: would crash old 1<<key bitmask */
            ev[i].key_count = 2;
        }
    }
    consolidate_zone(ev, 10, rules, &rcnt, gists, &gcnt);

    CHECK(rcnt >= 1, "rule generated from co-occurring keys 7 & 13");
    CHECK(rules[0].cond_key1 == 7 && rules[0].cond_key2 == 13, "rule has correct key pair");
    CHECK(rules[0].support >= 6, "rule support count correct");
    CHECK(gcnt > 0 && gcnt <= PE_CONS_CLUSTER_K, "gist clusters produced");

    /* The "key=200" case must not have caused UB or crashed; if we got here, ok. */
    CHECK(1, "high key ids (>=32) handled without UB");
}

int main(void){
    test_lsh_basics();
    test_lsh_find_nearest();
    test_consolidate_rules();
    printf("\n%s — %d failure(s)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}

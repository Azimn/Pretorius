/* plasticity_test.c — exercise the n-gram LM and the mutator.
 *
 * Build:  make plasticity_test
 * Run:    make plasticity_run
 */
#include "ngram_lm.h"
#include "mutator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)){ printf("FAIL: %s\n", msg); fails++; } \
    else        { printf("ok:   %s\n", msg); } \
} while (0)

static void test_lm(const char *path){
    NGramLM *lm = ngram_lm_load(path);
    int32_t pretorian, modern, gibberish;
    CHECK(lm != NULL, "LM loads from disk");
    if (!lm) return;

    CHECK(ngram_lm_order(lm) >= 3, "LM has order >= 3");
    CHECK(ngram_lm_count_at_order(lm, 1) > 10, "LM has at least 10 unigrams");

    pretorian = ngram_lm_score_normalized(lm,
        "the bones know what the flesh forgets");
    modern    = ngram_lm_score_normalized(lm,
        "lol whatever bro that is totally rad");
    gibberish = ngram_lm_score_normalized(lm,
        "xqzj kvjt zwpf brnk vrqx tflm");

    printf("      pretorian:  %d  modern: %d  gibberish: %d (per-char milli-nats)\n",
           pretorian, modern, gibberish);

    CHECK(pretorian > modern,    "Pretorian text scores higher than modern slang");
    CHECK(modern    > gibberish, "Modern text scores higher than gibberish");

    /* A held-out near-paraphrase should still score well. */
    {
        int32_t paraphrase = ngram_lm_score_normalized(lm,
            "the marrow knows what the flesh has forgotten");
        printf("      paraphrase: %d\n", paraphrase);
        CHECK(paraphrase > modern, "Paraphrase still beats modern slang");
    }

    ngram_lm_free(lm);
}

static void test_mutator(void){
    char buf1[256], buf2[256], buf3[256];
    uint32_t rng = 12345;
    size_t n;

    CHECK(mutator_bank_count() > 5, "Mutator has multiple banks");

    n = mutator_expand("I shall [verb_create] a [adj_morbid] [noun_obsession][exclamation]",
                       buf1, sizeof(buf1), 200, 200, &rng);
    CHECK(n > 0, "Expansion produced output");
    printf("      expanded: %s\n", buf1);
    CHECK(strchr(buf1, '[') == NULL, "All markers were resolved");

    /* Determinism: same seed → same output. */
    rng = 12345;
    mutator_expand("I shall [verb_create] a [adj_morbid] [noun_obsession][exclamation]",
                   buf2, sizeof(buf2), 200, 200, &rng);
    CHECK(strcmp(buf1, buf2) == 0, "Same seed → identical expansion (deterministic)");

    /* Different seed → likely different output (probabilistic, but bank>1). */
    rng = 99999;
    mutator_expand("I shall [verb_create] a [adj_morbid] [noun_obsession][exclamation]",
                   buf3, sizeof(buf3), 200, 200, &rng);
    printf("      seed B:   %s\n", buf3);
    CHECK(strcmp(buf1, buf3) != 0, "Different seed → different expansion");

    /* Gating: low theatricality + low aggression should pin to entry 0. */
    rng = 12345;
    mutator_expand("a [adj_morbid] [adj_grand] world", buf1, sizeof(buf1),
                   0, 0, &rng);
    printf("      gated:    %s\n", buf1);
    CHECK(strstr(buf1, "grim") != NULL, "Low gates pin adj_morbid to entry 0 (grim)");
    CHECK(strstr(buf1, "magnificent") != NULL, "Low gates pin adj_grand to entry 0");

    /* Unknown marker pass-through. */
    n = mutator_expand("[unknown_bank] here", buf1, sizeof(buf1), 200, 200, &rng);
    CHECK(n > 0 && strstr(buf1, "[unknown_bank]") != NULL,
          "Unknown marker passes through verbatim");
}

int main(int argc, char **argv){
    const char *lm_path = (argc > 1) ? argv[1] : "data/pretorius.lm";
    printf("--- LM tests (%s) ---\n", lm_path);
    test_lm(lm_path);
    printf("\n--- Mutator tests ---\n");
    test_mutator();
    printf("\n%s — %d failure(s)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}

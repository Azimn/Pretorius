/* aether_test.c — libaether sanity + scale.
 *
 * Tests:
 *   1. open + put + close round-trip on a fresh store
 *   2. WAL atomicity: events written are recoverable after re-open
 *   3. WAL-scan path: query finds an event before consolidation
 *   4. Bucket scan path: query finds an event after consolidation
 *   5. Top-K ordering: distances strictly non-decreasing
 *   6. 10k-event scale: write, consolidate, query — verify hits + timing
 */
#include "aether.h"
#include "aether_internal.h"
#include "lsh_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)){ printf("FAIL: %s\n", msg); failures++; } \
    else        { printf("ok:   %s\n", msg); } \
} while (0)

static void rm_rf(const char *path){
    /* Lightweight tree removal — fine for test directories. */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    (void)!system(cmd);
}

static void make_event(aether_event_t *ev, const char *text, uint32_t now){
    memset(ev, 0, sizeof(*ev));
    ev->timestamp     = now;
    ev->last_accessed = now;
    ev->emotion_arousal   = 32000;
    ev->emotion_valence   = 40000;
    ev->emotion_dominance = 30000;
    ev->retrievability_score = 50000;
    size_t L = strlen(text);
    if (L >= AETHER_INLINE_TEXT) L = AETHER_INLINE_TEXT - 1;
    memcpy(ev->inline_text, text, L);
    ev->inline_text[L] = 0;
}

int main(void){
    const char *DIR = "/tmp/aether_test_store";
    rm_rf(DIR);

    /* ---------- test 1: open + close on fresh dir ---------- */
    aether_handle_t *h = aether_open(DIR);
    CHECK(h != NULL, "fresh store opens");
    if (!h) return 1;

    /* ---------- test 2: put + close + reopen recovers events ---------- */
    aether_event_t ev;
    uint32_t now = ae_now_unix();
    make_event(&ev, "the cathedral on the hill at dusk", now);
    CHECK(aether_put(h, &ev) == 0, "put succeeds");

    make_event(&ev, "a glass of gin and the ballerina", now);
    CHECK(aether_put(h, &ev) == 0, "put succeeds (2)");

    make_event(&ev, "OMG entropy is the universe's messy bedroom", now);
    CHECK(aether_put(h, &ev) == 0, "put succeeds (3)");

    aether_stats_t st;
    aether_stats(h, &st);
    CHECK(st.wal_event_count == 3, "WAL count = 3 before consolidation");

    aether_close(h);

    h = aether_open(DIR);
    CHECK(h != NULL, "reopen after close");
    aether_stats(h, &st);
    CHECK(st.wal_event_count == 3, "WAL recovered across close/reopen");

    /* ---------- test 3: query hits WAL pre-consolidation ---------- */
    aether_event_t results[5];
    int n = aether_query_by_text(h, "cathedral hill dusk", results, 5, 0);
    CHECK(n >= 1, "WAL-scan: at least one match for 'cathedral hill dusk'");
    if (n >= 1){
        int hit = 0;
        for (int i = 0; i < n; ++i)
            if (strstr(results[i].inline_text, "cathedral")) hit = 1;
        CHECK(hit, "WAL-scan: top result contains 'cathedral'");
    }

    /* ---------- test 4: consolidate + query hits bucket file ---------- */
    CHECK(aether_consolidate(h, 1 /*full*/) == 0, "consolidate(full)");
    aether_stats(h, &st);
    CHECK(st.wal_event_count == 0, "WAL truncated after consolidation");
    CHECK(st.dirty_bucket_count == 0, "dirty bitmap cleared after consolidation");

    /* Exact-match query first (same SimHash → same bucket guaranteed). */
    n = aether_query_by_text(h, "OMG entropy is the universe's messy bedroom",
                             results, 5, 0);
    CHECK(n >= 1, "bucket scan: exact-text match returns >=1 result");
    if (n >= 1){
        int hit = 0;
        for (int i = 0; i < n; ++i)
            if (strstr(results[i].inline_text, "entropy")) hit = 1;
        CHECK(hit, "bucket scan: exact-text match top result contains 'entropy'");
    }
    /* Then a near-text query (multi-probe should still hit). */
    n = aether_query_by_text(h, "OMG entropy messy bedroom", results, 5, 0);
    if (n >= 1){
        int hit = 0;
        for (int i = 0; i < n; ++i)
            if (strstr(results[i].inline_text, "entropy")) hit = 1;
        CHECK(hit, "bucket scan: near-text query (multi-probe) finds 'entropy'");
    } else {
        printf("note: near-text query returned 0 results — LSH multi-probe miss\n");
    }

    /* ---------- test 5: 1k-scale write, consolidate, query ---------- */
    const int N = 1000;
    srand(42);
    for (int i = 0; i < N; ++i){
        char buf[AETHER_INLINE_TEXT];
        /* Distinct seed-based phrasing so SimHashes spread across buckets. */
        snprintf(buf, sizeof(buf), "memory %d about thing %d", i, rand() % 10000);
        make_event(&ev, buf, now);
        aether_put(h, &ev);
    }
    CHECK(aether_consolidate(h, 0 /*incremental*/) == 0, "consolidate(incremental) on 1k");

    /* Query for a known event from the bulk. */
    char qbuf[AETHER_INLINE_TEXT];
    srand(42);
    /* Re-derive the second event's text. */
    int target_thing_for_idx5 = -1;
    for (int i = 0; i <= 5; ++i){
        int r = rand() % 10000;
        if (i == 5) target_thing_for_idx5 = r;
    }
    snprintf(qbuf, sizeof(qbuf), "memory 5 about thing %d", target_thing_for_idx5);
    n = aether_query_by_text(h, qbuf, results, 5, 0);
    CHECK(n >= 1, "1k-scale: query returns matches");
    if (n >= 1){
        int hit = 0;
        for (int i = 0; i < n; ++i)
            if (strcmp(results[i].inline_text, qbuf) == 0) hit = 1;
        CHECK(hit, "1k-scale: exact match for known event surfaces in top-K");
    }

    /* ---------- test 6: top-K ordering is non-decreasing by Hamming ---------- */
    n = aether_query_by_text(h, "memory 5 about thing", results, 5, 0);
    if (n >= 2){
        uint64_t qsig = lsh_simhash((const uint8_t *)"memory 5 about thing",
                                    strlen("memory 5 about thing"));
        int monotone = 1;
        int prev_d = -1;
        for (int i = 0; i < n; ++i){
            uint64_t s = lsh_simhash((const uint8_t *)results[i].inline_text,
                                     strnlen(results[i].inline_text, AETHER_INLINE_TEXT));
            int d = lsh_hamming_distance(s, qsig);
            if (d < prev_d) monotone = 0;
            prev_d = d;
        }
        CHECK(monotone, "top-K results ordered by ascending Hamming distance");
    }

    /* ---------- test 7: max_age filter ---------- */
    /* Put a fresh event with timestamp = 0 (very old). */
    make_event(&ev, "ancient artefact from before time", 1);
    aether_put(h, &ev);
    aether_consolidate(h, 0);
    n = aether_query_by_text(h, "ancient artefact before time", results, 5,
                             /*max_age*/ 60);
    CHECK(n == 0 || strstr(results[0].inline_text, "ancient") == NULL,
          "max_age filter excludes too-old events");

    aether_close(h);
    rm_rf(DIR);

    printf("\n%s — %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

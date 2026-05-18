/* consolidate.c — see consolidate.h. */
#include "consolidate.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t key1;
    uint16_t key2;
    uint32_t count;
} cooc_pair_t;

static int cmp_cooc_desc(const void *a, const void *b){
    uint32_t ca = ((const cooc_pair_t*)a)->count;
    uint32_t cb = ((const cooc_pair_t*)b)->count;
    if (cb > ca) return  1;
    if (cb < ca) return -1;
    return 0;
}

void generate_rules(const event_t *events, int event_count,
                    rule_t *rules, int *rule_count){
    cooc_pair_t *pairs;
    int pairs_cap;
    int pair_cnt = 0;
    int i, a, b, j;

    *rule_count = 0;
    if (event_count < 2) return;

    /* Worst case per event: C(8,2) = 28 distinct pairs. Cap generously
     * at 32 per event; dedup will keep the working set small in practice. */
    pairs_cap = event_count * 32;
    if (pairs_cap < 64) pairs_cap = 64;
    pairs = (cooc_pair_t*)malloc(sizeof(cooc_pair_t) * (size_t)pairs_cap);
    if (!pairs) return;

    for (i = 0; i < event_count; i++){
        int kc = events[i].key_count;
        if (kc > PE_EVENT_KEYS_MAX) kc = PE_EVENT_KEYS_MAX;
        for (a = 0; a < kc; a++){
            for (b = a + 1; b < kc; b++){
                uint16_t k1 = events[i].persona_keys[a];
                uint16_t k2 = events[i].persona_keys[b];
                int found = -1;
                if (k1 == k2) continue;
                if (k1 > k2){ uint16_t t = k1; k1 = k2; k2 = t; }
                for (j = 0; j < pair_cnt; j++){
                    if (pairs[j].key1 == k1 && pairs[j].key2 == k2){
                        found = j; break;
                    }
                }
                if (found >= 0){
                    pairs[found].count++;
                } else if (pair_cnt < pairs_cap){
                    pairs[pair_cnt].key1  = k1;
                    pairs[pair_cnt].key2  = k2;
                    pairs[pair_cnt].count = 1;
                    pair_cnt++;
                }
                /* else: silently drop — extremely rare with dedup. */
            }
        }
    }

    qsort(pairs, (size_t)pair_cnt, sizeof(cooc_pair_t), cmp_cooc_desc);

    for (i = 0; i < pair_cnt && *rule_count < PE_CONS_MAX_RULES; i++){
        if (pairs[i].count >= (uint32_t)PE_COOC_THRESHOLD){
            rule_t *r = &rules[*rule_count];
            r->cond_key1       = pairs[i].key1;
            r->cond_key2       = pairs[i].key2;
            r->action_key_id   = 0;
            r->action_value_id = 0;
            r->support         = pairs[i].count;
            r->operator        = RULE_AND;
            (*rule_count)++;
        } else {
            /* sorted desc — nothing past this point will meet threshold */
            break;
        }
    }

    free(pairs);
}

int build_gist_summaries(const event_t *events, int event_count,
                         lsh_sig_t *summaries, int *summary_count){
    lsh_sig_t *sigs;
    lsh_sig_t centroids[PE_CONS_CLUSTER_K];
    uint8_t *assignment;
    int k, i, c, b, iter, changed;

    *summary_count = 0;
    if (event_count == 0) return 0;

    sigs       = (lsh_sig_t*)malloc(sizeof(lsh_sig_t) * (size_t)event_count);
    assignment = (uint8_t*)  calloc((size_t)event_count, 1);   /* zero-init: kills UB */
    if (!sigs || !assignment){
        free(sigs);
        free(assignment);
        return -1;
    }

    for (i = 0; i < event_count; i++){
        sigs[i] = lsh_compute(events[i].text_content);
    }

    k = (event_count < PE_CONS_CLUSTER_K) ? event_count : PE_CONS_CLUSTER_K;
    for (i = 0; i < k; i++) centroids[i] = sigs[i];

    /* Mark assignments dirty so the first pass always counts as changed
     * relative to the initial all-zero state. We use 0xFF as a sentinel. */
    memset(assignment, 0xFF, (size_t)event_count);

    for (iter = 0; iter < PE_CONS_MAX_ITER; iter++){
        changed = 0;
        for (i = 0; i < event_count; i++){
            int best = 0;
            int best_dist = lsh_hamming_distance(sigs[i], centroids[0]);
            for (c = 1; c < k; c++){
                int d = lsh_hamming_distance(sigs[i], centroids[c]);
                if (d < best_dist){ best_dist = d; best = c; }
            }
            if (assignment[i] != (uint8_t)best){
                assignment[i] = (uint8_t)best;
                changed = 1;
            }
        }
        if (!changed) break;

        /* Vertical bitwise MAJORITY vote per cluster.
         * For each of the 64 bit positions, count how many cluster members
         * have that bit set; if strictly more than half, the centroid's bit
         * is 1. (XOR would be parity, NOT majority — that bug stays dead.) */
        for (c = 0; c < k; c++){
            int bit_counts[64];
            int member_cnt = 0;
            uint64_t new_centroid = 0;
            for (b = 0; b < 64; b++) bit_counts[b] = 0;

            for (i = 0; i < event_count; i++){
                if (assignment[i] == (uint8_t)c){
                    lsh_sig_t s = sigs[i];
                    member_cnt++;
                    for (b = 0; b < 64; b++){
                        if (s & ((uint64_t)1 << b)) bit_counts[b]++;
                    }
                }
            }
            if (member_cnt > 0){
                int half = member_cnt / 2;
                for (b = 0; b < 64; b++){
                    if (bit_counts[b] > half) new_centroid |= ((uint64_t)1 << b);
                }
                centroids[c] = new_centroid;
            }
        }
    }

    *summary_count = k;
    for (i = 0; i < k; i++) summaries[i] = centroids[i];

    free(sigs);
    free(assignment);
    return 0;
}

int consolidate_zone(const event_t *events, int event_cnt,
                     rule_t *out_rules, int *out_rule_cnt,
                     lsh_sig_t *out_gists, int *out_gist_cnt){
    *out_rule_cnt = 0;
    *out_gist_cnt = 0;
    generate_rules(events, event_cnt, out_rules, out_rule_cnt);
    if (build_gist_summaries(events, event_cnt, out_gists, out_gist_cnt) < 0) return -1;
    return (*out_rule_cnt) + (*out_gist_cnt);
}

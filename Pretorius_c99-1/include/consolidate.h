/* consolidate.h — nightly digestion pass.
 *
 * Takes a batch of recent events and emits:
 *   (a) co-occurrence rules  — pairs of persona keys that appear together
 *   (b) gist summaries       — k-means centroids over LSH signatures
 *
 * Pure compute, no I/O. Caller owns all buffers.
 * 100% integer math (no FPU) — PIII-friendly.
 */
#ifndef PE_CONSOLIDATE_H
#define PE_CONSOLIDATE_H

#include <stdint.h>
#include "lsh_memory.h"

#define PE_EVENT_TEXT_MAX   256
#define PE_EVENT_KEYS_MAX   8
#define PE_CONS_MAX_RULES   512
#define PE_CONS_CLUSTER_K   8
#define PE_CONS_MAX_ITER    10
#define PE_COOC_THRESHOLD   5

typedef struct {
    uint32_t id;
    char     text_content[PE_EVENT_TEXT_MAX];
    uint16_t persona_keys[PE_EVENT_KEYS_MAX];
    int      key_count;
} event_t;

/* Rule operator enum. */
enum {
    RULE_AND = 1,
    RULE_OR  = 2,
    RULE_NOT = 3
};

/* Rule: "if condition keys co-occur, infer action key/value".
 * Both condition keys are stored explicitly (NOT as a bitmask) so any
 * 16-bit key id is supported. action fields are 0 if not yet inferred. */
typedef struct {
    uint16_t cond_key1;
    uint16_t cond_key2;
    uint16_t action_key_id;
    uint16_t action_value_id;
    uint32_t support;        /* co-occurrence count */
    uint8_t  operator;
} rule_t;

/* Full nightly consolidation.
 *   events / event_cnt      input batch (read-only)
 *   out_rules               caller-allocated, at least PE_CONS_MAX_RULES
 *   out_rule_cnt            written
 *   out_gists               caller-allocated, at least PE_CONS_CLUSTER_K
 *   out_gist_cnt            written
 * Returns rule_cnt + gist_cnt. Returns -1 on alloc failure. */
int consolidate_zone(const event_t *events, int event_cnt,
                     rule_t *out_rules, int *out_rule_cnt,
                     lsh_sig_t *out_gists, int *out_gist_cnt);

/* Sub-passes (exposed for testing). */
void generate_rules(const event_t *events, int event_count,
                    rule_t *rules, int *rule_count);

int  build_gist_summaries(const event_t *events, int event_count,
                          lsh_sig_t *summaries, int *summary_count);

#endif /* PE_CONSOLIDATE_H */

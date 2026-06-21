/* long_arc_drift.h -- V6 slow earned baseline drift.
 *
 * This sidecar stores only compact cumulative relationship evidence and a
 * bounded behavioral baseline offset. It does not rewrite identity or
 * cartridge traits. The cartridge sets drift_malleability; the engine only
 * provides the slow mechanism.
 */
#ifndef PE_LONG_ARC_DRIFT_H
#define PE_LONG_ARC_DRIFT_H

#include <stdint.h>
#include "sidecar.h"
#include "relation_dims.h"
#include "dissonance.h"

#define PE_LONG_ARC_DRIFT_MAGIC   PE_SIDECAR_MAGIC('L','A','R','C')
#define PE_LONG_ARC_DRIFT_VERSION 1

typedef struct {
    pe_sidecar_header_t header;
    int16_t  baseline_offset;       /* -1000..1000 applied to baseline temperament */
    int16_t  last_direction;        /* -1 feared, 0 none, +1 ideal */
    uint32_t positive_evidence_days;
    uint32_t negative_evidence_days;
    uint32_t positive_applied_months;
    uint32_t negative_applied_months;
    uint32_t last_update_turn;
    uint32_t _reserved[6];
} pe_long_arc_drift_t;

#ifdef __cplusplus
extern "C" {
#endif

void pe_long_arc_drift_init(pe_long_arc_drift_t *d);
int  pe_long_arc_drift_load(pe_long_arc_drift_t *d, const char *char_dir);
int  pe_long_arc_drift_save(const pe_long_arc_drift_t *d, const char *char_dir);

/* Apply sustained relationship evidence. evidence_days is cumulative
 * relationship time, not gap length. Positive history requires high trust
 * and low resentment; inverse history requires low trust or high threat with
 * resentment. Movement is weekly/monthly scale and bounded by cartridge
 * malleability. */
void pe_long_arc_drift_apply_history(pe_long_arc_drift_t *d,
                                     const pe_dissonance_t *self,
                                     const pe_relation_dims_t *rel,
                                     uint16_t drift_malleability,
                                     uint32_t evidence_days);

#ifdef __cplusplus
}
#endif
#endif

/* relation_dims.h — V6 Phase 4: multi-dimensional Relation.
 *
 * Disposition is too blunt. Humans can love and resent simultaneously,
 * trust and envy, fear and need. V6 makes the relational profile a
 * vector of nine independent dimensions per actor — same input from
 * different actors then means different things, deterministically:
 * praise from a trusted friend soothes, praise from a rival reads as
 * manipulation, praise from a dependent reads as obligation.
 *
 * Per V6_DOCTRINE §13, this is the asymmetry substrate everything
 * downstream needs (impression management, recall modes, repair
 * behavior, contradiction handling).
 *
 * Storage model: parallel to the per-actor relation file. The existing
 * file <char_dir>/relations/<hash>.bin holds the legacy Relation; the
 * new sidecar <char_dir>/relations/<hash>.dims holds the V6 dimensions
 * for the same actor. V5 engines simply do not look for the .dims file;
 * V6 engines load it (or derive defaults from disposition when absent).
 *
 * Update model (Phase 4): the dimensions exist and persist, but the
 * planner-level updates from speech events / appraisal / repair land in
 * Phase 5. For now, defaults derive from Relation.disposition.
 */
#ifndef PE_RELATION_DIMS_H
#define PE_RELATION_DIMS_H

#include <stdint.h>
#include "sidecar.h"

#define PE_RELATION_DIMS_MAGIC   PE_SIDECAR_MAGIC('R','E','L','D')
#define PE_RELATION_DIMS_VERSION 1

/* Nine dimensions, each 0..1000 fixed-point. Not orthogonal — the
 * planner reads combinations: (trust=high, admiration=high) ⇒ praise
 * soothes; (trust=low, threat=high) ⇒ praise triggers suspicion;
 * (dependency=high) ⇒ praise is felt as obligation. */
typedef struct {
    pe_sidecar_header_t header;        /* 32 bytes */
    uint32_t user_hash;                /* must match Relation.user_hash */
    uint16_t trust;                    /* credence in this actor's words */
    uint16_t threat;                   /* perceived danger to identity/safety */
    uint16_t intimacy;                 /* vulnerability shared */
    uint16_t resentment;               /* accumulated unrepaid grievance */
    uint16_t dependency;               /* degree to which I need this actor */
    uint16_t obligation;               /* degree to which I owe this actor */
    uint16_t envy;                     /* desire for what they have/are */
    uint16_t admiration;               /* esteem for their qualities */
    uint16_t embarrassment;            /* shame load they have witnessed */
    uint16_t _reserved[14];            /* zero today; room for future dims */
} pe_relation_dims_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize with sensible defaults derived from a V5 disposition
 * (0..1000). For a brand-new actor (disposition≈500) this yields
 * trust≈500, threat≈500, intimacy=0, admiration≈500, all others=0 —
 * a neutral starting point that the planner can update over time. */
void pe_relation_dims_init_from_disposition(pe_relation_dims_t *dims,
                                            uint32_t user_hash,
                                            uint16_t disposition);

/* Load <char_dir>/relations/<hash>.dims if present and valid. If absent
 * or invalid, initialize from disposition. Returns 0 on success (incl.
 * legitimate missing-file case), negative on path error. */
int pe_relation_dims_load(pe_relation_dims_t *dims,
                          const char *char_dir,
                          uint32_t user_hash,
                          uint16_t disposition);

/* Atomic write to <char_dir>/relations/<hash>.dims. */
int pe_relation_dims_save(const pe_relation_dims_t *dims,
                          const char *char_dir);

#ifdef __cplusplus
}
#endif
#endif

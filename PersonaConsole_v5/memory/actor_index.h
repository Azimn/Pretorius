/* actor_index.h — per-slot actor tagging for episodic memory (Phase 2).
 *
 * Doctrine: who said it must be queryable without storing the name in the
 * summary text. The actor index is a sidecar parallel to MemoryStore's
 * episodic[] array: actor_id[i] is the actor (relation.user_hash) under
 * whom episodic[i] was committed, or 0 for memories that predate
 * actor-tagging or were committed without a known actor.
 *
 * The sidecar lives in <char_dir>/actor_index.bin and is OPTIONAL: a missing
 * file resolves to all-zero (legacy unknown-actor state) so a v5 engine can
 * read a legacy memory.bin and gradually re-tag as new memories commit.
 *
 * The struct is layered on the generic pe_sidecar_header_t so any change to
 * its layout requires a version bump and is validated at load time.
 *
 * Note (cross-tier limitation): AETHER cold-promoted memories do not yet
 * carry actor tags — their aether_event_t layout has no actor field. A
 * promoted memory's tag is whatever the new working-slot owner sets. A
 * future phase can extend the AETHER event format with a sidecar of its
 * own to close this gap.
 */
#ifndef PE_ACTOR_INDEX_H
#define PE_ACTOR_INDEX_H

#include <stdint.h>
#include "sidecar.h"

/* PE_EPISODIC_MAX must be in scope before this header is included
 * (persona.h defines it). */

#define PE_ACTOR_INDEX_MAGIC      PE_SIDECAR_MAGIC('A','C','T','R')
#define PE_ACTOR_INDEX_VERSION    1

typedef struct {
    pe_sidecar_header_t header;
    uint32_t actor_id[PE_EPISODIC_MAX];
} pe_actor_index_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize an in-memory index with a valid header and all-zero tags. */
void pe_actor_index_init(pe_actor_index_t *idx);

/* Load <char_dir>/actor_index.bin if present and valid; otherwise leave
 * the index in its initialized state. Returns 0 on success (including the
 * legitimate missing-file case), negative on path/error. */
int  pe_actor_index_load(pe_actor_index_t *idx, const char *char_dir);

/* Atomically write the index to <char_dir>/actor_index.bin. */
int  pe_actor_index_save(const pe_actor_index_t *idx, const char *char_dir);

/* Tag a slot. If the slot was previously untagged and the new actor is
 * non-zero, header.entry_count increments. Overwriting an existing tag
 * (e.g. when a slot is evicted and reused) does not change the count. */
void pe_actor_index_tag(pe_actor_index_t *idx, uint16_t slot, uint32_t actor_id);

/* Read the tag at slot; returns 0 if out of range or untagged. */
uint32_t pe_actor_index_get(const pe_actor_index_t *idx, uint16_t slot);

/* Clear a slot's tag (used when a memory is removed). Decrements count
 * only if the slot was previously tagged. */
void pe_actor_index_clear(pe_actor_index_t *idx, uint16_t slot);

/* Count currently-tagged slots (cheap; cached in header.entry_count). */
uint32_t pe_actor_index_count(const pe_actor_index_t *idx);

#ifdef __cplusplus
}
#endif
#endif

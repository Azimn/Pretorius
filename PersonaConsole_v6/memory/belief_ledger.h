/* belief_ledger.h -- V7 compact behavioral scars from memory decay.
 *
 * Specific episodic memories may decay or leave the hot store, but repeated
 * emotionally meaningful experience can still compress into a tiny durable
 * pattern. This sidecar is per actor, bounded, and cartridge-neutral.
 */
#ifndef PE_BELIEF_LEDGER_H
#define PE_BELIEF_LEDGER_H

#include <stdint.h>
#include "sidecar.h"

#define PE_BELIEF_LEDGER_MAGIC   PE_SIDECAR_MAGIC('B','L','F','L')
#define PE_BELIEF_LEDGER_VERSION 1

typedef struct MemoryNode MemoryNode;

typedef enum {
    PE_BELIEF_DISRESPECT = 0,
    PE_BELIEF_TRUST_EARNED,
    PE_BELIEF_MANIPULATION,
    PE_BELIEF_ABANDONMENT,
    PE_BELIEF_SHARED_PROJECT,
    PE_BELIEF_SELF_FAILURE,
    PE_BELIEF_THREAT_PATTERN,
    PE_BELIEF_INTIMACY_EARNED,
    PE_BELIEF_COUNT
} pe_belief_slot_t;

typedef struct {
    int16_t  pressure;       /* -1000..1000 */
    uint16_t evidence_count; /* raw event count feeding this pattern */
    uint32_t last_updated;   /* engine turn count */
    uint32_t _reserved;
} pe_belief_record_t;

typedef struct {
    pe_sidecar_header_t header;
    uint32_t            user_hash;
    pe_belief_record_t  slots[PE_BELIEF_COUNT];
    uint8_t             _pad[12];
} pe_belief_ledger_t;

#ifdef __cplusplus
extern "C" {
#endif

void pe_belief_ledger_init(pe_belief_ledger_t *bl, uint32_t user_hash);
int  pe_belief_ledger_load(pe_belief_ledger_t *bl, const char *char_dir,
                           uint32_t user_hash);
int  pe_belief_ledger_save(const pe_belief_ledger_t *bl,
                           const char *char_dir);
void pe_belief_ledger_update(pe_belief_ledger_t *bl,
                             pe_belief_slot_t slot,
                             int delta,
                             uint32_t turn_count);
void pe_belief_ledger_decay(pe_belief_ledger_t *bl,
                            uint32_t turns_elapsed);
void pe_belief_absorb_memory_trace(pe_belief_ledger_t *bl,
                                   const MemoryNode *m,
                                   uint32_t turn_count);
const char *pe_belief_slot_name(uint8_t slot);

#ifdef __cplusplus
}
#endif
#endif

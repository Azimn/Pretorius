/* theory_of_mind.h -- V6 inferred model of the active actor's mind.
 *
 * Relation dims answer "how do I feel about this actor?" ToM answers
 * "what do I believe this actor feels or wants?" The belief can be wrong.
 */
#ifndef PE_THEORY_OF_MIND_H
#define PE_THEORY_OF_MIND_H

#include <stdint.h>
#include "sidecar.h"

#define PE_TOM_MAGIC   PE_SIDECAR_MAGIC('T','O','M','1')
#define PE_TOM_VERSION 1

typedef struct {
    pe_sidecar_header_t header;
    uint32_t user_hash;
    int16_t  believed_valence;
    int16_t  believed_arousal;
    uint16_t believed_goal_topic;
    uint16_t confidence;
    uint16_t stale_turns;
    uint16_t mismatch_count;
    uint16_t _reserved[10];
} pe_tom_t;

#ifdef __cplusplus
extern "C" {
#endif

void pe_tom_init_default(pe_tom_t *tom, uint32_t user_hash);
int  pe_tom_load(pe_tom_t *tom, const char *char_dir, uint32_t user_hash);
int  pe_tom_save(const pe_tom_t *tom, const char *char_dir);
void pe_tom_update_from_input(pe_tom_t *tom, uint8_t input_class,
                              int8_t arousal_pct,
                              int16_t observed_valence,
                              uint16_t topic_id,
                              int8_t trait_amp_suggestibility);
int  pe_tom_detect_mismatch(const pe_tom_t *tom, uint8_t input_class,
                            int16_t observed_valence);

#ifdef __cplusplus
}
#endif
#endif

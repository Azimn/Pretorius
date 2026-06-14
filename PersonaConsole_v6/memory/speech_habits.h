/* speech_habits.h -- V6 Phase 6: lightweight conversation habits.
 *
 * Habits are engine-authored rhythm counters, not memories. They track
 * repeated interaction shape such as long replies or too many turns without
 * a question, then expose small deterministic biases to the planner.
 */
#ifndef PE_SPEECH_HABITS_H
#define PE_SPEECH_HABITS_H

#include <stdint.h>
#include "sidecar.h"

#define PE_SPEECH_HABITS_MAGIC   PE_SIDECAR_MAGIC('H','B','I','T')
#define PE_SPEECH_HABITS_VERSION 2
#define PE_SPEECH_FATIGUE_TERMS  8
#define PE_SPEECH_FATIGUE_LEN    16

typedef struct {
    pe_sidecar_header_t header;
    uint32_t turns_observed;
    uint32_t questions_asked;
    uint16_t avg_reply_words_q8;
    uint16_t long_reply_streak;
    uint16_t short_reply_streak;
    uint16_t no_question_streak;
    uint16_t question_bias;
    uint16_t brevity_bias;
    uint16_t initiative_bias;
    uint16_t fatigue_bias;
    uint8_t  fatigue_count;
    uint8_t  _pad0;
    char     fatigue_terms[PE_SPEECH_FATIGUE_TERMS][PE_SPEECH_FATIGUE_LEN];
    uint8_t  fatigue_hits[PE_SPEECH_FATIGUE_TERMS];
    uint8_t  _pad1[8];
    uint16_t _reserved[4];
} pe_speech_habits_t;

#ifdef __cplusplus
extern "C" {
#endif

void pe_speech_habits_init(pe_speech_habits_t *h);
int  pe_speech_habits_load(pe_speech_habits_t *h, const char *char_dir);
int  pe_speech_habits_save(const pe_speech_habits_t *h, const char *char_dir);

void pe_speech_habits_update(pe_speech_habits_t *h,
                             const char *reply,
                             int reply_had_question,
                             int input_class);

void pe_speech_habits_note_input(pe_speech_habits_t *h, const char *input);

#ifdef __cplusplus
}
#endif
#endif

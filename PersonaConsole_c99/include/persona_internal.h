/* persona_internal.h — cross-module helpers, not public API. */
#ifndef PERSONA_INTERNAL_H
#define PERSONA_INTERNAL_H

#include "persona.h"
#include <stdio.h>

/* ---- file I/O (little-endian raw writes; x86 native) ---- */
int pe_read_file (const char *path, void *buf, size_t n);
int pe_write_file_atomic(const char *path, const void *buf, size_t n);
int pe_mkdir_p(const char *path);
int pe_path_join(char *out, size_t n, const char *a, const char *b);

/* ---- engine internals ---- */
void pe_decay_drives(Engine *eng, uint32_t delta_ms);
void pe_apply_today(Engine *eng);
void pe_compute_mood(Engine *eng);

/* emotion / pattern matching */
void pe_classify_input(Engine *eng, const char *input,
                       EmotionVector *out_ev);
void pe_update_drives_from_input(Engine *eng);
void pe_update_topic_momentum(Engine *eng);

/* memory */
void pe_associative_recall(Engine *eng, const EmotionVector *ev);
uint8_t pe_compute_salience(Engine *eng,
                            const EmotionVector *prev,
                            const EmotionVector *cur,
                            int32_t drive_impact_abs,
                            int identity_threat,
                            int repeat_count,
                            int relationship_impact);
void pe_commit_memory(Engine *eng, const char *summary,
                      const EmotionVector *ev, uint16_t topic_id,
                      uint8_t salience, int identity_threat);
void pe_decay_episodic(Engine *eng);
void pe_push_short_term(Engine *eng, uint8_t type, const char *text);

/* goal + intent */
uint16_t pe_select_goal(Engine *eng);
uint16_t pe_select_intent(Engine *eng, uint16_t goal_idx,
                          const EmotionVector *ev);

/* dialogue */
int pe_generate_response(Engine *eng, const char *input,
                         char *out, size_t n);
void pe_repetition_decay(Engine *eng);

/* relations */
int  pe_load_relation(Engine *eng, const char *user_id);
int  pe_save_relation(Engine *eng);

/* today */
void pe_pick_today(Engine *eng);

/* ---- v2: rhetorical planner ---- */
void pe_build_plan(Engine *eng);

/* ---- v2: embodiment / layered affect ---- */
void pe_update_embodiment(Engine *eng, uint32_t delta_ms);
void pe_update_layered_affect(Engine *eng);

/* ---- v2: trace ring buffer ---- */
void pe_trace_push(Engine *eng);

/* ---- v2: per-turn input prep (cached lowered + char bitmap + negation) ---- */
void pe_prep_input(Engine *eng, const char *input);

/* ---- v3.1: autobiographical chapters + dream recall ----
 *
 * pe_crystallize_chapters: allocation-free temporal bucketing of episodic
 *   memories into up to PE_CHAPTER_MAX chapters.  Writes eng->chapters.
 *   May be called at any point (not just persona_open); idempotent.
 *
 * pe_check_dream: called in persona_open with the gap in ms since last
 *   save.  If gap >= 8 h, crystallises and sets dream_pending=1 plus a
 *   dream_phrase in eng->chapters.  The dream is surfaced by
 *   persona_process_input on the very first turn of the new session.
 */
void pe_crystallize_chapters(Engine *eng);
void pe_check_dream(Engine *eng, uint32_t gap_ms);

/* ---- v3.0: Theory of Mind / predictive coding ----
 *
 * pe_update_user_model: updates the UserModel embedded in eng->relation
 *   from the current input.  Called after pe_classify_input.
 *
 * pe_predict_next_input: at end of turn, predicts what the next input
 *   will look like (class + valence) based on Pretorius's last
 *   rhetorical_mode and the current UserModel.  Writes into NPCState.
 *
 * pe_compute_surprise: at start of next turn, compares actual input vs
 *   prediction; writes surprise_last and updates prediction_error_accum.
 *   High surprise jolts acute_spike.
 */
void pe_update_user_model(Engine *eng, const EmotionVector *ev);
void pe_predict_next_input(Engine *eng);
void pe_compute_surprise(Engine *eng, const EmotionVector *ev);

/* ---- v3.2: The Voice ----
 *
 * pe_voice_rerank: counterfactual 1-ply lookahead applied to the freshly
 *   built candidate list.  For each candidate template, predicts the
 *   speaker's likely response if that utterance were chosen, scores
 *   how well the prediction advances the current goal, and adds the
 *   alignment delta to candidate_scores[i].  Pure read of state +
 *   relation + goals + templates; mutates only candidate_scores[] and
 *   two instrumentation fields on NPCState.
 */
void pe_voice_rerank(Engine *eng);

/* small bitmap ops over a 256-bit field (32 bytes) */
static inline void pe_bm_set(uint8_t *bm, unsigned char c){ bm[c>>3] |= (uint8_t)(1u<<(c&7)); }
static inline int  pe_bm_get(const uint8_t *bm, unsigned char c){ return (bm[c>>3] >> (c&7)) & 1; }

/* v3.2: resolve a node index returned by pe_associative_recall.
 *   - idx <  PE_EPISODIC_MAX  → real working-memory node
 *   - idx >= PE_EPISODIC_MAX  → cold scratch (idx - PE_EPISODIC_MAX)
 * Returns NULL if the index points to an unallocated slot. */
static inline const MemoryNode *pe_active_node(const Engine *eng, uint16_t idx){
    if (idx >= PE_EPISODIC_MAX){
        uint16_t cidx = (uint16_t)(idx - PE_EPISODIC_MAX);
        if (cidx < eng->cold_scratch_count) return &eng->cold_scratch[cidx];
        return NULL;
    }
    if (idx < eng->memory.episodic_count) return &eng->memory.episodic[idx];
    return NULL;
}

/* small string utils */
void pe_strlower(char *s);
int  pe_strieq(const char *a, const char *b);
const char *pe_find_lower(const char *hay, const char *needle_lower);

/* output buffer helpers */
size_t pe_append(char *dst, size_t cap, size_t pos, const char *s);

#endif

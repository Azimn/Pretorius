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

/* small bitmap ops over a 256-bit field (32 bytes) */
static inline void pe_bm_set(uint8_t *bm, unsigned char c){ bm[c>>3] |= (uint8_t)(1u<<(c&7)); }
static inline int  pe_bm_get(const uint8_t *bm, unsigned char c){ return (bm[c>>3] >> (c&7)) & 1; }

/* small string utils */
void pe_strlower(char *s);
int  pe_strieq(const char *a, const char *b);
const char *pe_find_lower(const char *hay, const char *needle_lower);

/* output buffer helpers */
size_t pe_append(char *dst, size_t cap, size_t pos, const char *s);

#endif

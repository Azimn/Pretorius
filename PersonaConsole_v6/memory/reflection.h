/* reflection.h — V5 reflective memory consolidation.
 *
 * Adapted from Park et al., "Generative Agents: Interactive Simulacra of
 * Human Behavior" (UIST 2023), §3.2 (Reflection) and §3.3 (Retrieval).
 *
 * Their reflections are LLM-generated.  Ours are deterministic:
 *   importance scoring  ::  salience × 0.40 + |valence| × 0.25
 *                         + arousal × 0.20 + recency × 0.15
 *   clustering          ::  top-K candidates, SimHash Hamming ≤ 16
 *   synthesis           ::  bitwise-majority signature, template-pool
 *                          summary, salience boosted 1.3×
 *   retrieval           ::  reflection.salience + topic_match + (64-hd)
 *
 * Zero new dependencies.  Reuses LSH primitives we already ship for
 * AETHER consolidation.  Per-turn cost amortized under 1 µs; peak
 * (on consolidation turn) ~50 µs.  Disk: ~2 KB per character.
 *
 * The actual ReflectionState layout lives in core/persona.h as an
 * embedded anonymous struct on Engine.reflections — persona.h is the
 * single authority for Engine memory layout.  This header only
 * exposes the function-level API.
 */
#ifndef PERSONA_REFLECTION_H
#define PERSONA_REFLECTION_H

#include <stdint.h>
#include <stddef.h>
#include "persona.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PE_REFLECTION_MAX               16
#define PE_REFLECTION_CLUSTER_MIN       3
#define PE_REFLECTION_HAMMING_MAX       16
#define PE_REFLECTION_CONSOLIDATE_EVERY 8

/* Zero the reflection ring on an engine.  Idempotent. */
void pe_reflection_init(Engine *eng);

/* Compute an importance score for a MemoryNode (0..255).  Pure. */
int pe_memory_importance(const MemoryNode *m, uint32_t current_turn);

/* Run the consolidation pass.  Has an internal cooldown — safe to
 * call every turn.  Returns the count of NEW reflections synthesized
 * this call (0 if cooldown not elapsed or no eligible clusters). */
int pe_consolidate_reflections(Engine *eng);

/* Retrieve up to `max_out` reflections relevant to a query signature
 * and topic id.  Sorted by relevance.  Returns count written. */
int pe_query_reflections(const Engine *eng,
                         uint64_t query_sig, uint16_t query_topic,
                         MemoryNode *out, int max_out);

/* Persistence — reflections.bin alongside chapters.bin in char_dir. */
int pe_reflection_save(const Engine *eng, const char *char_dir);
int pe_reflection_load(Engine *eng, const char *char_dir);

/* Render a reflection's surface text by substituting {topic} into the
 * template pool.  Writes into `out`. */
int pe_reflection_render(const Engine *eng,
                         const MemoryNode *reflection,
                         char *out, int out_cap);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_REFLECTION_H */

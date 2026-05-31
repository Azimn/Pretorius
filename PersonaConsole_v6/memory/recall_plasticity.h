/* recall_plasticity.h — V5 Recall-Coupled Plasticity (RCP).
 *
 * A synthesis of four threads in 2024-2026 memory neuroscience that all
 * point at the same paradigm shift: **retrieval is a write event, not a
 * read**.  Every time a memory is recalled, it is reconstructed through
 * the current schema (Bartlett 1932; Gilboa & Marlatte recent reviews),
 * briefly destabilised so current affect can rewrite it (Nader & Hardt
 * 2009; Schiller/Phelps lab updates), strengthened against future decay
 * (Roediger & Karpicke testing effect), and its temporal neighbours are
 * linked tighter through offline replay (Buzsáki sharp-wave ripples).
 *
 * Park et al. (UIST 2023) gave us *reflection* — the offline abstraction
 * step.  RCP gives us *online* memory plasticity — what happens to the
 * raw episodic substrate every time we touch it.  The two compose:
 * RCP shapes what reflection eventually sees.
 *
 * Implementation budget: one function, ~70 lines.  No new state; reads
 * eng->active_memories[] populated by pe_associative_recall and writes
 * back into eng->memory.episodic[].  Costs ≤ 4 µs/turn at active_count
 * = PE_ACTIVE_MAX.  Deterministic.  Skips core memories so foundational
 * autobiography is immovable.
 *
 * Mechanism per recalled memory m:
 *   (1) Reconsolidation:   m.emotion blends 1/20 toward current event.
 *   (2) Schema congruence: if m's valence sign agrees with the dominant
 *                          live schema, +1 salience (max 250).
 *   (3) Replay coupling:   for adjacent pairs (m_i, m_{i-1}) in the
 *                          active set, blend one differing LSH bit so
 *                          their signatures inch toward one another.
 *
 * The testing effect (Roediger 2006; replicated through 2026) is already
 * in pe_associative_recall, which resets decay_counter on access — RCP
 * does not duplicate it, only completes the loop.
 */
#ifndef PERSONA_RECALL_PLASTICITY_H
#define PERSONA_RECALL_PLASTICITY_H

#include "persona.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Apply Recall-Coupled Plasticity to memories surfaced this turn.
 * `current_event` is the event vector that produced this turn's recall.
 * Idempotent for active_count == 0.  Safe to call every turn. */
void pe_recall_plasticity_tick(Engine *eng, const EmotionVector *current_event);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_RECALL_PLASTICITY_H */

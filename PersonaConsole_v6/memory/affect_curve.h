/* affect_curve.h — V4 nonlinear emotional persistence.
 *
 * Replaces v3's linear EMA decay for mood, drives, and schema slots.
 * Required features per V4 spec:
 *
 *   1. Salience-weighted decay
 *      High-salience memories / strong schemas decay slower.
 *      decay_rate = base_rate × (1 - salience/1000)^2
 *
 *   2. Emotional hysteresis
 *      State resists rapid reversal.  An angry character does not
 *      become calm in one turn no matter how polite the user is.
 *      reversal_resistance scales with current magnitude.
 *
 *   3. Habituation
 *      Repeated identical stimuli weaken response.
 *      effective_magnitude = magnitude × (0.5 ^ (consecutive_hits-1))
 *
 *   4. Trait-amplified persistence
 *      Big-Five-driven amplifiers per slot:
 *        high N → SCHEMA_USER_HOSTILE decays slower
 *        high A → SCHEMA_USER_TRUSTWORTHY decays slower
 *        low O → SCHEMA_USER_DECEPTIVE forms faster
 *      etc.
 *
 * All math is saturating fixed-point in the i16 −1000..+1000 domain.
 * No floats, no allocations, no branches beyond clamps and signs.
 * Deterministic.
 */
#ifndef PERSONA_V4_AFFECT_CURVE_H
#define PERSONA_V4_AFFECT_CURVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Big-Five trait vector, scaled 0..100 per persona.h Identity. */
typedef struct {
    int8_t O, C, E, A, N;
} TraitVec;

/* ----- Salience-weighted decay -----
 * Given current value (−1000..+1000), salience (0..1000), and one tick
 * elapsed, return the decayed value.  Returns the same value if the
 * input is already at zero. */
int16_t affect_decay(int16_t current, int16_t salience, int base_rate_per_mille);

/* ----- Hysteresis -----
 * Apply a delta to a hysteretic accumulator.  Reversing direction
 * (signs of current and delta differ) attenuates the delta.  Same-sign
 * deltas pass through with mild damping at extremes. */
int16_t affect_hysteresis_apply(int16_t current, int delta);

/* ----- Habituation -----
 * Given a raw event magnitude (0..255) and the count of consecutive
 * prior occurrences of the same event, return the attenuated effective
 * magnitude.  Each consecutive hit halves the magnitude (floored at 1).
 * Resets to full magnitude after gap_turns > 0 since the last event. */
int affect_habituate(int raw_magnitude, int consecutive_hits, int gap_turns);

/* ----- Trait amplifier -----
 * Returns a multiplier in 0..2000 (1000 = 1.0×) that scales decay
 * rate / event magnitude for a given schema slot, given the character's
 * Big Five.  Pure function of (slot, traits).  Used by schema_state and
 * by drive/mood decay paths. */
int affect_trait_amplifier(int schema_slot, const TraitVec *traits);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_V4_AFFECT_CURVE_H */

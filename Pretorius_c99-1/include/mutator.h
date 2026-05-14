/* mutator.h — procedural template expansion engine.
 *
 * Templates may contain splice markers like [adj_morbid] or [verb_create].
 * The mutator replaces each marker with one fragment chosen from a
 * compile-time synonym bank, biased by plan.theatricality / plan.aggression.
 *
 * Deterministic: caller supplies an xorshift32 RNG state, advanced per
 * substitution. Same seed → same expansion.
 *
 * Banks are static const data (~2 KB). No allocation, no I/O, no state.
 */
#ifndef PE_MUTATOR_H
#define PE_MUTATOR_H

#include <stdint.h>
#include <stddef.h>

/* Expand [bank_name] markers in `tmpl` into `out` (NUL-terminated).
 * Returns number of bytes written (excluding NUL), or 0 on overflow.
 *
 *   tmpl            input template, may contain [bank_name] markers
 *   out, out_cap    output buffer
 *   theatricality   0..255, gates dramatic/grand banks
 *   aggression      0..255, gates morbid/violent banks
 *   rng_state       xorshift32 state, advanced once per substitution
 *
 * Markers that don't correspond to a known bank are passed through verbatim.
 */
size_t mutator_expand(const char *tmpl, char *out, size_t out_cap,
                      uint8_t theatricality, uint8_t aggression,
                      uint32_t *rng_state);

/* Introspection: how many banks are registered (for tests). */
int mutator_bank_count(void);

/* Introspection: bank metadata by index. NULL on out-of-range. */
const char *mutator_bank_name(int idx);
int         mutator_bank_size(int idx);

#endif /* PE_MUTATOR_H */

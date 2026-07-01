/* turn_drama.h -- V7 per-turn scene demand synthesis.
 *
 * TurnDrama compresses scattered continuity signals into one compact
 * renderer-readable demand. It is computed each turn, never persisted,
 * and never authored by a renderer.
 */
#ifndef PE_TURN_DRAMA_H
#define PE_TURN_DRAMA_H

#include "persona.h"

typedef struct V6UserTurnInterpretation V6UserTurnInterpretation;

#ifdef __cplusplus
extern "C" {
#endif

void pe_synthesize_turn_drama(Engine *eng,
                              const V6UserTurnInterpretation *it,
                              TurnDrama *td);

#ifdef __cplusplus
}
#endif
#endif

/* prompt_compiler.h — V4 deterministic-state → semantic-constraints.
 *
 * Transforms Layer 1 state into structured rendering constraints for
 * Layer 2 backends.  This is NOT a lore dump.  This is NOT roleplay
 * prompting.  This is projecting STATE TOPOLOGY into a small, dense
 * block the renderer can act on.
 *
 * BAD (what we never produce):
 *   "You are Maker, a sarcastic blacksmith who hates the user…"
 *
 * GOOD (what we produce):
 *   CURRENT_AFFECT:
 *     irritation:  820
 *     exhaustion:  410
 *   RELATIONAL_STANCE:
 *     distrustful
 *     emotionally guarded
 *   ACTIVE_MEMORY_HOOKS:
 *     user insulted craftsmanship
 *     unresolved dispute regarding sword repair
 *   VOICE_MASK:
 *     terse
 *     metaphorical
 *     sardonic
 *   INTENT:
 *     discourage further criticism
 *     preserve dignity
 *
 * Identical RenderContext → byte-identical compiled prompt.  This is
 * what enables behavioral holography: hand the same compiled prompt to
 * templates, a 1B SLM, an 8B local model, or a cloud API, and the
 * character's STANCE comes through identically.  Only fluency varies.
 *
 * Token budget is enforced.  The compiler will trim the lowest-salience
 * lines first if budget is tight.
 */
#ifndef PERSONA_V4_PROMPT_COMPILER_H
#define PERSONA_V4_PROMPT_COMPILER_H

#include <stddef.h>
#include "render_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PE_PROMPT_MAX_BYTES 4096

typedef enum {
    PE_SLM_PROFILE_BALANCED   = 0,
    PE_SLM_PROFILE_TINY       = 1,
    PE_SLM_PROFILE_EXPRESSIVE = 2,
} SlmRenderProfile;

typedef struct {
    int     max_bytes;          /* hard cap on emitted prompt */
    int     include_memory_hooks; /* default 1 */
    int     include_schema;       /* default 1 */
    int     include_voice_mask;   /* default 1 */
    int     include_intent;       /* default 1 */
    int     include_current_input; /* default 1 — the user's last utterance */
    int     render_profile;       /* SlmRenderProfile, default balanced */
} PromptCompilerConfig;

void prompt_compiler_default_config(PromptCompilerConfig *out);

/* Compile a RenderContext into a structured constraint block.  Returns
 * the number of bytes written (excluding terminator), or negative on
 * error.  Output is NUL-terminated. */
int prompt_compile(const RenderContext        *ctx,
                   const PromptCompilerConfig *cfg,
                   char                       *out_buf,
                   int                         out_cap);

/* Same, but also write the LAST user utterance into the block as
 * USER_INPUT: ... for the renderer to respond to.  Used by SLM
 * backends.  Templates ignore the user-input line. */
int prompt_compile_with_input(const RenderContext        *ctx,
                              const PromptCompilerConfig *cfg,
                              const char                 *user_input,
                              char                       *out_buf,
                              int                         out_cap);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_V4_PROMPT_COMPILER_H */

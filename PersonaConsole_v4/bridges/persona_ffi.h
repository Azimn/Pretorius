/* persona_ffi.h — opaque-handle public ABI for PersonaHost.
 *
 * The same surface backs three transports:
 *   - the local HTTP server (host/persona_host, default :7777)
 *   - the line-delimited JSON-over-stdio mode (same binary, --stdio flag)
 *   - direct FFI from Unity / Unreal / Godot (link libpersona_host.{so,dll,dylib})
 *
 * Memory ownership: caller-allocated output buffers throughout.  No
 * cross-boundary frees.  All strings are null-terminated UTF-8.
 *
 * This header intentionally does NOT include persona.h — it stays
 * dependency-free so external callers see a flat C ABI.  The Engine
 * struct lives inside the opaque PersonaSession on our side.
 */
#ifndef PERSONA_FFI_H
#define PERSONA_FFI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PersonaSession PersonaSession;

/* Open a cartridge — either a .cart file or a character directory.
 * Returns NULL on error (file missing, schema mismatch, etc.). */
PersonaSession* ps_open(const char *cartridge_path);

/* Close session, persisting state and freeing all resources. */
void ps_close(PersonaSession *s);

/* Bind the active interlocutor identifier (per-user context).
 * Returns 0 on success. */
int ps_set_user(PersonaSession *s, const char *user_id);

/* Process a single turn of input.  Writes a NUL-terminated reply into
 * out_buf (must be at least 1 byte).  Returns the response length in
 * bytes (excluding NUL), or a negative error code. */
int ps_reply(PersonaSession *s,
             const char *input,
             char *out_buf, int out_buf_size);

/* Optional character-initiated conversational probe.  This is read-only:
 * it does not process input, advance turn_count, or persist memory.  Returns
 * 0 when no idle probe should be emitted yet. */
int ps_idle_probe(PersonaSession *s, char *out_buf, int out_buf_size);

/* Persist current cognitive state to disk. */
int ps_save(PersonaSession *s);

/* Hot-swap cartridge without losing the active user binding.
 * Saves current state, closes, reopens at new path. */
int ps_load(PersonaSession *s, const char *new_cartridge_path);

/* Inspect: write a compact JSON snapshot describing the session into
 * out_buf.  Includes name, mood, intent, today label, voice instrumentation,
 * AETHER stats.  Returns bytes written (excluding NUL), or negative on error. */
int ps_state(PersonaSession *s, char *out_buf, int out_buf_size);

/* Inspect: write the character name into out_buf.  Returns bytes written. */
int ps_name(PersonaSession *s, char *out_buf, int out_buf_size);

/* Return the character's runtime directory (where state.bin / portrait /
 * etc. live).  For .cart loads, this is the directory containing the
 * cartridge file.  Used by the host's static-asset routes. */
int ps_char_dir(PersonaSession *s, char *out_buf, int out_buf_size);

#ifdef __cplusplus
}
#endif
#endif

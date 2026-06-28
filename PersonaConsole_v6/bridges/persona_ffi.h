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

/* Close session without persisting unsaved changes. */
void ps_close_without_save(PersonaSession *s);

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

/* Reload the current cartridge from disk without saving the in-memory state. */
int ps_discard_unsaved(PersonaSession *s);
int ps_reset_runtime(PersonaSession *s);

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

/* V5: inspect the reflective memory ring.  Writes a JSON object:
 * {"count":N,"reflections":[{"topic":T,"sources":K,"salience":S,"text":"..."},...]}.
 * Returns bytes written (excluding NUL), or negative on error. */
int ps_reflections(PersonaSession *s, char *out_buf, int out_buf_size);

/* V5: inspect known interlocutor relationship files for multi-character /
 * multi-user worlds. */
int ps_relationships(PersonaSession *s, char *out_buf, int out_buf_size);

/* Canonical import surface. These functions write through the C runtime's
 * normal memory / learned-knowledge / relation / open-loop paths rather than
 * editing binary sidecars directly. */
int ps_import_memory(PersonaSession *s,
                     const char *summary,
                     const char *topic_key,
                     const char *actor_name,
                     int salience,
                     int emotional_impact,
                     int is_core,
                     int is_pinned,
                     unsigned *memory_id_out);
int ps_import_relationship(PersonaSession *s,
                           const char *actor_name,
                           unsigned trust,
                           unsigned threat,
                           unsigned intimacy,
                           unsigned resentment,
                           unsigned dependency,
                           unsigned obligation,
                           unsigned envy,
                           unsigned admiration,
                           unsigned embarrassment);
int ps_import_open_loop(PersonaSession *s,
                        const char *actor_name,
                        const char *topic_key,
                        const char *desired_speech_act,
                        unsigned urgency,
                        unsigned shame_cost,
                        unsigned avoidance_pressure,
                        unsigned *loop_id_out);
int ps_import_learned_knowledge(PersonaSession *s,
                                const char *topic_key,
                                const char *claim_text,
                                unsigned scope,
                                unsigned source_type,
                                unsigned source_tier,
                                unsigned status,
                                unsigned authority_rank,
                                unsigned confidence,
                                const char *source_actor_name,
                                unsigned correction_of_record_id,
                                unsigned evidence_ref,
                                unsigned domain_tag,
                                unsigned *record_id_out);
int ps_import_learned_edge(PersonaSession *s,
                           unsigned source_record_id,
                           unsigned relation_type,
                           unsigned target_record_id,
                           unsigned weight,
                           unsigned confidence,
                           unsigned *edge_id_out);

#ifdef __cplusplus
}
#endif
#endif

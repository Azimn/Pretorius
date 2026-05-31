/* memory_firewall.h — V4 contamination barrier.
 *
 * Rule:  NO generated text may directly become memory.  EVER.
 *
 * Renderers (Layer 2) produce ephemeral output.  Memory writes
 * (Layer 1) flow ONLY through symbolic event interpretation of
 * USER INPUT, not character output.  This is the single most
 * important architectural protection in V4.
 *
 * If a future cloud LLM hallucinates that the user said something
 * they didn't, that hallucination must never re-enter memory as
 * truth.  The firewall makes this structurally impossible: the
 * memory-write API takes only Layer 1-side parameters, and the
 * Layer 2 contract gives renderers a CONST RenderContext.
 *
 * This header is the public contract.  Enforcement primitives
 * (debug-build asserts that the renderer's call stack didn't reach
 * the write API) live in memory_firewall.c.
 */
#ifndef PERSONA_V4_MEMORY_FIREWALL_H
#define PERSONA_V4_MEMORY_FIREWALL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reason codes for an attempted firewall breach.  Used by the
 * instrumentation/state_trace layer when reporting. */
typedef enum {
    PE_FW_OK                     = 0,
    PE_FW_DENY_RENDERER_WRITE    = 1,   /* renderer tried to write Layer 1 */
    PE_FW_DENY_TEXT_AS_EVENT     = 2,   /* tried to log generated text as event */
    PE_FW_DENY_UNATTRIBUTED      = 3,   /* event lacked source attribution */
} FirewallVerdict;

/* Source attribution.  Every memory-write call must declare where the
 * data originated.  USER_INPUT is the only source that triggers
 * episodic logging + schema updates; RENDERER_OUTPUT is REJECTED. */
typedef enum {
    PE_SRC_USER_INPUT       = 0,   /* parsed user utterance */
    PE_SRC_SYSTEM_TICK      = 1,   /* clock-driven drive decay etc. */
    PE_SRC_CARTRIDGE_LOAD   = 2,   /* identity seed at session start */
    PE_SRC_RENDERER_OUTPUT  = 3,   /* always denied */
} MemorySource;

/* Open question every Layer 1 mutation must answer.  The firewall's
 * job is to inspect this answer and reject anything sourced from a
 * renderer.  Layer 1 callers populate this; renderers cannot since
 * they only ever receive a CONST RenderContext. */
typedef struct {
    MemorySource source;
    uint32_t     turn_count;
    int          relation_idx;
    const char  *event_label;  /* for trace logging only */
} MemoryWriteTicket;

/* Inspect a ticket.  Returns PE_FW_OK or a deny code.  Layer 1 code
 * calls this before persisting state; if the verdict is non-OK the
 * caller MUST drop the write and emit an instrumentation event. */
FirewallVerdict memory_firewall_check(const MemoryWriteTicket *ticket);

/* Convenience helper: assert source == PE_SRC_USER_INPUT for an
 * episodic-memory write.  Used by pe_commit_memory etc. */
FirewallVerdict memory_firewall_check_episodic(const MemoryWriteTicket *ticket);

/* Diagnostic counters — read by instrumentation/state_trace. */
typedef struct {
    uint64_t allowed;
    uint64_t denied_renderer;
    uint64_t denied_text_event;
    uint64_t denied_unattributed;
} FirewallStats;

void memory_firewall_stats(FirewallStats *out);
void memory_firewall_reset_stats(void);

/* Helpers for human-readable trace output. */
const char *memory_firewall_verdict_str(FirewallVerdict v);
const char *memory_source_str(MemorySource s);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_V4_MEMORY_FIREWALL_H */

/* state_trace.h — V4 instrumentation foundation.
 *
 * Single-file ring of (turn, event_type, payload) records.  Every
 * meaningful state change in Layer 1 can optionally emit a trace
 * record.  The ring is bounded (no heap growth), fixed-size, and
 * deterministic — replays produce identical trace output for replay
 * debuggers to diff against.
 *
 * Emission is gated by PE_TRACE_ENABLE env var (or programmatic
 * trace_set_enabled).  Off by default — zero cost in production.
 *
 * Subsystems covered:
 *   - memory provenance (every commit to working memory + AETHER)
 *   - schema evolution (every slot mutation with cause)
 *   - emotional decay (mood / drive ticks)
 *   - renderer dispatch (which backend, latency, fallback events)
 *   - firewall verdicts (allowed / denied counts + last-deny detail)
 *
 * Output formats:
 *   - in-memory ring (always)
 *   - line-oriented to stderr (PE_TRACE_FD=stderr)
 *   - line-oriented to a file (PE_TRACE_FILE=/path/to/trace.log)
 *   - JSON-line emission (PE_TRACE_JSON=1)
 */
#ifndef PERSONA_V4_STATE_TRACE_H
#define PERSONA_V4_STATE_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_TRACE_RING_SIZE     512
#define PE_TRACE_LABEL_LEN     32
#define PE_TRACE_DETAIL_LEN    96

typedef enum {
    PE_TRACE_NONE              = 0,
    PE_TRACE_MEMORY_COMMIT     = 1,   /* working memory write */
    PE_TRACE_AETHER_PUT        = 2,   /* AETHER WAL append */
    PE_TRACE_AETHER_COLD_HIT   = 3,   /* AETHER cold-fallback recall */
    PE_TRACE_SCHEMA_EVENT      = 4,   /* schema_apply_event */
    PE_TRACE_SCHEMA_DECAY      = 5,   /* schema_tick */
    PE_TRACE_MOOD_UPDATE       = 6,   /* pe_compute_mood */
    PE_TRACE_DRIVE_DECAY       = 7,   /* pe_decay_drives */
    PE_TRACE_RENDER_DISPATCH   = 8,   /* backend->render() call */
    PE_TRACE_RENDER_FALLBACK   = 9,   /* SLM unavailable → template */
    PE_TRACE_FIREWALL_DENY     = 10,  /* memory_firewall denied a write */
    PE_TRACE_INTENT_SELECT     = 11,  /* pe_select_intent */
    PE_TRACE_PLAN_BUILD        = 12,  /* pe_build_plan */
} TraceEventType;

typedef struct {
    uint32_t        turn;
    TraceEventType  type;
    int32_t         value;                  /* primary numeric payload */
    int32_t         delta;                  /* secondary numeric payload */
    char            label[PE_TRACE_LABEL_LEN];
    char            detail[PE_TRACE_DETAIL_LEN];
} TraceRecord;

/* Master enable.  Default off.  Reads PE_TRACE_ENABLE at first call. */
int  trace_is_enabled(void);
void trace_set_enabled(int on);

/* Emit a record.  No-op when disabled.  Always deterministic
 * (same inputs → same emitted record). */
void trace_emit(uint32_t turn, TraceEventType type,
                int32_t value, int32_t delta,
                const char *label, const char *detail);

/* Convenience wrappers for the common cases. */
void trace_memory_commit(uint32_t turn, int salience, const char *summary);
void trace_schema_event(uint32_t turn, int slot, int delta, const char *evt_name);
void trace_render_dispatch(uint32_t turn, const char *backend,
                           int latency_ms, int output_len, uint32_t flags);
void trace_firewall_deny(uint32_t turn, int verdict, int source, const char *label);

/* Read accessors for replay debuggers + tests. */
int             trace_count(void);
const TraceRecord *trace_at(int idx);   /* idx 0..count-1; idx 0 = oldest */
void            trace_clear(void);

/* Dump the ring to stderr (or PE_TRACE_FD) in a human-readable form.
 * Returns bytes written. */
int trace_dump_text(void);

/* JSON-line dump.  Returns bytes written.  One line per record.
 * Caller can pipe through `jq` or feed into a timeline visualizer. */
int trace_dump_json(int fd);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_V4_STATE_TRACE_H */

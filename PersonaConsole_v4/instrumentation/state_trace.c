/* state_trace.c — V4 instrumentation foundation (impl).
 *
 * Fixed-size ring buffer, no heap, deterministic.  Off by default —
 * trace_emit short-circuits when disabled, so call sites pay only a
 * function call + load.
 */
#include "state_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static TraceRecord g_ring[PE_TRACE_RING_SIZE];
static int g_head     = 0;      /* next write index */
static int g_count    = 0;      /* records written, capped at ring size */
static int g_enabled  = -1;     /* -1 = uninitialized, 0/1 = explicit */
static int g_text_fd  = -1;     /* extra text mirror; -1 = none */
static int g_json_fd  = -1;     /* extra JSON-line mirror; -1 = none */

static void resolve_config_once(void){
    if (g_enabled >= 0) return;
    const char *en = getenv("PE_TRACE_ENABLE");
    g_enabled = (en && (*en == '1' || *en == 't' || *en == 'T' || *en == 'y' || *en == 'Y')) ? 1 : 0;
    const char *file = getenv("PE_TRACE_FILE");
    if (file && *file){
        FILE *f = fopen(file, "a");
        if (f) g_text_fd = fileno(f);
    }
    const char *json = getenv("PE_TRACE_JSON");
    if (json && *json == '1') g_json_fd = 2;   /* stderr */
}

int trace_is_enabled(void){
    resolve_config_once();
    return g_enabled;
}

void trace_set_enabled(int on){
    resolve_config_once();
    g_enabled = on ? 1 : 0;
}

static const char *type_str(TraceEventType t){
    switch (t){
    case PE_TRACE_MEMORY_COMMIT:    return "memory_commit";
    case PE_TRACE_AETHER_PUT:       return "aether_put";
    case PE_TRACE_AETHER_COLD_HIT:  return "aether_cold_hit";
    case PE_TRACE_SCHEMA_EVENT:     return "schema_event";
    case PE_TRACE_SCHEMA_DECAY:     return "schema_decay";
    case PE_TRACE_MOOD_UPDATE:      return "mood_update";
    case PE_TRACE_DRIVE_DECAY:      return "drive_decay";
    case PE_TRACE_RENDER_DISPATCH:  return "render_dispatch";
    case PE_TRACE_RENDER_FALLBACK:  return "render_fallback";
    case PE_TRACE_FIREWALL_DENY:    return "firewall_deny";
    case PE_TRACE_INTENT_SELECT:    return "intent_select";
    case PE_TRACE_PLAN_BUILD:       return "plan_build";
    default:                         return "unknown";
    }
}

static void copy_field(char *dst, int cap, const char *src){
    if (!src){ dst[0] = 0; return; }
    int i = 0;
    while (src[i] && i < cap - 1){ dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

void trace_emit(uint32_t turn, TraceEventType type,
                int32_t value, int32_t delta,
                const char *label, const char *detail){
    if (!trace_is_enabled()) return;
    TraceRecord *r = &g_ring[g_head];
    r->turn  = turn;
    r->type  = type;
    r->value = value;
    r->delta = delta;
    copy_field(r->label,  PE_TRACE_LABEL_LEN,  label);
    copy_field(r->detail, PE_TRACE_DETAIL_LEN, detail);
    g_head = (g_head + 1) % PE_TRACE_RING_SIZE;
    if (g_count < PE_TRACE_RING_SIZE) ++g_count;

    if (g_text_fd >= 0){
        char line[256];
        int n = snprintf(line, sizeof(line),
                         "t=%u %s value=%d delta=%d %s%s%s\n",
                         turn, type_str(type), value, delta,
                         label ? label : "",
                         (label && detail) ? " " : "",
                         detail ? detail : "");
        if (n > 0) (void)!write(g_text_fd, line, (size_t)n);
    }
    if (g_json_fd >= 0){
        char line[400];
        int n = snprintf(line, sizeof(line),
                         "{\"t\":%u,\"type\":\"%s\",\"value\":%d,\"delta\":%d,"
                         "\"label\":\"%s\",\"detail\":\"%s\"}\n",
                         turn, type_str(type), value, delta,
                         label ? label : "", detail ? detail : "");
        if (n > 0) (void)!write(g_json_fd, line, (size_t)n);
    }
}

void trace_memory_commit(uint32_t turn, int salience, const char *summary){
    trace_emit(turn, PE_TRACE_MEMORY_COMMIT, salience, 0, "commit", summary);
}
void trace_schema_event(uint32_t turn, int slot, int delta, const char *evt_name){
    trace_emit(turn, PE_TRACE_SCHEMA_EVENT, slot, delta, evt_name, NULL);
}
void trace_render_dispatch(uint32_t turn, const char *backend,
                           int latency_ms, int output_len, uint32_t flags){
    char detail[64];
    snprintf(detail, sizeof(detail), "out_len=%d flags=0x%x", output_len, flags);
    trace_emit(turn, PE_TRACE_RENDER_DISPATCH, latency_ms, (int32_t)flags,
               backend ? backend : "?", detail);
}
void trace_firewall_deny(uint32_t turn, int verdict, int source, const char *label){
    char detail[64];
    snprintf(detail, sizeof(detail), "verdict=%d source=%d", verdict, source);
    trace_emit(turn, PE_TRACE_FIREWALL_DENY, verdict, source,
               label ? label : "deny", detail);
}

int trace_count(void){ return g_count; }

const TraceRecord *trace_at(int idx){
    if (idx < 0 || idx >= g_count) return NULL;
    int start = (g_count < PE_TRACE_RING_SIZE) ? 0
              : (g_head /* head points at oldest when full */);
    int real  = (start + idx) % PE_TRACE_RING_SIZE;
    return &g_ring[real];
}

void trace_clear(void){
    g_head = 0;
    g_count = 0;
}

int trace_dump_text(void){
    int total = 0;
    for (int i = 0; i < g_count; ++i){
        const TraceRecord *r = trace_at(i);
        if (!r) continue;
        char line[256];
        int n = snprintf(line, sizeof(line),
                         "t=%u %s value=%d delta=%d %s%s%s\n",
                         r->turn, type_str(r->type), r->value, r->delta,
                         r->label[0] ? r->label : "",
                         (r->label[0] && r->detail[0]) ? " " : "",
                         r->detail[0] ? r->detail : "");
        if (n > 0){ (void)!write(2, line, (size_t)n); total += n; }
    }
    return total;
}

int trace_dump_json(int fd){
    int total = 0;
    for (int i = 0; i < g_count; ++i){
        const TraceRecord *r = trace_at(i);
        if (!r) continue;
        char line[400];
        int n = snprintf(line, sizeof(line),
                         "{\"t\":%u,\"type\":\"%s\",\"value\":%d,\"delta\":%d,"
                         "\"label\":\"%s\",\"detail\":\"%s\"}\n",
                         r->turn, type_str(r->type), r->value, r->delta,
                         r->label, r->detail);
        if (n > 0){ (void)!write(fd, line, (size_t)n); total += n; }
    }
    return total;
}

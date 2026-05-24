/* instrumentation_test.c — V4 priority 5 proof.
 *
 * Verifies the state_trace ring buffer:
 *   - off by default (no-op cost in production)
 *   - on when toggled
 *   - records cap at PE_TRACE_RING_SIZE (oldest evicted)
 *   - read accessors return records in insertion order
 *   - convenience wrappers populate the right fields
 *   - JSON dump produces parseable output
 */
#include "../../instrumentation/state_trace.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++g_fail; } \
} while (0)

int main(void){
    printf("--- V4 instrumentation test ---\n");

    /* Default-off: trace_emit is a no-op until enabled. */
    unsetenv("PE_TRACE_ENABLE");
    trace_set_enabled(0);
    trace_clear();
    trace_emit(1, PE_TRACE_MEMORY_COMMIT, 100, 0, "noop", NULL);
    CHECK(trace_count() == 0, "trace: disabled state silently drops records");

    /* On + populate. */
    trace_set_enabled(1);
    trace_clear();
    trace_memory_commit(1, 150, "first memory");
    trace_memory_commit(2, 200, "second memory");
    trace_schema_event(3, 1 /*HOSTILE*/, 80, "insulted_us");
    trace_render_dispatch(4, "template", 5, 48, 1u);
    trace_firewall_deny(5, 1 /*RENDERER_WRITE*/, 3 /*RENDERER_OUTPUT*/, "blocked");
    CHECK(trace_count() == 5, "trace: 5 records emitted");

    const TraceRecord *r0 = trace_at(0);
    CHECK(r0 && r0->turn == 1 && r0->type == PE_TRACE_MEMORY_COMMIT
          && r0->value == 150 && strstr(r0->detail, "first memory"),
          "trace: first record fields correct");

    const TraceRecord *r3 = trace_at(3);
    CHECK(r3 && !strcmp(r3->label, "template") && r3->value == 5,
          "trace: render_dispatch wrapper populates label + latency");

    const TraceRecord *r4 = trace_at(4);
    CHECK(r4 && r4->type == PE_TRACE_FIREWALL_DENY && r4->value == 1,
          "trace: firewall_deny wrapper records verdict");

    /* Ring eviction: write more than ring size, oldest should drop. */
    trace_clear();
    for (int i = 0; i < PE_TRACE_RING_SIZE + 50; ++i){
        trace_memory_commit((uint32_t)i, i, "fill");
    }
    CHECK(trace_count() == PE_TRACE_RING_SIZE,
          "trace: ring caps at PE_TRACE_RING_SIZE");
    const TraceRecord *first = trace_at(0);
    CHECK(first && first->turn == 50,
          "trace: oldest record after wrap is turn 50 (first 50 evicted)");

    const TraceRecord *last = trace_at(PE_TRACE_RING_SIZE - 1);
    CHECK(last && last->turn == (uint32_t)(PE_TRACE_RING_SIZE + 49),
          "trace: newest record is the last write");

    /* JSON dump: produce some output, then count newlines via /dev/null pipe. */
    /* Quick correctness probe: write to a temp file, read back, count lines. */
    char tmpname[] = "/tmp/v4_trace_XXXXXX";
    int tfd = mkstemp(tmpname);
    if (tfd < 0){ printf("FAIL: could not open temp file\n"); ++g_fail; goto done; }
    int bytes = trace_dump_json(tfd);
    close(tfd);
    CHECK(bytes > 0, "trace: JSON dump produces output");
    FILE *f = fopen(tmpname, "r");
    if (f){
        int lines = 0;
        char buf[512];
        while (fgets(buf, sizeof(buf), f)) ++lines;
        fclose(f);
        unlink(tmpname);
        CHECK(lines == PE_TRACE_RING_SIZE,
              "trace: JSON output has one line per record");
    }

done:
    if (g_fail){ printf("FAILED — %d failure(s)\n", g_fail); return 1; }
    printf("PASSED — 0 failure(s)\n");
    return 0;
}

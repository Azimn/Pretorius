/* engine_clock.h — canonical time for Layer 1.
 *
 * Doctrine: no engine, memory, schema, or planner code reads the wall clock
 * directly. All canonical time enters Layer 1 through this interface so the
 * five-tuple (cartridge, seed, WAL, canonical inputs, canonical clock)
 * stays a pure function. A test or replay harness pins the clock via
 * PE_CLOCK_OVERRIDE_MS (environment) or pe_clock_set_override_ms()
 * (programmatic). In override mode, day/hour helpers use UTC so the result
 * is independent of the host time zone.
 *
 * Allowed callers of `time(NULL)` / `localtime_r` / `clock_gettime` are:
 *   - this file (the implementation),
 *   - non-canonical tooling (cartridge compilers, packagers, release),
 *   - logging that never feeds back into Layer 1,
 *   - the RNG fallback seed source when no cartridge seed is supplied.
 */
#ifndef PE_ENGINE_CLOCK_H
#define PE_ENGINE_CLOCK_H

#include <stdint.h>

typedef struct {
    int     year;    /* e.g. 2026 */
    uint8_t month;   /* 1..12 */
    uint8_t day;     /* 1..31 */
    uint8_t hour;    /* 0..23 */
    uint8_t minute;  /* 0..59 */
    uint8_t wday;    /* 0=Sunday .. 6=Saturday */
} pe_clock_dt_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical clock readers — these are what engine/memory/schema/planner call. */
uint64_t pe_clock_now_ms(void);
uint32_t pe_clock_now_s(void);
uint32_t pe_clock_today_key(void);   /* packed YYYYMMDD */
uint8_t  pe_clock_local_hour(void);  /* 0..23 */
void     pe_clock_today_dt(pe_clock_dt_t *out);

/* Test / replay overrides. Production code never calls these.
 *  - set_override_ms: freeze the clock at v (ms since Unix epoch).
 *  - advance_ms:      tick the frozen clock by delta ms.
 *  - clear_override:  return to wall clock.
 *
 * The implementation also honors the PE_CLOCK_OVERRIDE_MS environment
 * variable at first use, so a fixture can pin the clock without any
 * in-process call. */
void pe_clock_set_override_ms(uint64_t v);
void pe_clock_advance_ms(uint64_t delta);
void pe_clock_clear_override(void);

/* Returns nonzero if the clock is currently overridden. Useful for debug
 * traces; do not branch behavior on it. */
int  pe_clock_is_overridden(void);

#ifdef __cplusplus
}
#endif
#endif

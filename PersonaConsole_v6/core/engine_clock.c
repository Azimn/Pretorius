/* engine_clock.c — canonical time implementation.  See engine_clock.h. */
#include "engine_clock.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static int      g_inited       = 0;
static int      g_override_set = 0;
static uint64_t g_override_ms  = 0;

static uint64_t wall_now_ms(void){
    struct timespec ts;
    /* CLOCK_REALTIME tracks wall time; CLOCK_MONOTONIC is for rng seed only. */
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void ensure_init(void){
    if (g_inited) return;
    const char *env = getenv("PE_CLOCK_OVERRIDE_MS");
    if (env && *env){
        g_override_ms  = (uint64_t)strtoull(env, NULL, 10);
        g_override_set = 1;
    }
    g_inited = 1;
}

uint64_t pe_clock_now_ms(void){
    ensure_init();
    return g_override_set ? g_override_ms : wall_now_ms();
}

uint32_t pe_clock_now_s(void){
    return (uint32_t)(pe_clock_now_ms() / 1000ull);
}

static void resolve_tm(uint64_t ms, struct tm *out){
    time_t s = (time_t)(ms / 1000ull);
    if (g_override_set){
        /* Deterministic: UTC, independent of host time zone. */
        gmtime_r(&s, out);
    } else {
        /* Live: user's local time, as expected for ordinary operation. */
        localtime_r(&s, out);
    }
}

uint32_t pe_clock_today_key(void){
    struct tm t;
    resolve_tm(pe_clock_now_ms(), &t);
    return (uint32_t)((t.tm_year + 1900) * 10000
                    + (t.tm_mon  + 1)    * 100
                    +  t.tm_mday);
}

uint8_t pe_clock_local_hour(void){
    struct tm t;
    resolve_tm(pe_clock_now_ms(), &t);
    return (uint8_t)t.tm_hour;
}

void pe_clock_today_dt(pe_clock_dt_t *out){
    if (!out) return;
    struct tm t;
    resolve_tm(pe_clock_now_ms(), &t);
    out->year   = t.tm_year + 1900;
    out->month  = (uint8_t)(t.tm_mon + 1);
    out->day    = (uint8_t)t.tm_mday;
    out->hour   = (uint8_t)t.tm_hour;
    out->minute = (uint8_t)t.tm_min;
    out->wday   = (uint8_t)t.tm_wday;
}

void pe_clock_set_override_ms(uint64_t v){
    g_override_ms  = v;
    g_override_set = 1;
    g_inited       = 1;
}

void pe_clock_advance_ms(uint64_t delta){
    ensure_init();
    if (!g_override_set){
        /* If we advance without an explicit set, anchor to current wall now
         * so subsequent reads tick from a known starting point. */
        g_override_ms  = wall_now_ms();
        g_override_set = 1;
    }
    g_override_ms += delta;
}

void pe_clock_clear_override(void){
    g_override_set = 0;
}

int pe_clock_is_overridden(void){
    ensure_init();
    return g_override_set ? 1 : 0;
}

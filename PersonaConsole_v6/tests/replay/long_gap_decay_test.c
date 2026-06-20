/* long_gap_decay_test.c -- closed-form long-gap decay math. */
#include "../../memory/affect_curve.h"
#include "../../schema/schema_state.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)

int main(void){
    printf("--- V6 long-gap decay math ---\n");

    int16_t month = affect_decay_steps(700, 0, 25, 30);
    int16_t year = affect_decay_steps(700, 0, 25, 365);
    CHECK(year < month, "affect_decay_steps continues changing across long elapsed time");

    SchemaState a, b;
    schema_state_init(&a);
    a.slot[SCHEMA_USER_HOSTILE] = 700;
    a.evidence[SCHEMA_USER_HOSTILE] = 500;
    b = a;
    schema_tick_many(&a, 30u * 24u);
    schema_tick_many(&b, 365u * 24u);
    CHECK(b.slot[SCHEMA_USER_HOSTILE] < a.slot[SCHEMA_USER_HOSTILE],
          "schema_tick_many differentiates 30-day and 365-day gaps");

    const uint32_t three_year_hours = 3u * 365u * 24u;
    int16_t high = affect_decay_steps(1000, 990, 8, three_year_hours);
    int16_t low  = affect_decay_steps(1000, 100, 8, three_year_hours);
    CHECK(high > low,
          "high-salience milestone residual remains above low-salience residual after 3 years");
    CHECK(high > 100 && low < 50,
          "three-year salience split is measurable, not a cosmetic difference");

    if (fails){
        printf("FAILED -- %d long-gap decay assertion(s)\n", fails);
        return 1;
    }
    printf("PASSED -- V6 long-gap decay math\n");
    return 0;
}

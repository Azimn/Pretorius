/* long_gap_decay_test.c -- closed-form long-gap decay math. */
#include "../../memory/affect_curve.h"
#include "../../schema/schema_state.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)

int main(void){
    printf("--- V6 long-gap decay math ---\n");

    int16_t step = 700;
    int16_t many = 700;
    for (int i = 0; i < 96; ++i)
        step = affect_decay(step, 0, 25);
    many = affect_decay_steps(many, 0, 25, 96);
    CHECK(step == many, "affect_decay_steps matches repeated one-tick decay for constant rate");

    SchemaState a, b;
    schema_state_init(&a);
    a.slot[SCHEMA_USER_HOSTILE] = 700;
    a.evidence[SCHEMA_USER_HOSTILE] = 500;
    b = a;
    for (int i = 0; i < 96; ++i) schema_tick(&a);
    schema_tick_many(&b, 96);
    CHECK(a.slot[SCHEMA_USER_HOSTILE] == b.slot[SCHEMA_USER_HOSTILE],
          "schema_tick_many matches repeated schema_tick over a short constant window");

    const uint32_t three_year_hours = 3u * 365u * 24u;
    int16_t high = affect_decay_steps(1000, 990, 8, three_year_hours);
    int16_t low  = affect_decay_steps(1000, 100, 8, three_year_hours);
    CHECK(high > low,
          "high-salience milestone residual remains above low-salience residual after 3 years");
    CHECK(high > 100 && low == 0,
          "three-year salience split is measurable, not a cosmetic difference");

    if (fails){
        printf("FAILED -- %d long-gap decay assertion(s)\n", fails);
        return 1;
    }
    printf("PASSED -- V6 long-gap decay math\n");
    return 0;
}

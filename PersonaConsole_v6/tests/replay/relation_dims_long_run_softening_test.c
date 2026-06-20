/* relation_dims_long_run_softening_test.c -- V6 long-run relation cooling. */
#include "../../memory/relation_dims.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)

int main(void){
    printf("--- V6 relation_dims long-run softening ---\n");

    pe_relation_dims_t spike;
    pe_relation_dims_init_from_disposition(&spike, 0x12345678u, 500);
    pe_relation_dims_update_from_input(&spike, 2, 100);
    uint16_t resentment_before = spike.resentment;
    uint16_t threat_before = spike.threat;
    pe_relation_dims_soften_for_gap(&spike, 182u * 86400u);
    CHECK(spike.resentment < resentment_before,
          "single-event resentment spike softens after a six-month gap");
    CHECK(spike.threat < threat_before && spike.threat >= 500,
          "single-event threat spike softens toward the neutral floor");

    pe_relation_dims_t milestone;
    pe_relation_dims_init_from_disposition(&milestone, 0x87654321u, 850);
    milestone.trust = 900;
    milestone.intimacy = 650;
    milestone.admiration = 850;
    pe_relation_dims_soften_for_gap(&milestone, 182u * 86400u);
    CHECK(milestone.trust == 900 && milestone.intimacy == 650 &&
          milestone.admiration == 850,
          "milestone-backed bond dimensions resist absence softening");

    if (fails){
        printf("FAILED -- %d relation long-run assertion(s)\n", fails);
        return 1;
    }
    printf("PASSED -- V6 relation_dims long-run softening\n");
    return 0;
}

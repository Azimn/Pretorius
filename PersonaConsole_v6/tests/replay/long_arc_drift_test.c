/* long_arc_drift_test.c -- V6 earned month-scale baseline drift. */
#include "../../memory/long_arc_drift.h"
#include "../../memory/relation_dims.h"
#include "../../memory/dissonance.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)

static pe_relation_dims_t positive_relation(void){
    pe_relation_dims_t r;
    memset(&r, 0, sizeof(r));
    r.trust = 840;
    r.threat = 420;
    r.intimacy = 640;
    r.resentment = 40;
    r.admiration = 760;
    return r;
}

static pe_relation_dims_t negative_relation(void){
    pe_relation_dims_t r;
    memset(&r, 0, sizeof(r));
    r.trust = 220;
    r.threat = 840;
    r.resentment = 700;
    r.embarrassment = 520;
    return r;
}

int main(void){
    pe_dissonance_t self;
    pe_long_arc_drift_t flexible, rigid, feared;
    pe_relation_dims_t pos = positive_relation();
    pe_relation_dims_t neg = negative_relation();

    printf("--- V6 long-arc drift ---\n");
    pe_dissonance_init(&self);
    self.ideal_self_model = 700;
    self.ought_self_model = 100;
    self.feared_self_model = -650;

    pe_long_arc_drift_init(&flexible);
    pe_long_arc_drift_apply_history(&flexible, &self, &pos, 240, 14);
    CHECK(flexible.baseline_offset == 0,
          "two positive weeks are not enough to move baseline");
    pe_long_arc_drift_apply_history(&flexible, &self, &pos, 240, 120);
    CHECK(flexible.baseline_offset > 0,
          "multi-month positive history moves baseline toward ideal self");
    CHECK(flexible.positive_evidence_days >= 134,
          "positive relationship evidence is cumulative");
    {
        int16_t before = flexible.baseline_offset;
        pe_long_arc_drift_apply_history(&flexible, &self, &pos, 240, 1);
        CHECK(flexible.baseline_offset == before,
              "one extra day does not reapply an already-earned month");
    }

    pe_long_arc_drift_init(&rigid);
    pe_long_arc_drift_apply_history(&rigid, &self, &pos, 0, 180);
    CHECK(rigid.baseline_offset == 0,
          "rigid cartridge with drift_malleability=0 does not drift");
    CHECK(rigid.positive_evidence_days == 0,
          "rigid cartridge does not accumulate drift evidence");

    pe_long_arc_drift_init(&feared);
    pe_long_arc_drift_apply_history(&feared, &self, &neg, 240, 120);
    CHECK(feared.baseline_offset < 0,
          "multi-month inverse history moves baseline toward feared self");
    CHECK(feared.negative_evidence_days >= 120,
          "negative relationship evidence is cumulative");

    if (fails){
        printf("FAILED -- %d long-arc drift assertion(s)\n", fails);
        return 1;
    }
    printf("PASSED -- V6 long-arc drift is slow, bounded, and cartridge-gated\n");
    return 0;
}

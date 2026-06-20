/* affect_dynamics_test.c -- V6 affect contagion, forecast, expression policy. */
#include "../../memory/affect_dynamics.h"
#include "../../memory/relation_dims.h"
#include "../../memory/speech_ledger.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)

static MemoryNode mem_node(uint16_t topic, int8_t valence, uint8_t salience){
    MemoryNode m;
    memset(&m, 0, sizeof(m));
    m.topic_id = topic;
    m.emotion.valence = valence;
    m.emotion.arousal = 60;
    m.salience = salience;
    m.memory_type = MEM_EPISODIC;
    snprintf(m.summary, sizeof(m.summary), "topic memory");
    return m;
}

int main(void){
    printf("--- V6 affect dynamics ---\n");
    pe_relation_dims_t high, low;
    pe_relation_dims_init_from_disposition(&high, 1, 820);
    high.intimacy = 700;
    pe_relation_dims_init_from_disposition(&low, 2, 180);
    low.threat = 850;

    int16_t high_pull = affect_contagion_pull(0, 2, 80, -70, &high, 800);
    int16_t low_pull  = affect_contagion_pull(0, 2, 80, -70, &low, 800);
    printf("      high_pull=%d low_pull=%d\n", high_pull, low_pull);
    CHECK(high_pull < low_pull,
          "high trust/intimacy pulls mood farther toward perceived negative affect than low trust/high threat");

    MemoryStore store;
    memset(&store, 0, sizeof(store));
    store.episodic_count = 3;
    store.episodic[0] = mem_node(7, -80, 240);
    store.episodic[1] = mem_node(7, -60, 180);
    store.episodic[2] = mem_node(8,  80, 240);
    int16_t bad = affect_forecast_topic(7, &store, 800);
    int16_t good = affect_forecast_topic(8, &store, 800);
    CHECK(bad < -400, "negative history yields negative forecast bias");
    CHECK(good > 400, "positive history yields positive forecast bias");

    CHECK(expression_policy_decide(-300, &low, 500, PE_WR_NONE) == PE_EXPR_MASKED,
          "low trust negative internal valence masks expression");
    CHECK(expression_policy_decide(-300, &high, 500, PE_WR_NONE) == PE_EXPR_GENUINE,
          "high trust allows genuine expression");
    CHECK(expression_policy_decide(-300, &high, 500, PE_WR_PRIVACY) == PE_EXPR_WITHHELD,
          "withhold reason logs as withheld, distinct from masked");

    if (fails){
        printf("FAILED -- %d affect dynamics assertion(s)\n", fails);
        return 1;
    }
    printf("PASSED -- V6 affect dynamics and expression policy\n");
    return 0;
}

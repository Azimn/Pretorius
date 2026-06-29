/* v7_1_sovereign_override_test.c -- Layer 1 pressure can redirect a turn. */
#include "../../core/persona.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)

static void prepare(Engine *eng, uint16_t threshold){
    pe_open_loops_init(&eng->open_loops);
    pe_speech_ledger_init(&eng->speech_ledger);
    eng->state.sovereign_override = 0;
    eng->state.sovereign_reason = PE_SOV_NONE;
    eng->state.neutral_streak = 0;
    eng->state.turns_since_question = 0;
    memset(eng->state.want_turns_since_engaged, 0, sizeof(eng->state.want_turns_since_engaged));
    memset(eng->identity.wants, 0, sizeof(eng->identity.wants));
    eng->dissonance.ideal_gap = 0;
    eng->dissonance.ought_gap = 0;
    eng->dissonance.feared_gap = 0;
    eng->state.current_intent = PE_INTENT_ANSWER;
    eng->identity.sovereignty_threshold = threshold;
    eng->input_class = 0;
    eng->matched_group = 0xFFFFu;
    eng->primary_topic = eng->topics.count ? (uint16_t)eng->topics.topics[0].id : 0xFFFFu;
}

int main(void){
    Engine eng;
    char out[512];
    uint32_t actor;
    uint16_t topic;
    const pe_speech_event_t *last;

    printf("--- V7.1 sovereign override ---\n");
    if (persona_open(&eng,"profiles/kiki") != 0){
        printf("FAIL: open Kiki profile\n");
        return 1;
    }
    persona_set_user(&eng,"v7_1_sovereign_actor");
    actor = eng.relation.user_hash;
    topic = eng.topics.count ? (uint16_t)eng.topics.topics[0].id : 0xFFFFu;

    prepare(&eng,500);
    pe_open_loops_record(&eng.open_loops, actor, topic, PE_SA_ASSERTION,
                         800, 300, 100, eng.state.turn_count,
                         eng.state.turn_count + 40u);
    persona_process_input(&eng,"v7_1_sovereign_actor","zorch",out,sizeof(out));
    CHECK(eng.state.sovereign_override==1,"override fires above threshold");
    CHECK(eng.state.current_intent==PE_INTENT_INITIATE,"override sets initiate intent");
    last=pe_speech_ledger_last(&eng.speech_ledger);
    CHECK(last && last->speech_act==PE_SA_REDIRECT,"redirect speech event is logged");

    prepare(&eng,900);
    persona_process_input(&eng,"v7_1_sovereign_actor","zorch",out,sizeof(out));
    CHECK(eng.state.sovereign_override==0,
          "override flag is recomputed and clears on the subsequent quiet turn");

    prepare(&eng,900);
    pe_open_loops_record(&eng.open_loops, actor, topic, PE_SA_ASSERTION,
                         300, 60, 20, eng.state.turn_count,
                         eng.state.turn_count + 40u);
    persona_process_input(&eng,"v7_1_sovereign_actor","zorch",out,sizeof(out));
    CHECK(eng.state.sovereign_override==0,"high threshold blocks low pressure override");

    prepare(&eng,100);
    persona_process_input(&eng,"v7_1_sovereign_actor","zorch",out,sizeof(out));
    CHECK(eng.state.sovereign_override==0,"no open loops, wants, or dissonance means no override");

    persona_close(&eng);
    if (fails){ printf("FAILED -- %d sovereign override assertion(s)\n",fails); return 1; }
    printf("PASSED -- V7.1 sovereign override is thresholded and logged\n");
    return 0;
}
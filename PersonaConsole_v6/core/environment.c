#include "persona.h"
#include "environment.h"
#include "engine_clock.h"
#include <stdio.h>
#include <string.h>

void environment_session_start(Engine *eng){
    eng->environment.session_count++;
    eng->environment.turns_this_session = 0;
    eng->environment.session_start = (time_t)pe_clock_now_s();
    eng->environment.repeated_question_count = 0;
    eng->environment.last_question_hash = 0;
}

void environment_update_turn(Engine *eng, const char *input){
    uint32_t h = persona_hash(input ? input : "");
    eng->environment.total_turns++;
    eng->environment.turns_this_session++;
    if (h == eng->environment.last_question_hash){
        if (eng->environment.repeated_question_count < 0xffff)
            eng->environment.repeated_question_count++;
    } else {
        eng->environment.repeated_question_count = 1;
        eng->environment.last_question_hash = h;
    }
}

void environment_hour_string(char *out, unsigned n){
    if (!out || n == 0) return;
    snprintf(out, n, "%u", (unsigned)pe_clock_local_hour());
}

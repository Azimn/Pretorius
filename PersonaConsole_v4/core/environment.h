/* environment.h - session and repeated-question tracking. */
#ifndef PE_ENVIRONMENT_H
#define PE_ENVIRONMENT_H

#include <stdint.h>
#include <time.h>

typedef struct {
    uint32_t session_count;
    uint32_t total_turns;
    uint32_t turns_this_session;
    time_t   session_start;
    uint16_t repeated_question_count;
    uint32_t last_question_hash;
} Environment;

typedef struct Engine Engine;

void environment_session_start(Engine *eng);
void environment_update_turn(Engine *eng, const char *input);
void environment_hour_string(char *out, unsigned n);

#endif

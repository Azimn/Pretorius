/* intent.h - lightweight weighted intent arbitration. */
#ifndef PE_INTENT_H
#define PE_INTENT_H

#include <stdint.h>

#define MAX_INTENTS 8

typedef struct Engine Engine;

typedef struct {
    const char *name;
    uint8_t id;
    uint8_t weight;
} Intent;

void intent_recompute(Engine *eng, Intent intents[], int *count);

#endif

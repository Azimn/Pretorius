/* identity.h - identity gravity / drift protection. */
#ifndef PE_IDENTITY_H
#define PE_IDENTITY_H

#include <stdint.h>

typedef struct Engine Engine;

void identity_update_rolling(Engine *eng, int16_t valence, int16_t arousal);

#endif

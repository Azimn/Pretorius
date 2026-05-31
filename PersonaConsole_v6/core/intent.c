#include "persona.h"
#include "intent.h"

static uint8_t clamp_u8(int v){
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void add_intent(Intent intents[], int *count,
                       const char *name, uint8_t id, int weight){
    if (*count >= MAX_INTENTS) return;
    intents[*count].name = name;
    intents[*count].id = id;
    intents[*count].weight = clamp_u8(weight);
    (*count)++;
}

void intent_recompute(Engine *eng, Intent intents[], int *count){
    int mood = eng->state.mood;
    int fatigue = eng->state.exhaustion / 8 + eng->state.fatigue / 8;
    int memory_pressure = (eng->active_count > 0) ? eng->active_match[0] / 8 : 0;
    int topic_pressure = eng->state.obsession_pressure / 10;
    int identity_boost = eng->identity_boost_remaining ? 120 : 0;

    *count = 0;
    add_intent(intents, count, "answer", PE_INTENT_ANSWER,
               60 + (eng->input_class == 3 ? 90 : 0) + (mood > 0 ? mood / 40 : 0));
    add_intent(intents, count, "reminisce", PE_INTENT_REMINISCE,
               40 + memory_pressure + identity_boost / 2);
    add_intent(intents, count, "monologue", PE_INTENT_MONOLOGUE,
               45 + topic_pressure + eng->identity.extraversion / 2048);
    add_intent(intents, count, "probe", PE_INTENT_PROBE,
               35 + (eng->input_class == 0 ? 45 : 0) + eng->relation.um_engagement / 8);
    add_intent(intents, count, "withdraw", PE_INTENT_WITHDRAW,
               25 + fatigue + (mood < 0 ? -mood / 20 : 0));
    add_intent(intents, count, "accuse", PE_INTENT_ACCUSE,
               20 + (eng->input_class == 2 ? 120 : 0) + eng->state.irritation_carry / 8);
    add_intent(intents, count, "boast", PE_INTENT_BOAST,
               25 + (eng->input_class == 1 ? 110 : 0) + eng->drives.drives[PE_DRIVE_RECOGNITION].mood_weight);
    add_intent(intents, count, "identity", PE_INTENT_MONOLOGUE,
               30 + identity_boost);
}

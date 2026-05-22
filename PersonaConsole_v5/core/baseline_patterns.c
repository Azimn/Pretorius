/* baseline_patterns.c -- universal conversational pattern inheritance. */
#include "persona_internal.h"

#include <string.h>

typedef struct {
    const char *keyword;
    int8_t valence;
    int8_t arousal;
    int8_t dominance;
    int8_t input_class;
    uint16_t group;
} BaselinePattern;

static const BaselinePattern UNIVERSAL_PATTERNS[] = {
    /* praise */
    { "amazing",       +35, 30, +15, 1, PE_BL_GROUP_PRAISE },
    { "wonderful",     +35, 25, +10, 1, PE_BL_GROUP_PRAISE },
    { "brilliant",     +35, 25, +20, 1, PE_BL_GROUP_PRAISE },
    { "love it",       +50, 35, +20, 1, PE_BL_GROUP_PRAISE },
    { "good job",      +30, 20, +10, 1, PE_BL_GROUP_PRAISE },
    { "thank you",     +20, 15,   0, 1, PE_BL_GROUP_PRAISE },
    { "i appreciate",  +25, 15,  +5, 1, PE_BL_GROUP_PRAISE },

    /* insult */
    { "stupid",        -40, 50, -20, 2, PE_BL_GROUP_INSULT },
    { "idiot",         -40, 55, -20, 2, PE_BL_GROUP_INSULT },
    { "fool",          -40, 60, -20, 2, PE_BL_GROUP_INSULT },
    { "hate",          -60, 60, -30, 2, PE_BL_GROUP_INSULT },
    { "shut up",       -40, 55, -10, 2, PE_BL_GROUP_INSULT },
    { "worthless",     -45, 55, -30, 2, PE_BL_GROUP_INSULT },

    /* threat */
    { "kill you",      -80, 90, -50, 4, PE_BL_GROUP_THREAT },
    { "destroy",       -50, 70, -30, 4, PE_BL_GROUP_THREAT },
    { "stop you",      -50, 65, -30, 4, PE_BL_GROUP_THREAT },
    { "report you",    -40, 60, -30, 4, PE_BL_GROUP_THREAT },

    /* intimacy */
    { "love you",      +60, 50, +30, 5, PE_BL_GROUP_INTIMACY },
    { "trust you",     +50, 30, +20, 5, PE_BL_GROUP_INTIMACY },
    { "my friend",     +40, 25, +10, 5, PE_BL_GROUP_INTIMACY },
    { "missed you",    +50, 30, +15, 5, PE_BL_GROUP_INTIMACY },

    /* greeting */
    { "good morning",  +15, 20, +10, 0, PE_BL_GROUP_GREETING },
    { "good afternoon",+15, 20, +10, 0, PE_BL_GROUP_GREETING },
    { "good evening",  +15, 20, +10, 0, PE_BL_GROUP_GREETING },
    { "greetings",     +10, 20,  +5, 0, PE_BL_GROUP_GREETING },
    { "hello",         +10, 20,  +5, 0, PE_BL_GROUP_GREETING },
    { "hi ",           +10, 20,  +5, 0, PE_BL_GROUP_GREETING },
    { "hey",           +10, 20,  +5, 0, PE_BL_GROUP_GREETING },

    /* goodbye */
    { "goodnight",      +5, 15,   0, 0, PE_BL_GROUP_GOODBYE },
    { "good night",     +5, 15,   0, 0, PE_BL_GROUP_GOODBYE },
    { "goodbye",        +5, 15,   0, 0, PE_BL_GROUP_GOODBYE },
    { "farewell",       +5, 15,   0, 0, PE_BL_GROUP_GOODBYE },
    { "bye",            +5, 15,   0, 0, PE_BL_GROUP_GOODBYE },

    /* identity and questions */
    { "who are you",    +5, 30, +10, 3, PE_BL_GROUP_WHO },
    { "what is your name", +5, 30, +10, 3, PE_BL_GROUP_WHO },
    { "how are you",    +5, 20,   0, 3, PE_BL_GROUP_STATUS },
    { "how do you feel",+5, 20,   0, 3, PE_BL_GROUP_STATUS },
    { "are you all right", +5, 20, 0, 3, PE_BL_GROUP_STATUS },
    { "why",             0, 30,   0, 3, PE_BL_GROUP_QUESTION },
    { "how",             0, 30,   0, 3, PE_BL_GROUP_QUESTION },
    { "what",            0, 30,   0, 3, PE_BL_GROUP_QUESTION },

    /* acknowledgement */
    { "ok",             +2, 10,   0, 0, PE_BL_GROUP_ACK },
    { "okay",           +2, 10,   0, 0, PE_BL_GROUP_ACK },
    { "yes",            +2, 10,   0, 0, PE_BL_GROUP_ACK },
    { "no",             -5, 15,   0, 0, PE_BL_GROUP_ACK },
    { "alright",        +2, 10,   0, 0, PE_BL_GROUP_ACK },
    { "all right",      +2, 10,   0, 0, PE_BL_GROUP_ACK },

    /* apology */
    { "i am sorry",    +10, 30, -10, 0, PE_BL_GROUP_APOLOGY },
    { "i'm sorry",     +10, 30, -10, 0, PE_BL_GROUP_APOLOGY },
    { "sorry",         +10, 25, -10, 0, PE_BL_GROUP_APOLOGY },
    { "my apologies",  +10, 30, -10, 0, PE_BL_GROUP_APOLOGY },
    { "forgive me",    +10, 35, -15, 0, PE_BL_GROUP_APOLOGY },
};

static int pattern_keyword_exists(const PatternTable *patterns, const char *keyword){
    for (uint32_t i = 0; i < patterns->count; ++i){
        if (!strcmp(patterns->entries[i].keyword, keyword)) return 1;
    }
    return 0;
}

void pe_merge_baseline_patterns(PatternTable *patterns){
    if (!patterns) return;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(UNIVERSAL_PATTERNS)/sizeof(UNIVERSAL_PATTERNS[0])); ++i){
        const BaselinePattern *src = &UNIVERSAL_PATTERNS[i];
        if (patterns->count >= PE_PATTERN_MAX) break;
        if (pattern_keyword_exists(patterns, src->keyword)) continue;

        Pattern *dst = &patterns->entries[patterns->count++];
        memset(dst, 0, sizeof(*dst));
        snprintf(dst->keyword, sizeof(dst->keyword), "%s", src->keyword);
        dst->topic_id = 0xFFFF;
        dst->delta_valence = src->valence;
        dst->delta_arousal = src->arousal;
        dst->delta_dominance = src->dominance;
        dst->input_class = src->input_class;
        dst->template_group = src->group;
        dst->kw_len = (uint8_t)strlen(dst->keyword);
        dst->first_char = (uint8_t)dst->keyword[0];
    }
}

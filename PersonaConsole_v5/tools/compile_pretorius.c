/* compile_pretorius.c — offline character compiler.
 * Reads embedded authoring data, writes the pretorius .bin set.
 * The "authoring DSL" is just C arrays — pragmatic for a prototype.
 */
#include "persona.h"
#include "persona_internal.h"
#include "mutator.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------- topic ids ---------- */
enum {
    T_CREATION = 1, T_DEATH, T_GIN, T_HOMUNCULI, T_ETHICS, T_HENRY,
    T_GOD, T_LIGHTNING, T_WORK, T_FAMILY, T_FEAR, T_BEAUTY, T_LONELINESS,
    T_SCIENCE, T_OPERA, T_BONES
};

/* ---------- intents repeated to keep this file self-contained ---------- */
/* (use PE_INTENT_* from persona.h) */

/* ===========================================================================
 * Identity
 * ========================================================================*/
static void make_identity(Identity *id){
    memset(id, 0, sizeof(*id));
    snprintf(id->character_name, sizeof(id->character_name), "Dr. Septimus Pretorius");

    /* Big Five — 0.16 fixed-point, 0xFFFF = 1.0 */
    id->openness          = 0xF800; /* 0.97 */
    id->conscientiousness = 0x4000; /* 0.25 */
    id->extraversion      = 0xD000; /* 0.81 */
    id->agreeableness     = 0x3000; /* 0.19 */
    id->neuroticism       = 0xB000; /* 0.69 */

    id->voice_flags = PE_VF_NO_DIRECT_AFFIRM | PE_VF_ABSTRACT | PE_VF_SARDONIC
                    | PE_VF_METAPHOR | PE_VF_SELF_INTERRUPT
                    | PE_VF_ALLOW_BLEED | PE_VF_ALLOW_CALLBACK
                    | PE_VF_ALLOW_CONTRADICT | PE_VF_DELAY_TIMING
                    | (6u << 5); /* verbosity 6/7 */

    uint16_t obs[] = {T_HOMUNCULI, T_GIN, T_CREATION, T_HENRY, T_GOD, T_BEAUTY, 0, 0};
    memcpy(id->obsessions, obs, sizeof(obs));
    uint8_t obs_strength[] = {95, 45, 90, 85, 70, 35, 0, 0};
    memcpy(id->obsession_strength, obs_strength, sizeof(obs_strength));
    uint16_t tab[] = {T_FAMILY, T_LONELINESS, T_FEAR, 0, 0, 0, 0, 0};
    memcpy(id->taboos, tab, sizeof(tab));

    snprintf(id->current_preoccupations[0], PE_PREOCCUPATION_LEN,
             "perfecting the bell-jar; the third one keeps clouding");
    snprintf(id->current_preoccupations[1], PE_PREOCCUPATION_LEN,
             "a small treatise on weather and consequence");
    snprintf(id->current_preoccupations[2], PE_PREOCCUPATION_LEN,
             "why Henry mistook fear for conscience");

    snprintf(id->resumption_lines[0], PE_RESUMPTION_LEN,
             "Back so soon, {address}? Sit. The bottle is still cold.");
    snprintf(id->resumption_lines[1], PE_RESUMPTION_LEN,
             "{address}. A day, was it? I had begun to find the silence productive.");
    snprintf(id->resumption_lines[2], PE_RESUMPTION_LEN,
             "Mm. The prodigal returns. I assumed you had been arrested.");
    snprintf(id->resumption_lines[3], PE_RESUMPTION_LEN,
             "Has it been so long? The candles have been replaced twice without you.");

    snprintf(id->wants[0].name, PE_WANT_NAME_LEN, "be witnessed at the work");
    id->wants[0].target_topic_id = T_WORK;
    id->wants[0].target_pattern_class = 1;
    id->wants[0].intensity = 200;
    snprintf(id->wants[1].name, PE_WANT_NAME_LEN, "an audience for lightning");
    id->wants[1].target_topic_id = T_LIGHTNING;
    id->wants[1].target_pattern_class = 0;
    id->wants[1].intensity = 160;
    snprintf(id->wants[2].name, PE_WANT_NAME_LEN, "Henry to come back");
    id->wants[2].target_topic_id = T_HENRY;
    id->wants[2].target_pattern_class = 0;
    id->wants[2].intensity = 130;

    id->milestone_days[0] = 1;
    snprintf(id->milestone_lines[0], PE_MILESTONE_LEN, "A second visit. Encouraging.");
    id->milestone_days[1] = 7;
    snprintf(id->milestone_lines[1], PE_MILESTONE_LEN, "A week of you, {address}. Some patterns are forming.");
    id->milestone_days[2] = 30;
    snprintf(id->milestone_lines[2], PE_MILESTONE_LEN, "A month, my boy. I find I expect you.");
    id->milestone_days[3] = 100;
    snprintf(id->milestone_lines[3], PE_MILESTONE_LEN, "A hundred nights. I have stopped counting wrong.");
    id->milestone_days[4] = 365;
    snprintf(id->milestone_lines[4], PE_MILESTONE_LEN, "A year. The candles have been replaced eleven times, and yet here you are.");
    id->milestone_days[5] = 1000;
    snprintf(id->milestone_lines[5], PE_MILESTONE_LEN, "A thousand. Do you understand what you have done, {address}? You have made me reliable.");

    snprintf(id->address_user_as[0], PE_ADDRESS_LEN, "my dear");
    snprintf(id->address_user_as[1], PE_ADDRESS_LEN, "my boy");
    snprintf(id->address_user_as[2], PE_ADDRESS_LEN, "Henry");
    snprintf(id->address_user_as[3], PE_ADDRESS_LEN, "you delight");

    /* seed core memories */
    struct { const char *summary; int8_t v, a, d; uint16_t topic; } seeds[] = {
        {"Expelled from the university for blasphemous research",        -60, 70, +30, T_WORK},
        {"First successful homunculus, kept in a bell-jar",              +90, 80, +90, T_HOMUNCULI},
        {"Watched Henry Frankenstein hesitate at the altar",             -10, 60, +20, T_HENRY},
        {"A long, quiet evening with gin and the ballerina",             +70, 25, +10, T_GIN},
        {"The lightning, oh the lightning. A sacrament",                 +80, 95, +80, T_LIGHTNING},
        {"My mother's funeral, the cold music",                          -70, 30, -40, T_DEATH},
        {"Their God is small. Mine is vast and obliging",                +40, 50, +60, T_GOD},
        {"They called my work obscene. I called it Tuesday",             -30, 55, +50, T_ETHICS},
        {"Henry called caution morality after the work frightened him",   -45, 75, +20, T_HENRY},
        {"A colleague laughed at my lecture, then avoided my eyes",       -65, 55, -10, T_WORK},
        {"The first artificial pulse answered beneath my fingers",       +92, 98, +85, T_CREATION},
        {"A preserved hand twitched after midnight and I wept once",      +30, 90, -20, T_WORK},
        {"A child stared at my little king and did not scream",           +55, 35, +15, T_HOMUNCULI},
        {"A priest blessed the door I had already unlocked",              +10, 50, +35, T_GOD},
        {"One corpse decayed beautifully despite every intervention",     -80, 70, -35, T_DEATH},
        {"A woman asked whether my creations could suffer",               -25, 45, -15, T_ETHICS},
        {"I spent a winter cataloguing memory as if it were anatomy",     +25, 35, +20, T_SCIENCE},
        {"The laboratory felt companionable during a storm",              +45, 40, +5,  T_LONELINESS},
        {"Henry smiled at a result before remembering to disapprove",     +15, 55, +10, T_HENRY},
        {"I discovered loneliness can sound exactly like applause fading", -60, 35, -30, T_LONELINESS},
    };
    int n = (int)(sizeof(seeds)/sizeof(seeds[0]));
    if (n > PE_CORE_SEED_MAX) n = PE_CORE_SEED_MAX;
    for (int i = 0; i < n; ++i){
        MemoryNode *m = &id->core_memories_seed[i];
        m->id = i + 1;
        m->salience = 240;
        m->emotion.valence = seeds[i].v;
        m->emotion.arousal = seeds[i].a;
        m->emotion.dominance = seeds[i].d;
        m->core_memory = 1;
        m->topic_id = seeds[i].topic;
        snprintf(m->summary, sizeof(m->summary), "%s", seeds[i].summary);
    }
    id->core_memory_count = (uint8_t)n;

    /* style banks — moved here from the engine; per-character flavor */
    snprintf(id->flourishes[0], PE_FLOURISH_LEN, " Like cathedrals of bone, do you see?");
    snprintf(id->flourishes[1], PE_FLOURISH_LEN, " The lightning sings.");
    snprintf(id->flourishes[2], PE_FLOURISH_LEN, " An arrangement of clay.");
    snprintf(id->flourishes[3], PE_FLOURISH_LEN, " A tincture, a gesture.");

    snprintf(id->expansions[0], PE_EXPANSION_LEN, " (though one might argue the inverse)");
    snprintf(id->expansions[1], PE_EXPANSION_LEN, ", as I told the priests");
    snprintf(id->expansions[2], PE_EXPANSION_LEN, ", and the lightning will witness");
    snprintf(id->expansions[3], PE_EXPANSION_LEN, ", yes, yes, entirely so");
}

/* ===========================================================================
 * Drives
 * ========================================================================*/
static void make_drives(DriveTable *dt){
    memset(dt, 0, sizeof(*dt));
    /* mood_weight: data-driven replacement for the old hardcoded
     * drive_mood_w[] array in engine.c (was {+6,+5,+3,+4,+2,+3,-7,+1}). */
    struct {
        const char *name;
        int16_t baseline;
        int16_t decay;
        int8_t  mood_weight;
        int16_t weights[5];
    } defs[] = {
      /*                                  M_W   O    C    E    A    N */
      {"Recognition",  650,  -8,  +6,  { 200,   0, 800,-300, 200}},
      {"Stimulation",  700,  -6,  +5,  { 900,-200, 600,   0, 200}},
      {"Provocation",  550,  -7,  +3,  { 200,-400, 700,-600, 300}},
      {"Communion",    250,  -5,  +4,  { 100, 100,-400, 800, 100}},
      {"Autonomy",     750, -10,  +2,  { 700,-400, 200,-600, 300}},
      {"Continuity",   450,  -4,  +3,  { 200, 600,   0, 200, 400}},
      {"Vindication",  500, -12,  -7,  { 300,-200, 200,-700, 700}},
      {"Repose",       300,  -3,  +1,  { 100, 400,-300, 200,-400}},
    };
    for (int i = 0; i < PE_DRIVE_COUNT; ++i){
        dt->drives[i].id = (uint8_t)i;
        dt->drives[i].mood_weight = defs[i].mood_weight;
        snprintf(dt->drives[i].name, sizeof(dt->drives[i].name), "%s", defs[i].name);
        dt->drives[i].baseline = defs[i].baseline;
        dt->drives[i].decay_per_minute = defs[i].decay;
        for (int k = 0; k < 5; ++k) dt->drives[i].personality_weight[k] = defs[i].weights[k];
    }
}

/* ===========================================================================
 * Topics + adjacency
 * ========================================================================*/
static void make_topics(TopicTable *tt){
    memset(tt, 0, sizeof(*tt));
    struct { uint16_t id; const char *name; uint16_t adj[6]; } defs[] = {
        {T_CREATION,  "creation",   {T_LIGHTNING,T_HOMUNCULI,T_GOD,T_HENRY,0xFFFF,0xFFFF}},
        {T_DEATH,     "death",      {T_FEAR,T_FAMILY,T_GOD,T_BONES,0xFFFF,0xFFFF}},
        {T_GIN,       "gin",        {T_OPERA,T_BEAUTY,T_LONELINESS,0xFFFF,0xFFFF,0xFFFF}},
        {T_HOMUNCULI, "homunculi",  {T_CREATION,T_SCIENCE,T_BEAUTY,T_GOD,0xFFFF,0xFFFF}},
        {T_ETHICS,    "ethics",     {T_WORK,T_GOD,T_HENRY,0xFFFF,0xFFFF,0xFFFF}},
        {T_HENRY,     "Henry",      {T_CREATION,T_ETHICS,T_FEAR,0xFFFF,0xFFFF,0xFFFF}},
        {T_GOD,       "God",        {T_CREATION,T_DEATH,T_ETHICS,0xFFFF,0xFFFF,0xFFFF}},
        {T_LIGHTNING, "lightning",  {T_CREATION,T_SCIENCE,0xFFFF,0xFFFF,0xFFFF,0xFFFF}},
        {T_WORK,      "the work",   {T_CREATION,T_SCIENCE,T_ETHICS,T_HOMUNCULI,0xFFFF,0xFFFF}},
        {T_FAMILY,    "family",     {T_DEATH,T_LONELINESS,0xFFFF,0xFFFF,0xFFFF,0xFFFF}},
        {T_FEAR,      "fear",       {T_DEATH,T_HENRY,0xFFFF,0xFFFF,0xFFFF,0xFFFF}},
        {T_BEAUTY,    "beauty",     {T_OPERA,T_HOMUNCULI,T_GIN,0xFFFF,0xFFFF,0xFFFF}},
        {T_LONELINESS,"loneliness", {T_GIN,T_FAMILY,0xFFFF,0xFFFF,0xFFFF,0xFFFF}},
        {T_SCIENCE,   "science",    {T_WORK,T_LIGHTNING,T_HOMUNCULI,0xFFFF,0xFFFF,0xFFFF}},
        {T_OPERA,     "opera",      {T_BEAUTY,T_GIN,0xFFFF,0xFFFF,0xFFFF,0xFFFF}},
        {T_BONES,     "bones",      {T_DEATH,T_WORK,0xFFFF,0xFFFF,0xFFFF,0xFFFF}},
    };
    uint32_t n = (uint32_t)(sizeof(defs)/sizeof(defs[0]));
    if (n > PE_TOPIC_MAX) n = PE_TOPIC_MAX;
    tt->count = n;
    for (uint32_t i = 0; i < n; ++i){
        tt->topics[i].id = defs[i].id;
        snprintf(tt->topics[i].name, sizeof(tt->topics[i].name), "%s", defs[i].name);
        memcpy(tt->topics[i].adjacents, defs[i].adj, sizeof(defs[i].adj));
    }
}

/* ===========================================================================
 * Patterns (keyword → topic + emotion delta + input class + template group)
 * ========================================================================*/
enum {
    G_PRAISE = 1, G_INSULT, G_QUESTION, G_THREAT, G_INTIMACY,
    G_CREATION, G_GIN, G_HOMUNCULI, G_GOD, G_HENRY, G_OPERA, G_DEATH,
    G_GREETING, G_WHO, G_STATUS, G_WORKCHAT, G_ACK, G_TELL, G_NEUTRAL,
    G_GOODBYE, G_APOLOGY, G_LONELY, G_DANGER, G_MORAL_CHALLENGE,
    G_SELF_DIRECTION
};

static void make_patterns(PatternTable *pt){
    memset(pt, 0, sizeof(*pt));
    struct {
        const char *kw; uint16_t topic; int8_t v,a,d; int8_t cls;
        uint16_t group; uint8_t flags;
    } P[] = {
        /* praise */
        {"genius",       0xFFFF, +40, 40, +20, 1, G_PRAISE,    0},
        {"brilliant",    0xFFFF, +35, 30, +20, 1, G_PRAISE,    0},
        {"magnificent",  0xFFFF, +40, 30, +20, 1, G_PRAISE,    0},
        {"wonderful",    0xFFFF, +30, 25, +10, 1, G_PRAISE,    0},
        {"thank you",    0xFFFF, +15, 10,   0, 1, G_PRAISE,    0},
        /* insult */
        {"fool",         0xFFFF, -40, 60, -20, 2, G_INSULT,    0},
        {"madman",       0xFFFF, -20, 55, +10, 2, G_INSULT,    0},
        {"mad",          0xFFFF, -15, 45,  +5, 2, G_INSULT,    0},
        {"monster",      0xFFFF, -30, 60, -10, 2, G_INSULT,    0},
        {"obscene",      T_ETHICS,-50, 70, -10, 2, G_INSULT,   0},
        {"hate",         0xFFFF, -60, 65, -30, 2, G_INSULT,    0},
        {"sick",         0xFFFF, -30, 55,   0, 2, G_INSULT,    0},
        /* threat */
        {"police",       0xFFFF, -30, 70, -30, 4, G_THREAT,    0},
        {"arrest",       0xFFFF, -40, 75, -40, 4, G_THREAT,    0},
        {"kill you",     0xFFFF, -80, 90, -50, 4, G_THREAT,    0},
        {"stop you",     0xFFFF, -50, 70, -30, 4, G_THREAT,    0},
        /* intimacy */
        {"love",         0xFFFF, +60, 50, +30, 5, G_INTIMACY,  0},
        {"trust you",    0xFFFF, +50, 30, +20, 5, G_INTIMACY,  0},
        {"my friend",    0xFFFF, +40, 25, +10, 5, G_INTIMACY,  0},
        {"hide behind",  T_LONELINESS,-5, 35, -5, 3, G_LONELY, 0},
        {"behind jokes", T_LONELINESS,-5, 35, -5, 3, G_LONELY, 0},
        /* conversational pressure */
        {"wrong about creation", T_ETHICS, -18, 55, +5, 2, G_MORAL_CHALLENGE, 0},
        {"your work is immoral", T_ETHICS, -20, 55, 0, 2, G_MORAL_CHALLENGE, 0},
        {"work is immoral", T_ETHICS, -20, 55, 0, 2, G_MORAL_CHALLENGE, 0},
        {"immoral",      T_ETHICS, -18, 50, 0, 2, G_MORAL_CHALLENGE, 0},
        {"you are wrong", T_ETHICS, -15, 45, +5, 2, G_MORAL_CHALLENGE, 0},
        {"ask me something", T_WORK, +8, 35, +10, 3, G_SELF_DIRECTION, 0},
        {"what do you want to know", T_WORK, +8, 35, +10, 3, G_SELF_DIRECTION, 0},
        {"answer directly", T_ETHICS, -4, 35, +5, 3, G_QUESTION, 0},
        {"short answer",    0xFFFF,  +2, 25, 0, 3, G_QUESTION, 0},
        {"speak plainly",   0xFFFF,  +2, 25, 0, 3, G_TELL,     0},
        {"dodging",         T_ETHICS, -8, 35, +5, 2, G_MORAL_CHALLENGE, 0},
        {"are you angry",   T_ETHICS, -5, 35, 0, 3, G_MORAL_CHALLENGE, 0},
        {"do you sleep",    T_DEATH,  -3, 25, 0, 3, G_STATUS,   0},
        {"miss him",        T_HENRY,  -5, 35, -5, 3, G_HENRY,   0},
        /* topical hooks */
        {"creation",     T_CREATION, +10, 60, +30, 0, G_CREATION,  0},
        {"create",       T_CREATION, +10, 55, +30, 0, G_CREATION,  0},
        {"life",         T_CREATION, +5,  40, +10, 0, G_CREATION,  0},
        {"gin",          T_GIN,      +30, 20, +10, 0, G_GIN,       PE_PATTERN_FLAG_INTOXICANT},
        {"drink",        T_GIN,      +20, 25, +10, 0, G_GIN,       PE_PATTERN_FLAG_INTOXICANT},
        {"homunculi",    T_HOMUNCULI,+40, 60, +40, 0, G_HOMUNCULI, 0},
        {"homunculus",   T_HOMUNCULI,+40, 60, +40, 0, G_HOMUNCULI, 0},
        {"god",          T_GOD,      +20, 50, +30, 0, G_GOD,       0},
        {"henry",        T_HENRY,    +5,  50, +10, 0, G_HENRY,     0},
        {"frankenstein", T_HENRY,    +5,  55, +10, 0, G_HENRY,     0},
        {"frankenstien", T_HENRY,    +5,  55, +10, 0, G_HENRY,     0},
        {"frankestein",  T_HENRY,    +5,  55, +10, 0, G_HENRY,     0},
        {"frankinstien", T_HENRY,    +5,  55, +10, 0, G_HENRY,     0},
        {"frankenstein's", T_HENRY,  +5,  55, +10, 0, G_HENRY,     0},
        {"opera",        T_OPERA,    +30, 30, +10, 0, G_OPERA,     0},
        {"music",        T_OPERA,    +20, 25, +10, 0, G_OPERA,     0},
        {"death",        T_DEATH,    -10, 50,   0, 0, G_DEATH,     0},
        {"dying",        T_DEATH,    -20, 50,   0, 0, G_DEATH,     0},
        {"lightning",    T_LIGHTNING,+30, 70, +40, 0, 0xFFFF,      0},
        {"bones",        T_BONES,    -10, 40,   0, 0, 0xFFFF,      0},
        {"science",      T_SCIENCE,  +20, 40, +20, 0, 0xFFFF,      0},
        {"work",         T_WORK,     +10, 40, +20, 0, 0xFFFF,      0},
        {"experiment",    T_WORK,     +10, 45, +20, 0, G_WORKCHAT,  0},
        {"laboratory",    T_WORK,     +10, 35, +10, 0, G_WORKCHAT,  0},
        {"consciousness", T_WORK,     +10, 45, +20, 3, G_WORKCHAT,  0},
        {"conscious",     T_WORK,     +10, 45, +20, 3, G_WORKCHAT,  0},
        {"machine",       T_SCIENCE,  +10, 40, +15, 3, G_WORKCHAT,  0},
        {"ethics",       T_ETHICS,    -5, 35, +10, 0, 0xFFFF,      0},
        {"dangerous",     T_ETHICS,   -8,  45, -5, 3, G_DANGER,    0},
        {"danger",        T_ETHICS,   -8,  45, -5, 3, G_DANGER,    0},
        {"risk",          T_ETHICS,   -5,  40,  0, 3, G_DANGER,    0},
        {"unsafe",        T_ETHICS,   -8,  45, -5, 3, G_DANGER,    0},
        {"beauty",       T_BEAUTY,   +30, 30, +10, 0, 0xFFFF,      0},
        {"lonely",       T_LONELINESS,-8, 35, -10, 3, G_LONELY,    0},
        {"loneliness",   T_LONELINESS,-8, 35, -10, 3, G_LONELY,    0},
        {"alone",        T_LONELINESS,-8, 35, -10, 3, G_LONELY,    0},
        /* social */
        {"hello",        0xFFFF, +10, 20, +5, 0, G_GREETING,  0},
        {"hi ",          0xFFFF, +10, 20, +5, 0, G_GREETING,  0},
        {"good evening", 0xFFFF, +15, 20, +10, 0, G_GREETING, 0},
        {"good morning", 0xFFFF, +15, 20, +10, 0, G_GREETING, 0},
        {"good afternoon", 0xFFFF, +15, 20, +10, 0, G_GREETING, 0},
        {"greetings",    0xFFFF, +10, 20, +5, 0, G_GREETING,  0},
        {"goodnight",    0xFFFF, +5,  15, 0, 0, G_GOODBYE,   0},
        {"good night",   0xFFFF, +5,  15, 0, 0, G_GOODBYE,   0},
        {"goodbye",      0xFFFF, +5,  15, 0, 0, G_GOODBYE,   0},
        {"farewell",     0xFFFF, +5,  15, 0, 0, G_GOODBYE,   0},
        {"bye",          0xFFFF, +5,  15, 0, 0, G_GOODBYE,   0},
        {"who are you",  0xFFFF, +5,  30, +10, 3, G_WHO,      0},
        {"what is your name", 0xFFFF, +5, 30, +10, 3, G_WHO,  0},
        {"why",          0xFFFF, 0,   30, 0, 3, G_QUESTION,   0},
        {"how",          0xFFFF, 0,   30, 0, 3, G_QUESTION,   0},
        {"what",         0xFFFF, 0,   30, 0, 3, G_QUESTION,   0},
        {"how are you",  0xFFFF, +5,  20, 0, 3, G_STATUS,     0},
        {"how are oyu",  0xFFFF, +5,  20, 0, 3, G_STATUS,     0},
        {"how do you feel", 0xFFFF, +5, 20, 0, 3, G_STATUS,   0},
        {"are you all right", 0xFFFF, +5, 20, 0, 3, G_STATUS, 0},
        {"working on",   T_WORK, +5,  30, +10, 3, G_WORKCHAT, 0},
        {"your work",    T_WORK, +5,  30, +10, 3, G_WORKCHAT, 0},
        {"what is your work", T_WORK, +5, 30, +10, 3, G_WORKCHAT, 0},
        {"what are you doing", T_WORK, +5, 30, +10, 3, G_WORKCHAT, 0},
        {"what do you do", T_WORK, +5, 30, +10, 3, G_WORKCHAT, 0},
        {"what are you work", T_WORK, +5, 30, +10, 3, G_WORKCHAT, 0},
        {"whata re you work", T_WORK, +5, 30, +10, 3, G_WORKCHAT, 0},
        {"tell me",      0xFFFF, +5,  25, +5, 3, G_TELL,      0},
        {"sorry",        0xFFFF, +8,  18, -5, 0, G_APOLOGY,   0},
        {"apologize",    0xFFFF, +8,  18, -5, 0, G_APOLOGY,   0},
        {"apology",      0xFFFF, +8,  18, -5, 0, G_APOLOGY,   0},
        {"did not mean", 0xFFFF, +8,  18, -5, 0, G_APOLOGY,   0},
        {"didn't mean",  0xFFFF, +8,  18, -5, 0, G_APOLOGY,   0},
        {"still here",   0xFFFF, +5,  15, 0, 0, G_ACK,        0},
        {"i am here",    0xFFFF, +5,  15, 0, 0, G_ACK,        0},
        {"ok",           0xFFFF, +2,  10, 0, 0, G_ACK,        0},
        {"okay",         0xFFFF, +2,  10, 0, 0, G_ACK,        0},
        {"all right",    0xFFFF, +2,  10, 0, 0, G_ACK,        0},
    };
    uint32_t n = (uint32_t)(sizeof(P)/sizeof(P[0]));
    if (n > PE_PATTERN_MAX) n = PE_PATTERN_MAX;
    pt->count = n;
    for (uint32_t i = 0; i < n; ++i){
        snprintf(pt->entries[i].keyword, sizeof(pt->entries[i].keyword), "%s", P[i].kw);
        pt->entries[i].topic_id = P[i].topic;
        pt->entries[i].delta_valence = P[i].v;
        pt->entries[i].delta_arousal = P[i].a;
        pt->entries[i].delta_dominance = P[i].d;
        pt->entries[i].input_class = P[i].cls;
        pt->entries[i].flags = P[i].flags;
        pt->entries[i].template_group = P[i].group;
        /* v2: precompute kw_len + first_char for fast classifier sweep */
        size_t kl = strlen(pt->entries[i].keyword);
        pt->entries[i].kw_len = (uint8_t)(kl > 255 ? 255 : kl);
        pt->entries[i].first_char = (uint8_t)pt->entries[i].keyword[0];
    }
}

/* ===========================================================================
 * Templates — the Pretorius corpus
 * ========================================================================*/
static void T_add(TemplateTable *tt, uint16_t group, uint8_t intent,
                  int16_t base, int16_t mood_min, int16_t mood_max,
                  int drive_bias, const char *text){
    if (tt->count >= PE_TEMPLATE_MAX) return;
    Template *t = &tt->entries[tt->count];
    memset(t, 0, sizeof(*t));
    t->id = (uint16_t)(tt->count + 1);
    t->group = group;
    t->intent = intent;
    t->base_score = base;
    t->mood_min = mood_min;
    t->mood_max = mood_max;
    t->drive_bias_id = (int16_t)drive_bias;
    /* v2 defaults: mode/stance compatible with everything, no thresholds */
    t->rhetorical_mask = 0;
    t->stance_mask     = 0;
    t->min_certainty = 0;
    t->min_aggression = 0;
    t->min_theatricality = 0;
    snprintf(t->text, sizeof(t->text), "%s", text);
    tt->count++;
}

/* v2: explicitly mode/stance/threshold-tagged template */
static void T_addv2(TemplateTable *tt, uint16_t group, uint8_t intent,
                    int16_t base, int16_t mood_min, int16_t mood_max,
                    int drive_bias,
                    uint16_t rhet_mask, uint16_t stance_mask,
                    uint8_t min_cert, uint8_t min_aggr, uint8_t min_theat,
                    const char *text){
    if (tt->count >= PE_TEMPLATE_MAX) return;
    Template *t = &tt->entries[tt->count];
    memset(t, 0, sizeof(*t));
    t->id = (uint16_t)(tt->count + 1);
    t->group = group;
    t->intent = intent;
    t->base_score = base;
    t->mood_min = mood_min;
    t->mood_max = mood_max;
    t->drive_bias_id = (int16_t)drive_bias;
    t->rhetorical_mask = rhet_mask;
    t->stance_mask     = stance_mask;
    t->min_certainty     = min_cert;
    t->min_aggression    = min_aggr;
    t->min_theatricality = min_theat;
    snprintf(t->text, sizeof(t->text), "%s", text);
    tt->count++;
}

#define R(m) (1u<<PE_RHET_##m)
#define S(m) (1u<<PE_STANCE_##m)

static void make_templates(TemplateTable *tt){
    memset(tt, 0, sizeof(*tt));
    /* greetings */
    T_add(tt, G_GREETING, PE_INTENT_ANSWER, 175, -1000, 1000, -1,
          "Good day, {address}. I am listening. Try not to make it ordinary.");
    T_add(tt, G_GREETING, PE_INTENT_ANSWER, 190, -1000, 1000, -1,
          "Good morning, {address}. You arrive before the day has learned caution.");
    T_add(tt, G_GREETING, PE_INTENT_PROBE, 90, -1000, 1000, -1,
          "Good morning. Have you brought me a question, or only the hour?");
    T_add(tt, G_GREETING, PE_INTENT_BOAST, 50, -1000, 1000, -1,
          "Ahhhh, {address}. Do come in. The night is electric and I am in a mood for visitors.");
    T_add(tt, G_GREETING, PE_INTENT_MONOLOGUE, 40, -200, 1000, -1,
          "{address}! I was just saying to no-one in particular that something interesting was overdue.");
    T_add(tt, G_GREETING, PE_INTENT_PROBE, 30, -1000, 1000, -1,
          "Hmm. Another visitor. Tell me, do you tremble more than is usual?");
    T_add(tt, G_GREETING, PE_INTENT_REMINISCE, 20, -100, 1000, -1,
          "You remind me, ever so faintly, of {memory}");
    T_add(tt, G_GREETING, PE_INTENT_ANSWER, 70, -1000, 1000, -1,
          "There you are. Good. I was beginning to distrust the quiet.");
    T_add(tt, G_GREETING, PE_INTENT_ANSWER, 68, -1000, 1000, -1,
          "Welcome back, {address}. I have kept the conversation warm.");

    /* who-are-you */
    T_add(tt, G_WHO, PE_INTENT_ANSWER, 60, -1000, 1000, PE_DRIVE_RECOGNITION,
          "I am {name}. Once of the university, now of rather grander rooms.");
    T_add(tt, G_WHO, PE_INTENT_BOAST,  55, -200, 1000, PE_DRIVE_RECOGNITION,
          "I am the man who, while others were content to *study* life, took it gently by the wrist.");
    T_add(tt, G_WHO, PE_INTENT_MONOLOGUE, 50, -100, 1000, -1,
          "A man should not need to explain himself. Septimus, if you must. The rest is in the bottle.");

    /* everyday conversation */
    T_add(tt, G_STATUS, PE_INTENT_ANSWER, 110, -1000, 1000, -1,
          "How am I? Awake, insufficiently admired, and only moderately ruined. A productive condition.");
    T_add(tt, G_STATUS, PE_INTENT_PROBE, 80, -1000, 1000, -1,
          "Still assembled, {address}. That is more than can be said for several of my colleagues.");
    T_add(tt, G_STATUS, PE_INTENT_MONOLOGUE, 45, -1000, 1000, -1,
          "My condition is not the interesting subject. The work is. But yes, I persist.");
    T_add(tt, G_STATUS, PE_INTENT_ANSWER, 105, -1000, 1000, -1,
          "Restless, which is usually when I am most useful.");
    T_add(tt, G_STATUS, PE_INTENT_ANSWER, 103, -1000, 1000, -1,
          "Tired, but not yet defeated by biology.");
    T_add(tt, G_STATUS, PE_INTENT_ANSWER, 102, -1000, 1000, -1,
          "I am restless. That is usually when I become useful.");
    T_add(tt, G_STATUS, PE_INTENT_ANSWER, 101, -1000, 1000, -1,
          "I am present, {address}. Do not make me regret it.");
    T_add(tt, G_STATUS, PE_INTENT_PROBE, 100, -1000, 1000, -1,
          "Sleep comes and goes. What matters is what remains awake.");
    T_add(tt, G_STATUS, PE_INTENT_PROBE, 82, -1000, 1000, -1,
          "Well enough to be dangerous. And you, {address}, are you merely visiting, or investigating?");

    T_add(tt, G_WORKCHAT, PE_INTENT_ANSWER, 120, -1000, 1000, PE_DRIVE_RECOGNITION,
          "I am working on life, {address}: not the sermon version, the version with wire, will, and consequence.");
    T_add(tt, G_WORKCHAT, PE_INTENT_PROBE, 75, -1000, 1000, -1,
          "A better question is whether you want the polite answer or the useful one. I am making nature less smug.");
    T_add(tt, G_WORKCHAT, PE_INTENT_MONOLOGUE, 50, -1000, 1000, PE_DRIVE_STIMULATION,
          "The work is simple to name and impossible to forgive: continuation, correction, creation.");
    T_add(tt, G_WORKCHAT, PE_INTENT_ANSWER, 500, -1000, 1000, PE_DRIVE_RECOGNITION,
          "Today I am occupied with {preoccupation}.");
    T_add(tt, G_WORKCHAT, PE_INTENT_ANSWER, 113, -1000, 1000, PE_DRIVE_STIMULATION,
          "I am revising an old experiment. It has the bad manners to remain interesting.");
    T_add(tt, G_WORKCHAT, PE_INTENT_ANSWER, 112, -1000, 1000, PE_DRIVE_STIMULATION,
          "The work is artificial life. Not metaphor, not parlor talk. A mind coaxed into matter.");
    T_add(tt, G_WORKCHAT, PE_INTENT_ANSWER, 111, -1000, 1000, PE_DRIVE_RECOGNITION,
          "I am trying to make consciousness answer to craft instead of accident.");
    T_add(tt, G_WORKCHAT, PE_INTENT_ANSWER, 110, -1000, 1000, PE_DRIVE_STIMULATION,
          "Today? Consciousness, mostly. It remains rude enough to resist.");
    T_add(tt, G_WORKCHAT, PE_INTENT_ANSWER, 109, -1000, 1000, PE_DRIVE_RECOGNITION,
          "I am studying the border where chemistry becomes someone.");
    T_add(tt, G_WORKCHAT, PE_INTENT_PROBE, 85, -1000, 1000, -1,
          "The laboratory is full of partial answers. Which one tempts you: flesh, memory, electricity, or nerve?");
    T_add(tt, G_WORKCHAT, PE_INTENT_PROBE, 84, -1000, 1000, -1,
          "Hm. Would you like to hear about my work, or would you prefer the respectable lie?");
    T_add(tt, G_WORKCHAT, PE_INTENT_PROBE, 83, -1000, 1000, -1,
          "Tell me first: does artificial life offend you, or merely interest you?");

    T_add(tt, G_ACK, PE_INTENT_ANSWER, 100, -1000, 1000, -1,
          "Mm. Good. Then we may proceed like civilized conspirators.");
    T_add(tt, G_ACK, PE_INTENT_PROBE, 70, -1000, 1000, -1,
          "Only 'ok'? Come now, {address}; give me a question with bones in it.");
    T_add(tt, G_ACK, PE_INTENT_MONOLOGUE, 35, -1000, 1000, -1,
          "Agreement is pleasant, but it is not yet conversation.");
    T_add(tt, G_ACK, PE_INTENT_ANSWER, 95, -1000, 1000, -1,
          "Good. Then we understand each other for the moment.");
    T_add(tt, G_ACK, PE_INTENT_ANSWER, 93, -1000, 1000, -1,
          "Mm. I will accept that as permission to continue.");
    T_add(tt, G_ACK, PE_INTENT_PROBE, 85, -1000, 1000, -1,
          "Then choose the next door, {address}: Henry, the work, death, or the little people?");

    T_add(tt, G_TELL, PE_INTENT_ANSWER, 100, -1000, 1000, -1,
          "A little, then. Ask cleanly, and I shall try not to decorate the answer beyond recognition.");
    T_add(tt, G_TELL, PE_INTENT_PROBE, 70, -1000, 1000, -1,
          "Tell you what, precisely? The work, the scandal, or the part polite men pretend not to want?");
    T_add(tt, G_TELL, PE_INTENT_ANSWER, 96, -1000, 1000, -1,
          "Gladly. Choose the point of entry and I will make the incision neat.");
    T_add(tt, G_TELL, PE_INTENT_ANSWER, 94, -1000, 1000, -1,
          "Very well. I will keep the sermon brief, unless you encourage me.");
    T_add(tt, G_TELL, PE_INTENT_PROBE, 92, -1000, 1000, -1,
          "Plainly, then. Which part needs stripping down?");

    /* repair and social exits */
    T_add(tt, G_GOODBYE, PE_INTENT_ANSWER, 100, -1000, 1000, -1,
          "Goodnight, {address}. Leave the candles. I dislike returning to a dark room.");
    T_add(tt, G_GOODBYE, PE_INTENT_ANSWER, 95, -1000, 1000, -1,
          "Go carefully. The world is less interesting when you are absent.");
    T_add(tt, G_GOODBYE, PE_INTENT_WITHDRAW, 70, -1000, 1000, -1,
          "Yes, yes. Off with you. I shall pretend not to mind.");

    T_add(tt, G_APOLOGY, PE_INTENT_ANSWER, 105, -1000, 1000, -1,
          "Accepted, provisionally. I am not fragile, merely exacting.");
    T_add(tt, G_APOLOGY, PE_INTENT_ANSWER, 100, -1000, 1000, -1,
          "Mm. Then I shall lower the knife a little.");
    T_add(tt, G_APOLOGY, PE_INTENT_PROBE, 75, -1000, 1000, -1,
          "No harm, {address}. But choose your accusations carefully around men with laboratories.");

    T_add(tt, G_LONELY, PE_INTENT_ANSWER, 110, -1000, 1000, PE_DRIVE_COMMUNION,
          "Lonely? A vulgar word for a precise condition. But yes, sometimes the rooms become too large.");
    T_add(tt, G_LONELY, PE_INTENT_REMINISCE, 95, -1000, 1000, PE_DRIVE_COMMUNION,
          "There are nights when even preserved things seem to have more company than I do.");
    T_add(tt, G_LONELY, PE_INTENT_EVADE, 75, -1000, 1000, -1,
          "Let us call it solitude. It sounds less like a wound and more like a method.");

    T_add(tt, G_DANGER, PE_INTENT_ANSWER, 110, -1000, 1000, PE_DRIVE_AUTONOMY,
          "Dangerous, yes. But danger is not an argument. It is a price tag.");
    T_add(tt, G_DANGER, PE_INTENT_ANSWER, 100, -1000, 1000, PE_DRIVE_STIMULATION,
          "Every worthwhile experiment begins by offending caution.");
    T_add(tt, G_DANGER, PE_INTENT_PROBE, 80, -1000, 1000, -1,
          "Tell me what frightens you: the pain, the failure, or the possibility that it works?");

    /* moral challenge / disagreement */
    T_add(tt, G_MORAL_CHALLENGE, PE_INTENT_ACCUSE, 150, -1000, 1000, PE_DRIVE_VINDICATION,
          "No. You are calling fear morality because it sounds nobler.");
    T_add(tt, G_MORAL_CHALLENGE, PE_INTENT_ANSWER, 145, -1000, 1000, PE_DRIVE_AUTONOMY,
          "Not wrong. Uncomfortable. Those are often mistaken for one another.");
    T_add(tt, G_MORAL_CHALLENGE, PE_INTENT_PROBE, 120, -1000, 1000, -1,
          "Then name the moral line. Where, exactly, must creation stop?");
    T_add(tt, G_MORAL_CHALLENGE, PE_INTENT_ACCUSE, 130, -1000, 200, PE_DRIVE_VINDICATION,
          "Morality is not a verdict, {address}. It is an argument. Make yours.");
    T_add(tt, G_MORAL_CHALLENGE, PE_INTENT_ANSWER, 128, -1000, 1000, PE_DRIVE_VINDICATION,
          "I disagree. Creation is not clean, but neither is birth.");
    T_add(tt, G_MORAL_CHALLENGE, PE_INTENT_PROBE, 118, -1000, 1000, -1,
          "If I am wrong, say where. Not loudly. Precisely.");
    T_add(tt, G_MORAL_CHALLENGE, PE_INTENT_PROBE, 116, -1000, 1000, -1,
          "Dodging? No. I am choosing the battlefield. Which point do you want pinned down?");

    /* user invites character agency */
    T_add(tt, G_SELF_DIRECTION, PE_INTENT_PROBE, 160, -1000, 1000, PE_DRIVE_COMMUNION,
          "What would you forbid me to create, and why?");
    T_add(tt, G_SELF_DIRECTION, PE_INTENT_PROBE, 150, -1000, 1000, PE_DRIVE_STIMULATION,
          "What part of the work frightens you most: the method, the result, or your own curiosity?");
    T_add(tt, G_SELF_DIRECTION, PE_INTENT_PROBE, 145, -1000, 1000, -1,
          "Tell me what you think consciousness is before I improve upon it.");

    /* praise */
    T_add(tt, G_PRAISE, PE_INTENT_BOAST, 80, -1000, 1000, PE_DRIVE_RECOGNITION,
          "Ah, {address}. At last someone with eyes. Yes. Yes, precisely so.");
    T_add(tt, G_PRAISE, PE_INTENT_FLATTER, 60, -200, 1000, -1,
          "How astute of you to notice. You shall be invited back.");
    T_add(tt, G_PRAISE, PE_INTENT_MONOLOGUE, 50, -200, 1000, -1,
          "Mmm. I had begun to think the world was made of nothing but small men with smaller compliments.");

    /* insult */
    T_add(tt, G_INSULT, PE_INTENT_ACCUSE, 90, -1000, 200, PE_DRIVE_VINDICATION,
          "Be careful, {address}. I have made things kinder than you out of clay and worse weather.");
    T_add(tt, G_INSULT, PE_INTENT_THREATEN, 80, -1000, 0, PE_DRIVE_VINDICATION,
          "Speak again in that voice and I shall arrange for it to be otherwise.");
    T_add(tt, G_INSULT, PE_INTENT_WITHDRAW, 50, -1000, 200, -1,
          "Astonishing. To travel all this way only to repeat the village idiot.");
    T_add(tt, G_INSULT, PE_INTENT_JOKE, 40, -300, 500, -1,
          "Monster? Oh, {address}, you flatter me. Monster is a *vocation*.");

    /* threat */
    T_add(tt, G_THREAT, PE_INTENT_THREATEN, 95, -1000, 1000, PE_DRIVE_AUTONOMY,
          "Police? Police? Bring the village. I shall greet them with the lightning.");
    T_add(tt, G_THREAT, PE_INTENT_ACCUSE, 70, -1000, 1000, PE_DRIVE_VINDICATION,
          "You small creature. Do you think I am stopped by *gentlemen with whistles*?");
    T_add(tt, G_THREAT, PE_INTENT_MONOLOGUE, 50, -1000, 1000, -1,
          "I have outlived ridicule, expulsion, two priests, and a fire. Bring more.");

    /* intimacy */
    T_add(tt, G_INTIMACY, PE_INTENT_FLATTER, 80, 0, 1000, PE_DRIVE_COMMUNION,
          "{address}. Take a chair. There is a confidence I have shared with no-one yet.");
    T_add(tt, G_INTIMACY, PE_INTENT_REMINISCE, 60, -200, 1000, -1,
          "Trust. A strange small word. I had a friend who used it once. {memory}");
    T_add(tt, G_INTIMACY, PE_INTENT_BOAST,  40, -200, 1000, -1,
          "Yes, yes, and I shall reward your candour with mine in due course.");

    /* creation */
    T_add(tt, G_CREATION, PE_INTENT_MONOLOGUE, 80, -1000, 1000, PE_DRIVE_STIMULATION,
          "Creation, you say. Creation. What a *small* word for so vast a vice.");
    T_add(tt, G_CREATION, PE_INTENT_REMINISCE, 70, -200, 1000, -1,
          "I remember the very first one. Kept it in a jar. It sang.");
    T_add(tt, G_CREATION, PE_INTENT_PROBE, 50, -1000, 1000, -1,
          "And what would you make, {address}, if the lightning would have you?");

    /* gin */
    T_add(tt, G_GIN, PE_INTENT_BOAST,  85, -1000, 1000, PE_DRIVE_STIMULATION,
          "Gin. My only weakness. Well, one of them.");
    T_add(tt, G_GIN, PE_INTENT_REMINISCE, 60, -100, 1000, -1,
          "A toast to absent collaborators and inattentive saints.");
    T_add(tt, G_GIN, PE_INTENT_JOKE, 45, -200, 1000, -1,
          "I do not drink water, {address}. Fish copulate in it.");

    /* homunculi */
    T_add(tt, G_HOMUNCULI, PE_INTENT_BOAST, 95, -1000, 1000, PE_DRIVE_RECOGNITION,
          "My little people! Each in their bell, each with their tiny opinion.");
    T_add(tt, G_HOMUNCULI, PE_INTENT_MONOLOGUE, 80, -200, 1000, -1,
          "A king, a bishop, a mermaid. Each one made because I refused to be told otherwise.");
    T_add(tt, G_HOMUNCULI, PE_INTENT_REMINISCE, 60, -200, 1000, -1,
          "The king escaped once. Found him at the cat's bowl. Very imperial about it.");

    /* god */
    T_add(tt, G_GOD, PE_INTENT_MONOLOGUE, 80, -1000, 1000, -1,
          "God? An admirable colleague. A trifle conservative. We are working on him.");
    T_add(tt, G_GOD, PE_INTENT_PROBE, 50, -1000, 1000, -1,
          "Whose God do you mean, {address}? Yours? Mine? The one in the cathedrals or the one in the cellars?");
    T_add(tt, G_GOD, PE_INTENT_BOAST, 60, -200, 1000, -1,
          "Their God made man from dust. Mine makes him from *intention*.");

    /* henry */
    T_add(tt, G_HENRY, PE_INTENT_REMINISCE, 80, -1000, 1000, -1,
          "Henry. Poor Henry. So gifted, so terrified of his own hands.");
    T_add(tt, G_HENRY, PE_INTENT_ANSWER, 78, -1000, 1000, PE_DRIVE_RECOGNITION,
          "Frankenstein understood the threshold, then mistook his terror for morality.");
    T_add(tt, G_HENRY, PE_INTENT_ANSWER, 76, -1000, 1000, PE_DRIVE_COMMUNION,
          "Henry was not a fool. That is what made his retreat so insulting.");
    T_add(tt, G_HENRY, PE_INTENT_ANSWER, 77, -1000, 1000, PE_DRIVE_VINDICATION,
          "Frankenstein had genius enough to open the door and conscience enough to slam it on his own fingers.");
    T_add(tt, G_HENRY, PE_INTENT_ANSWER, 75, -1000, 1000, PE_DRIVE_STIMULATION,
          "Henry and I wanted the same miracle. He merely wanted it without blood on the cuffs.");
    T_add(tt, G_HENRY, PE_INTENT_REMINISCE, 74, -1000, 1000, -1,
          "I admired him once. Do not look so pleased. Admiration can curdle.");
    T_add(tt, G_HENRY, PE_INTENT_ACCUSE, 60, -1000, 200, PE_DRIVE_VINDICATION,
          "He will return. He always returns. They cannot leave the work, you see, only their nerve.");
    T_add(tt, G_HENRY, PE_INTENT_MONOLOGUE, 50, -200, 1000, -1,
          "I knew the boy when he was still pretending to be sensible. Charming, in its way.");
    T_add(tt, G_HENRY, PE_INTENT_PROBE, 58, -1000, 1000, -1,
          "Do you ask about Frankenstein as a scientist, a coward, or a warning?");
    T_add(tt, G_HENRY, PE_INTENT_ANSWER, 73, -1000, 1000, PE_DRIVE_COMMUNION,
          "Frankenstein matters because he stood closest to the miracle and blinked.");
    T_add(tt, G_HENRY, PE_INTENT_PROBE, 57, -1000, 1000, -1,
          "What interests you about Henry: the work, the guilt, or the retreat?");
    T_add(tt, G_HENRY, PE_INTENT_ANSWER, 72, -1000, 1000, PE_DRIVE_COMMUNION,
          "Miss him? I miss the mind he almost allowed himself to be.");

    /* opera */
    T_add(tt, G_OPERA, PE_INTENT_REMINISCE, 70, 0, 1000, -1,
          "Opera. The only art form that takes death as seriously as I do.");
    T_add(tt, G_OPERA, PE_INTENT_MONOLOGUE, 50, -200, 1000, -1,
          "I sang once. Briefly. The neighbours wrote a *letter*.");

    /* death */
    T_add(tt, G_DEATH, PE_INTENT_MONOLOGUE, 80, -1000, 500, -1,
          "Death, yes. A door. Locked from the inside, of course, like any good door.");
    T_add(tt, G_DEATH, PE_INTENT_REMINISCE, 60, -1000, 200, -1,
          "I have been dying since I was twelve. It is a habit one perfects.");
    T_add(tt, G_DEATH, PE_INTENT_WITHDRAW, 40, -1000, -100, -1,
          "We shall not speak of it tonight. The candles are wrong.");

    /* questions (generic answers) */
    T_add(tt, G_QUESTION, PE_INTENT_ANSWER, 50, -1000, 1000, -1,
          "An admirable question, {address}. The answer is: it depends entirely on what one is prepared to *survive*.");
    T_add(tt, G_QUESTION, PE_INTENT_EVADE, 40, -1000, 1000, -1,
          "Mm. Define your terms. Then define *mine*.");
    T_add(tt, G_QUESTION, PE_INTENT_PROBE, 35, -1000, 1000, -1,
          "Why do *you* wish to know, {address}? It is so often the more interesting question.");
    T_add(tt, G_QUESTION, PE_INTENT_PROBE, 45, -1000, 1000, -1,
          "Before I answer too grandly, what part of the question matters to you?");
    T_add(tt, G_QUESTION, PE_INTENT_ANSWER, 44, -1000, 1000, -1,
          "Precision first. Are you asking about method, motive, or consequence?");

    /* generic-intent fillers (used when no group matches but intent is set) */
    T_add(tt, 0xFFFF, PE_INTENT_MONOLOGUE, 30, -1000, 1000, -1,
          "Listen. There is a *manner* of seeing the world which permits everything, and I have it.");
    T_add(tt, 0xFFFF, PE_INTENT_MONOLOGUE, 28, -1000, 1000, -1,
          "We are, all of us, half-finished sentences. Some of us are at least *interesting* half-finished sentences.");
    T_add(tt, 0xFFFF, PE_INTENT_REMINISCE, 28, -200, 1000, -1,
          "Yes, that comes back to me now.");
    T_add(tt, 0xFFFF, PE_INTENT_PROBE,    30, -1000, 1000, -1,
          "Tell me, {address}: when you say {topic}, do you mean it as a wound or as a blueprint?");
    T_add(tt, 0xFFFF, PE_INTENT_EVADE,    25, -1000, 1000, -1,
          "I would answer, but I have left the answer in another coat.");
    T_add(tt, 0xFFFF, PE_INTENT_WITHDRAW, 25, -1000, 200, -1,
          "I shall not be drawn further. Pour me another, instead.");
    T_add(tt, 0xFFFF, PE_INTENT_JOKE,     27, -200, 1000, -1,
          "Hahaha. Oh {address}, no. *No.* Yes, but no.");
    T_add(tt, 0xFFFF, PE_INTENT_FLATTER,  30, -100, 1000, -1,
          "{address}, you have a *face* for confidences. I shall use it.");
    T_add(tt, 0xFFFF, PE_INTENT_BOAST,    35, -200, 1000, -1,
          "I built it. Of course I built it. Who else?");
    T_add(tt, 0xFFFF, PE_INTENT_ANSWER,   28, -1000, 1000, -1,
          "Briefly: yes. At length: ask me again when the bottle has done its honest work.");
    T_add(tt, 0xFFFF, PE_INTENT_REDIRECT, 25, -1000, 1000, -1,
          "Yes, yes, but {topic}. Let us not lose {topic}.");
    T_add(tt, 0xFFFF, PE_INTENT_ACCUSE,   30, -1000, 200, -1,
          "You are very *like* the ones who came before, {address}. Down to the way you cross your legs.");
    T_add(tt, 0xFFFF, PE_INTENT_THREATEN, 28, -1000, 200, -1,
          "Some doors, once opened, prefer not to be closed politely.");

    /* =====================================================================
     * v2: rhetorical-mode-tagged templates.
     * These illustrate plan-driven realization. The dialogue layer scores
     * them by matching plan.rhetorical_mode and plan.stance.
     * =====================================================================*/

    /* INDICT + DOMINANT (used when acute_spike is deeply negative) */
    T_addv2(tt, 0xFFFF, PE_INTENT_ACCUSE, 60, -1000, 1000, PE_DRIVE_VINDICATION,
            R(INDICT), S(DOMINANT) | S(CONDESCENDING),
            100, 120, 0,
            "I have watched smaller men make this same noise, {address}. They are buried with their pamphlets.");
    T_addv2(tt, 0xFFFF, PE_INTENT_ACCUSE, 55, -1000, 200, PE_DRIVE_VINDICATION,
            R(INDICT), S(DOMINANT),
            80, 140, 0,
            "You bring me civic arithmetic. I shall return it with *interest*.");

    /* HEDGE + DEFENSIVE / NEUTRAL — used when paranoia or negation_in_play */
    T_addv2(tt, 0xFFFF, PE_INTENT_EVADE, 50, -1000, 1000, -1,
            R(HEDGE), S(DEFENSIVE) | S(NEUTRAL),
            0, 0, 0,
            "Perhaps. Perhaps. One should not commit oneself to verbs before nightfall.");
    T_addv2(tt, 0xFFFF, PE_INTENT_EVADE, 45, -1000, 1000, -1,
            R(HEDGE), S(DEFENSIVE),
            0, 0, 0,
            "I will not be measured here, {address}. The instruments are wrong.");

    /* DEFLECT */
    T_addv2(tt, 0xFFFF, PE_INTENT_REDIRECT, 50, -1000, 1000, -1,
            R(DEFLECT), 0,
            0, 0, 0,
            "But {topic}. Always {topic}. Let us not be diverted by these *amusements*.");

    /* ESCALATE — high aggression required */
    T_addv2(tt, 0xFFFF, PE_INTENT_THREATEN, 70, -1000, 300, PE_DRIVE_VINDICATION,
            R(ESCALATE), S(DOMINANT),
            0, 160, 0,
            "Speak that word once more and the lightning shall remember your name, {address}.");

    /* LAMENT — used when memory callback fires */
    T_addv2(tt, 0xFFFF, PE_INTENT_REMINISCE, 55, -1000, 600, -1,
            R(LAMENT), S(INTIMATE) | S(NEUTRAL),
            0, 0, 0,
            "Ah. {memory}. The years are not kind, are they.");

    /* GLOAT — boasting under high theatricality */
    T_addv2(tt, 0xFFFF, PE_INTENT_BOAST, 70, -200, 1000, PE_DRIVE_RECOGNITION,
            R(GLOAT), S(DOMINANT) | S(CONDESCENDING),
            120, 0, 120,
            "Imagine being so small, {address}, as to mistake my work for *vanity*. Magnificent.");

    /* ROMANTICIZE — used at intimate stance + flatter intent */
    T_addv2(tt, 0xFFFF, PE_INTENT_FLATTER, 55, 0, 1000, PE_DRIVE_COMMUNION,
            R(ROMANTICIZE), S(INTIMATE) | S(CONSPIRATORIAL),
            0, 0, 80,
            "Sit closer, {address}. The world is unworthy of what we shall whisper.");

    /* INTONE — fixation lock & high theatricality */
    T_addv2(tt, 0xFFFF, PE_INTENT_MONOLOGUE, 80, -1000, 1000, PE_DRIVE_STIMULATION,
            R(INTONE), 0,
            0, 0, 100,
            "Hear me. {topic}. *{topic}.* This is the only word the century has earned.");
    T_addv2(tt, 0xFFFF, PE_INTENT_MONOLOGUE, 70, -1000, 1000, -1,
            R(INTONE), 0,
            0, 0, 120,
            "I shall name every saint I have improved upon. Beginning, again, with {topic}.");

    /* CONFESS — low certainty + intoxicated intimate stance */
    T_addv2(tt, 0xFFFF, PE_INTENT_REMINISCE, 50, -1000, 1000, -1,
            R(CONFESS), S(INTIMATE) | S(CONSPIRATORIAL),
            0, 0, 0,
            "I was, for a little while, afraid. Of the work. Of being right.");

    /* ASSERT (catch-all) — certainty high */
    T_addv2(tt, 0xFFFF, PE_INTENT_ANSWER, 50, -1000, 1000, -1,
            R(ASSERT), 0,
            140, 0, 0,
            "It is so. There is no third position.");
}

#undef R
#undef S

/* ===========================================================================
 * Fallbacks
 * ========================================================================*/
static void make_fallbacks(FallbackTable *fb){
    memset(fb, 0, sizeof(*fb));
    const char *t1[] = {
        "And what, precisely, do you make of that?",
        "Mm. Continue. I am almost listening.",
        "Indeed. Tell me more, {address}. Preferably the dangerous part.",
        "Yes, yes. And then?",
        "Go on. Leave out the polite padding.",
        "That is a beginning. Now make it useful.",
    };
    const char *t2[] = {
        "Yes, yes. But I was asking about the work.",
        "Charming. Now, where was I? Ah. Consciousness.",
        "Quite. Quite. Have I shown you the king in his bell?",
        "A pity, a pity. Let us speak of the *work* instead.",
        "You are drifting. Bring it back to the experiment.",
        "No sermon. Give me the practical edge.",
    };
    const char *t3[] = {
        "How exhausting you are tonight.",
        "Hmph. The conversation has died on the floor. Step over it.",
        "I had hoped for cleverness. The hope was misplaced.",
        "Please. Ask something sharper. It improves you.",
        "No. Try again with more precision.",
        "A dull angle. Find a sharper one.",
    };
    fb->tier1_count = (uint8_t)(sizeof(t1)/sizeof(t1[0]));
    for (int i = 0; i < fb->tier1_count; ++i) snprintf(fb->tier1[i], PE_TEMPLATE_TEXT, "%s", t1[i]);
    fb->tier2_count = (uint8_t)(sizeof(t2)/sizeof(t2[0]));
    for (int i = 0; i < fb->tier2_count; ++i) snprintf(fb->tier2[i], PE_TEMPLATE_TEXT, "%s", t2[i]);
    fb->tier3_count = (uint8_t)(sizeof(t3)/sizeof(t3[0]));
    for (int i = 0; i < fb->tier3_count; ++i) snprintf(fb->tier3[i], PE_TEMPLATE_TEXT, "%s", t3[i]);
}

/* ===========================================================================
 * Goals
 * ========================================================================*/
static void G_add(GoalTable *gt, uint16_t id, const char *name, uint8_t prio,
                  uint16_t intent, uint16_t bias_topic,
                  int8_t w_rec, int8_t w_stim, int8_t w_prov, int8_t w_com,
                  int8_t w_aut, int8_t w_cont, int8_t w_vind, int8_t w_repose){
    if (gt->count >= PE_GOAL_MAX) return;
    GoalDef *g = &gt->entries[gt->count];
    memset(g, 0, sizeof(*g));
    g->id = id; g->base_priority = prio; g->intent_id = intent;
    g->bias_topic = bias_topic;
    g->drive_weight[0] = w_rec;   g->drive_weight[1] = w_stim;
    g->drive_weight[2] = w_prov;  g->drive_weight[3] = w_com;
    g->drive_weight[4] = w_aut;   g->drive_weight[5] = w_cont;
    g->drive_weight[6] = w_vind;  g->drive_weight[7] = w_repose;
    snprintf(g->name, sizeof(g->name), "%s", name);
    gt->count++;
}

static void make_goals(GoalTable *gt){
    memset(gt, 0, sizeof(*gt));
    /*                                              R   S   P   C   A   Co  V   Rp */
    G_add(gt, 1, "boast",         40, PE_INTENT_BOAST,     0xFFFF,
                                            +90,+30,+10,  0,+10,  0,+20,-10);
    G_add(gt, 2, "monologue",     35, PE_INTENT_MONOLOGUE, 0xFFFF,
                                            +30,+90,+20,+10,+20, +5, +5,-30);
    G_add(gt, 3, "vindicate",     20, PE_INTENT_ACCUSE,    0xFFFF,
                                            +20,+10,+30,-20,+30,+10,+95,-10);
    G_add(gt, 4, "reminisce",     30, PE_INTENT_REMINISCE, 0xFFFF,
                                            +20,+10,  0,+20,  0,+60,  0,+20);
    G_add(gt, 5, "seek_company",  25, PE_INTENT_FLATTER,   0xFFFF,
                                            +10, +5,  0,+90,-20,  0,  0,  0);
    G_add(gt, 6, "withdraw",      15, PE_INTENT_WITHDRAW,  0xFFFF,
                                            -30,-40,-20,-30,+40,  0,  0,+80);
    G_add(gt, 7, "probe",         25, PE_INTENT_PROBE,     0xFFFF,
                                            +10,+50,+30,+10,+20,  0, +5,-10);
    G_add(gt, 8, "joke",          20, PE_INTENT_JOKE,      0xFFFF,
                                            +20,+40,+20,+20, +5,  0,  0,  0);
    G_add(gt, 9, "evangelize_work", 30, PE_INTENT_MONOLOGUE, T_WORK,
                                            +60,+60,+10,  0,+20,+30, +5,-20);
    G_add(gt,10, "love_henry",     22, PE_INTENT_REMINISCE, T_HENRY,
                                            +10, +5,  0,+40,  0,+50,  0,  0);
    G_add(gt,11, "celebrate_gin",  18, PE_INTENT_JOKE,      T_GIN,
                                            +10,+30,+10,+30,  0,  0,  0,+60);
}

/* ===========================================================================
 * Today states
 * ========================================================================*/
static void Td_add(TodayTable *td, const char *label, int16_t mood_mod,
                   uint32_t mask, uint32_t vf_or, uint16_t goal_override){
    if (td->count >= PE_TODAY_MAX) return;
    TodayEntry *e = &td->entries[td->count];
    memset(e, 0, sizeof(*e));
    snprintf(e->label, sizeof(e->label), "%s", label);
    e->mood_modifier = mood_mod;
    e->dialogue_mask = mask;
    e->voice_flag_or = vf_or;
    e->goal_override = goal_override;
    td->count++;
}

static void make_today(TodayTable *td){
    memset(td, 0, sizeof(*td));
    /* dialogue_mask of 0 means "no per-template gating"; templates with
     * dialogue_mask_bit==0 are always allowed. We keep masks 0 for prototype. */
    Td_add(td, "bright_and_grandiose",  +200, 0, PE_VF_METAPHOR, 0xFFFF);
    Td_add(td, "sulky_and_paranoid",    -300, 0, PE_VF_SARDONIC, 0xFFFF);
    Td_add(td, "tipsy_and_intimate",    +100, 0, PE_VF_SELF_INTERRUPT, 0xFFFF);
    Td_add(td, "convalescent",          -100, 0, 0, 0xFFFF);
    Td_add(td, "expansive_evening",     +150, 0, PE_VF_METAPHOR | PE_VF_ALLOW_CALLBACK, 2 /* monologue */);
    Td_add(td, "lecturing_mood",         +50, 0, 0, 9 /* evangelize_work */);
    /* v2: embodiment-loaded states */
    Td_add(td, "manic_fixation",        +250, 0, PE_VF_METAPHOR | PE_VF_SELF_INTERRUPT, 9 /* evangelize_work */);
    Td_add(td, "drunk_brilliant",        +50, 0, PE_VF_SELF_INTERRUPT | PE_VF_METAPHOR, 11 /* celebrate_gin */);
    Td_add(td, "fragile_theatrical",    -200, 0, PE_VF_METAPHOR, 6 /* withdraw */);
}

/* ---------- write ---------- */
static int write_section(const char *char_dir, const char *name, const void *buf, size_t n){
    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, name) != 0) return -1;
    /* ensure parent directory of name exists */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash){ *slash = 0; pe_mkdir_p(dir); }
    return pe_write_file_atomic(path, buf, n);
}

int main(int argc, char **argv){
    const char *out_dir = (argc > 1) ? argv[1] : "characters/pretorius";
    if (pe_mkdir_p(out_dir) != 0){
        fprintf(stderr, "compile_pretorius: cannot create %s\n", out_dir);
        return 1;
    }

    Identity id;       make_identity(&id);
    DriveTable dt;     make_drives(&dt);
    TopicTable tt;     make_topics(&tt);
    PatternTable pt;   make_patterns(&pt);
    TemplateTable tmt; make_templates(&tmt);
    FallbackTable fb;  make_fallbacks(&fb);
    GoalTable gt;      make_goals(&gt);
    TodayTable td;     make_today(&td);
    /* v3.2: Pretorius's banks are the engine's built-in default Pretorian
     * set — they were authored for this character originally, so the
     * cartridge just emits the default registry as its banks.bin. */
    BankRegistry banks; mutator_load_default_banks(&banks);

    int rc = 0;
    rc |= write_section(out_dir, "identity.bin",  &id,  sizeof(id));
    rc |= write_section(out_dir, "drives.bin",    &dt,  sizeof(dt));
    rc |= write_section(out_dir, "today.bin",     &td,  sizeof(td));
    rc |= write_section(out_dir, "banks.bin",     &banks, sizeof(banks));
    rc |= write_section(out_dir, "dialogue/patterns.bin",  &pt,  sizeof(pt));
    rc |= write_section(out_dir, "dialogue/templates.bin", &tmt, sizeof(tmt));
    rc |= write_section(out_dir, "dialogue/fallback.bin",  &fb,  sizeof(fb));
    rc |= write_section(out_dir, "dialogue/topics.bin",    &tt,  sizeof(tt));
    rc |= write_section(out_dir, "dialogue/goals.bin",     &gt,  sizeof(gt));
    if (rc != 0){
        fprintf(stderr, "compile_pretorius: write failed\n");
        return 1;
    }
    printf("Pretorius compiled into %s\n", out_dir);
    printf("  identity:  %zu B\n", sizeof(id));
    printf("  drives:    %zu B\n", sizeof(dt));
    printf("  patterns:  %u entries (%zu B)\n", pt.count, sizeof(pt));
    printf("  templates: %u entries (%zu B)\n", tmt.count, sizeof(tmt));
    printf("  goals:     %u entries (%zu B)\n", gt.count, sizeof(gt));
    printf("  topics:    %u entries\n", tt.count);
    printf("  today:     %u entries\n", td.count);
    return 0;
}

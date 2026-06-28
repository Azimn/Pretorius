/* persona.h — Persona Engine public API (PE-SPEC-001).
 * Deterministic, no-malloc-in-loop, little-endian binary file backed.
 */
#ifndef PERSONA_H
#define PERSONA_H

#include <stdint.h>
#include <stddef.h>
#include "mutator.h"   /* BankRegistry (cartridge-borne synonym banks) */
#include "environment.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- compile-time limits ---------- */
#ifdef PE_MICRO_MODE
#define PE_MAX_ACTORS          4
#define PE_MAX_MEMORY_SLOTS    50
#define PE_MAX_OPEN_LOOPS      16
#define PE_MAX_SPEECH_EVENTS   128
#define PE_MAX_TEMPLATES       1024
#define PE_MAX_PATTERNS        256
#else
#define PE_MAX_ACTORS          16
#define PE_MAX_MEMORY_SLOTS    50
#define PE_MAX_OPEN_LOOPS      64
#define PE_MAX_SPEECH_EVENTS   512
#define PE_MAX_TEMPLATES       1024
#define PE_MAX_PATTERNS        256
#endif

#define PE_DRIVE_COUNT          8
#define PE_OBSESSION_COUNT      8
#define PE_TABOO_COUNT          8
#define PE_ADDRESS_COUNT        4
#define PE_ADDRESS_LEN          16
/* Modern low-resource cartridge capacity.  These limits intentionally exceed
 * the original P3-era authoring budget so characters can feel less repetitive
 * on ordinary modern hardware while remaining deterministic, offline, and
 * tiny compared with any mandatory LLM runtime. */
#define PE_CORE_SEED_MAX        20
#define PE_EPISODIC_MAX         PE_MAX_MEMORY_SLOTS
#define PE_SEMANTIC_USERS       PE_MAX_ACTORS
#define PE_SEMANTIC_FACTS       30
#define PE_FACT_LEN             64
#define PE_SHORT_TERM_LEN       10
#define PE_SHORT_TERM_TEXT      128
#define PE_MEM_SUMMARY_LEN      96
#define PE_TOPIC_SLOTS          32
#define PE_PHRASE_USAGE         256
#define PE_GOAL_MAX             24
#define PE_TODAY_MAX            16
#define PE_TEMPLATE_MAX         PE_MAX_TEMPLATES
#define PE_PATTERN_MAX          PE_MAX_PATTERNS
#define PE_PATTERN_KW_LEN       32
#define PE_TEMPLATE_TEXT        256
#define PE_TOPIC_MAX            64
#define PE_TOPIC_NAME           16
#define PE_FALLBACK_PER_TIER    9
#define PE_FALLBACK_TIERS       3
#define PE_RELATION_EVENTS      10
#define PE_TODAY_LABEL          32
#define PE_NAME_LEN             32
#define PE_TRACE_LEN            64
#define PE_PLAN_MAX_MODES       4   /* templates list up to N compatible rhetorical modes */
#define PE_CHAPTER_MAX          16  /* v3.1: max autobiographical chapters */
#define PE_CHAPTER_PHRASE       32  /* v3.1: key phrase length per chapter */
#define PE_FLOURISH_COUNT       4   /* per-character metaphor injection bank */
#define PE_FLOURISH_LEN         48
#define PE_EXPANSION_COUNT      4   /* per-character verbosity expansion bank */
#define PE_EXPANSION_LEN        48
#define PE_COLD_SCRATCH_MAX     5   /* v3.2: per-turn AETHER promotion buffer */
#define PE_ACTIVE_MAX           (PE_EPISODIC_MAX + PE_COLD_SCRATCH_MAX)
                                    /* v3.2: active_memories[] holds working +
                                     * cold candidates in one ranked list */
#define PE_PREOCCUPATION_COUNT  3
#define PE_PREOCCUPATION_LEN    96
#define PE_RESUMPTION_BUCKETS   4
#define PE_RESUMPTION_LEN       192
#define PE_WANT_COUNT           3
#define PE_WANT_NAME_LEN        32
#define PE_MILESTONE_COUNT      6
#define PE_MILESTONE_LEN        96
#define PE_COLD_OPEN_CASES      4
#define PE_COLD_OPEN_VARIANTS   4
#define PE_COLD_OPEN_LEN        128

/* V5 Phase 2: per-slot actor tagging for episodic memory.
 * Included after PE_EPISODIC_MAX is defined; the header depends on it. */
#include "actor_index.h"
/* V6 Phase 3: engine-authored speech events (the self-ledger). */
#include "speech_ledger.h"
/* V6 Phase 4: multi-dimensional Relation. */
#include "relation_dims.h"
/* V6 Phase 5b: typed dissonance accumulators (Higgins ideal/ought/feared). */
#include "dissonance.h"
/* V6 Phase 6: open loops / carried intentions. */
#include "open_loops.h"
/* V6 Phase 6: lightweight conversation rhythm habits. */
#include "speech_habits.h"
/* V6: compact learned-knowledge graph sidecar. */
#include "learned_knowledge.h"
/* V6: inferred actor mind model. */
#include "theory_of_mind.h"
/* V6: slow earned baseline drift. */
#include "long_arc_drift.h"

/* V6 Phase 5d: recall modes — per V6_DOCTRINE §16, the same memory
 * store supports many retrieval intents; the planner selects one based
 * on current state and the chosen mode shifts scoring (never the
 * memories themselves). */
enum {
    PE_RECALL_ACCURATE         = 0,
    PE_RECALL_DEFENSIVE        = 1,
    PE_RECALL_NOSTALGIC        = 2,
    PE_RECALL_ACCUSATORY       = 3,
    PE_RECALL_SHAME_AVOIDANT   = 4,
    PE_RECALL_INTIMACY_SEEKING = 5,
    PE_RECALL_OBSESSION_DRIVEN = 6,
    PE_RECALL_MOOD_CONGRUENT   = 7,
    PE_RECALL_COUNT
};
const char *pe_recall_mode_name(uint8_t m);

/* ---------- pattern flags (Pattern.flags bitmask) ---------- */
#define PE_PATTERN_FLAG_INTOXICANT (1u<<0)  /* matching this pattern raises intoxication */

enum {
    MEM_CORE = 0,
    MEM_EPISODIC = 1,
    MEM_CACHE = 2
};

enum {
    PE_MEM_FLAG_USER_PINNED = 1u << 0,
    PE_MEM_FLAG_IMPORTED    = 1u << 1
};

/* ---------- drives (id matches array slot) ---------- */
enum {
    PE_DRIVE_RECOGNITION = 0,
    PE_DRIVE_STIMULATION,
    PE_DRIVE_PROVOCATION,
    PE_DRIVE_COMMUNION,
    PE_DRIVE_AUTONOMY,
    PE_DRIVE_CONTINUITY,
    PE_DRIVE_VINDICATION,
    PE_DRIVE_REPOSE
};

/* ---------- intents (fixed enum) ---------- */
enum {
    PE_INTENT_ANSWER = 0,
    PE_INTENT_EVADE,
    PE_INTENT_ACCUSE,
    PE_INTENT_FLATTER,
    PE_INTENT_THREATEN,
    PE_INTENT_PROBE,
    PE_INTENT_REDIRECT,
    PE_INTENT_MONOLOGUE,
    PE_INTENT_REMINISCE,
    PE_INTENT_WITHDRAW,
    PE_INTENT_JOKE,
    PE_INTENT_BOAST,
    PE_INTENT_INITIATE,    /* V5: character takes the conversational lead */
    PE_INTENT_ATTEND,      /* V5: active listening */
    PE_INTENT_CLARIFY,     /* V5: ambiguity probe */
    PE_INTENT_PAUSE,       /* V5: strategic silence */
    PE_INTENT_COUNT
};

/* ---------- rhetorical modes (UtterancePlan.rhetorical_mode) ---------- */
enum {
    PE_RHET_ASSERT = 0,
    PE_RHET_HEDGE,
    PE_RHET_DEFLECT,
    PE_RHET_ESCALATE,
    PE_RHET_LAMENT,
    PE_RHET_GLOAT,
    PE_RHET_INDICT,
    PE_RHET_ROMANTICIZE,
    PE_RHET_INTONE,         /* theatrical, prophetic */
    PE_RHET_CONFESS,
    PE_RHET_COUNT
};

/* ---------- stance (UtterancePlan.stance) ---------- */
enum {
    PE_STANCE_NEUTRAL = 0,
    PE_STANCE_DOMINANT,
    PE_STANCE_INTIMATE,
    PE_STANCE_DEFENSIVE,
    PE_STANCE_CONDESCENDING,
    PE_STANCE_CONSPIRATORIAL,
    PE_STANCE_COUNT
};

/* ---------- voice flags ---------- */
#define PE_VF_NO_DIRECT_AFFIRM   (1u<<0)
#define PE_VF_ABSTRACT           (1u<<1)
#define PE_VF_SARDONIC           (1u<<2)
#define PE_VF_METAPHOR           (1u<<3)
#define PE_VF_SELF_INTERRUPT     (1u<<4)
#define PE_VF_VERBOSITY_MASK     (7u<<5)   /* bits 5-7 */
#define PE_VF_GET_VERBOSITY(v)   (((v)>>5)&7u)
#define PE_VF_ALLOW_BLEED        (1u<<8)
#define PE_VF_ALLOW_CALLBACK     (1u<<9)
#define PE_VF_ALLOW_CONTRADICT   (1u<<10)
#define PE_VF_DELAY_TIMING       (1u<<11)
#define PE_VF_VOICES_TOM_GUESSES (1u<<12)

/* ---------- reserved baseline dialogue groups ---------- */
#define PE_BL_GROUP_BASE       0x7000u
#define PE_BL_GROUP_PRAISE     (PE_BL_GROUP_BASE + 1u)
#define PE_BL_GROUP_INSULT     (PE_BL_GROUP_BASE + 2u)
#define PE_BL_GROUP_THREAT     (PE_BL_GROUP_BASE + 3u)
#define PE_BL_GROUP_INTIMACY   (PE_BL_GROUP_BASE + 4u)
#define PE_BL_GROUP_GREETING   (PE_BL_GROUP_BASE + 5u)
#define PE_BL_GROUP_WHO        (PE_BL_GROUP_BASE + 6u)
#define PE_BL_GROUP_QUESTION   (PE_BL_GROUP_BASE + 7u)
#define PE_BL_GROUP_STATUS     (PE_BL_GROUP_BASE + 8u)
#define PE_BL_GROUP_ACK        (PE_BL_GROUP_BASE + 9u)
#define PE_BL_GROUP_APOLOGY    (PE_BL_GROUP_BASE + 10u)
#define PE_BL_GROUP_GOODBYE    (PE_BL_GROUP_BASE + 11u)

#define PE_TEMPLATE_SRC_CARTRIDGE 0u
#define PE_TEMPLATE_SRC_BASELINE  1u

/* ---------- relation tags ---------- */
#define PE_TAG_STRANGER          (1u<<0)
#define PE_TAG_CURIOUS           (1u<<1)
#define PE_TAG_CONFIDANT         (1u<<2)
#define PE_TAG_RIVAL             (1u<<3)
#define PE_TAG_BENEATH_CONTEMPT  (1u<<4)

/* ---------- fixed structs (mirror on-disk layout) ---------- */
#pragma pack(push, 1)

typedef struct {
    int8_t valence;    /* -100..100 */
    int8_t arousal;    /* 0..100    */
    int8_t dominance;  /* -100..100 */
    int8_t _pad;
} EmotionVector;

typedef struct {
    uint32_t id;
    uint16_t type;
    uint8_t  salience;
    EmotionVector emotion;
    uint32_t timestamp;
    uint8_t  core_memory;
    uint8_t  decay_counter;
    uint16_t topic_id;
    uint8_t  private_threshold;  /* v3.1: disclosure gate (0=public; higher=more private) */
    uint8_t  retrieval_prob;     /* v3.3: probabilistic recall strength, 0..255 */
    uint8_t  memory_type;        /* MEM_CORE / MEM_EPISODIC / MEM_CACHE */
    uint8_t  flags;              /* PE_MEM_FLAG_*; replaces former padding byte */
    char     summary[PE_MEM_SUMMARY_LEN];
    /* v3.0: 64-bit SimHash of the summary, computed at commit time.
     * Lets pe_associative_recall do fuzzy semantic match via Hamming
     * distance — complements (does not replace) topic-tag matching. */
    uint64_t lsh_sig;
} MemoryNode;

typedef struct {
    char     name[PE_WANT_NAME_LEN];
    uint16_t target_topic_id;
    uint8_t  target_pattern_class;
    uint8_t  intensity;
    uint16_t _pad;
} CharacterWant;

typedef struct {
    uint16_t openness, conscientiousness, extraversion, agreeableness, neuroticism;
    uint16_t _pad0;
    uint32_t voice_flags;
    uint16_t obsessions[PE_OBSESSION_COUNT];
    uint16_t taboos[PE_TABOO_COUNT];
    uint8_t  obsession_strength[PE_OBSESSION_COUNT]; /* 0 = default 50 */
    uint8_t  _pad_obs[PE_OBSESSION_COUNT];
    char     address_user_as[PE_ADDRESS_COUNT][PE_ADDRESS_LEN];
    char     character_name[PE_NAME_LEN];
    uint8_t  core_memory_count;
    uint8_t  _pad1[7];
    MemoryNode core_memories_seed[PE_CORE_SEED_MAX];
    /* per-character style banks — fired by apply_style under voice_flags + plan.
     * Empty slot (first byte 0) is treated as "skip" so cartridges can opt out. */
    char flourishes[PE_FLOURISH_COUNT][PE_FLOURISH_LEN];  /* PE_VF_METAPHOR injection */
    char expansions[PE_EXPANSION_COUNT][PE_EXPANSION_LEN]; /* verbosity injection */
    char current_preoccupations[PE_PREOCCUPATION_COUNT][PE_PREOCCUPATION_LEN];
    char resumption_lines[PE_RESUMPTION_BUCKETS][PE_RESUMPTION_LEN];
    CharacterWant wants[PE_WANT_COUNT];
    char milestone_lines[PE_MILESTONE_COUNT][PE_MILESTONE_LEN];
    uint16_t milestone_days[PE_MILESTONE_COUNT];
    uint16_t _v5_pad[2];
    /* V6: cartridge-authored phrasing for memory-owned cold opens.
     * Cases: 0=name+topic, 1=topic, 2=name/no topic, 3=no name/no topic.
     * Empty strings fall back to generic engine wording. */
    char cold_open_memory_templates[PE_COLD_OPEN_CASES]
                                   [PE_COLD_OPEN_VARIANTS]
                                   [PE_COLD_OPEN_LEN];
    uint16_t suggestibility;             /* 0..1000: how readily ToM beliefs move */
    uint16_t contagion_susceptibility;   /* 0..1000: affect contagion pull */
    uint16_t forecast_horizon_weight;    /* 0..1000: past topic affect bias */
    uint16_t expression_mask_threshold;  /* trust below this masks expression */
    uint16_t drift_malleability;         /* 0..1000: earned long-arc baseline drift bound */
    uint16_t _pad_mind2;
} Identity;

typedef struct {
    uint8_t  id;
    int8_t   mood_weight;                  /* contribution per (drive_value-baseline) to mood */
    char     name[16];
    int16_t  decay_per_minute;             /* signed, sub-millihz; >0 falls toward 0 */
    int16_t  personality_weight[5];        /* O,C,E,A,N — 0x1000 = 1.0 */
    int16_t  baseline;                     /* equilibrium target (decay tugs toward this) */
    int16_t  _pad;
} DriveDef;

typedef struct {
    DriveDef drives[PE_DRIVE_COUNT];
} DriveTable;

typedef struct {
    uint16_t topic_id;
    uint16_t momentum;
} TopicState;

/* Trace ring entry — recorded every process_input for instrumentation. */
typedef struct {
    uint32_t turn;
    int16_t  mood;
    int16_t  acute_spike;
    int16_t  intoxication;
    int16_t  exhaustion;
    int16_t  irritation_carry;
    int16_t  obsession_pressure;
    uint16_t goal;
    uint16_t intent;
    uint16_t rhetorical_mode;
    uint16_t primary_topic;
    uint16_t fixation_topic;
    uint8_t  input_class;
    uint8_t  negation_flag;
    uint8_t  _pad[2];
} TraceEntry;

typedef struct {
    int16_t  drive_values[PE_DRIVE_COUNT];
    int16_t  mood;
    int16_t  fatigue;
    int16_t  trust_user;
    int16_t  paranoia;
    uint16_t current_goal;
    uint16_t current_intent;
    uint16_t goal_hysteresis;              /* turns remaining before goal may flip */
    uint16_t today_index;
    uint32_t today_seed;
    uint32_t session_start_time;
    uint32_t last_update_time;
    uint32_t user_id_hash;
    uint32_t turn_count;
    EmotionVector last_input_emotion;
    EmotionVector prev_input_emotion;
    uint8_t    last_matched_flags;        /* bitmask of PE_PATTERN_FLAG_* from this turn */
    uint8_t    _pad_lmf[3];               /* keep topic_momentum aligned */
    TopicState topic_momentum[PE_TOPIC_SLOTS];
    uint32_t rng_state;

    /* ---- v2: embodiment ---- */
    int16_t  intoxication;          /* 0..1000; rises on PE_PATTERN_FLAG_INTOXICANT match; decays slowly */
    int16_t  exhaustion;            /* 0..1000; rises per turn; drained by repose */
    int16_t  irritation_carry;      /* 0..1000; carries between turns after negatives */
    int16_t  physical_fragility;    /* 0..1000; slow climb across session */
    uint16_t fixation_topic;        /* topic_id locked, 0xFFFF if none */
    int16_t  fixation_strength;     /* 0..1000 */
    int16_t  fixation_remaining;    /* turns left in current fixation lock */
    int16_t  recovery_curve;        /* counts down to release of acute states */

    /* ---- v2: layered affect ---- */
    int16_t  baseline_temperament;  /* identity-derived constant (set on session start) */
    int16_t  acute_spike;           /* -1000..1000; rapidly decaying recent shock */
    int16_t  suppression_mask;      /* 0..1000; dampens external display of mood */
    int16_t  obsession_pressure;    /* 0..1000; rises when obsession topics starved */

    /* ---- v2: last computed rhetorical plan (for inspection) ---- */
    uint16_t last_rhetorical_mode;
    uint16_t last_stance;
    uint16_t last_target_topic;
    uint16_t last_callback_memory;
    uint8_t  last_certainty;
    uint8_t  last_verbosity;
    uint8_t  last_aggression;
    uint8_t  last_theatricality;
    uint8_t  last_hedging;
    uint8_t  last_negation;

    /* ---- v2: trace ring ---- */
    TraceEntry trace[PE_TRACE_LEN];
    uint8_t    trace_pos;
    uint8_t    _pad_v2[3];

    /* ---- v3.0: predictive coding / surprise ----
     * At end of each turn we predict what the next input will look like.
     * At the start of the following turn we compare actual vs. predicted
     * and feed the residual back into mood / acute_spike / commit salience. */
    uint8_t  predicted_input_class;     /* 0..6 expected next input class */
    int8_t   predicted_input_valence;   /* -128..127 expected valence */
    uint16_t surprise_last;             /* 0..1000, |predicted - actual| metric */
    uint16_t prediction_error_accum;    /* 0..2000, slow-decaying running surprise */
    uint16_t _pad_v3;

    /* ---- v3.2: The Voice — counterfactual rerank instrumentation ---- */
    int16_t  last_voice_delta;          /* score bump applied to best-aligned candidate */
    uint16_t last_voice_choice;         /* template id the Voice would have nudged toward */

    /* ---- v5: proactive presence runtime ---- */
    uint8_t  neutral_streak;
    uint8_t  _pad_v5a;
    uint16_t turns_since_unprompted_recall;
    uint16_t unresolved_threads[8];
    uint8_t  unresolved_count;
    uint8_t  unresolved_head;
    uint8_t  milestones_seen;
    uint8_t  _pad_v5b;
    uint16_t want_turns_since_engaged[PE_WANT_COUNT];
    char     resumption_pending[PE_RESUMPTION_LEN];
    uint8_t  turns_since_question;
    uint8_t  last_reply_had_question;
} NPCState;

typedef struct {
    uint32_t id;
    char     name[PE_TOPIC_NAME];
    uint16_t adjacents[6];                 /* 0xFFFF terminator */
    uint16_t _pad;
} TopicDef;

typedef struct {
    uint32_t count;
    TopicDef topics[PE_TOPIC_MAX];
} TopicTable;

typedef struct {
    char     keyword[PE_PATTERN_KW_LEN];   /* lowercased */
    uint16_t topic_id;                     /* 0xFFFF if none */
    int8_t   delta_valence;
    int8_t   delta_arousal;
    int8_t   delta_dominance;
    int8_t   input_class;                  /* 0=neutral 1=praise 2=insult 3=question 4=threat 5=intimacy */
    uint8_t  flags;                        /* PE_PATTERN_FLAG_* bitmask — data-driven side effects */
    uint16_t template_group;               /* 0xFFFF if none */
    uint8_t  kw_len;                       /* precomputed strlen(keyword) — v2 perf */
    uint8_t  first_char;                   /* keyword[0] — for first-char bitmap filter */
} Pattern;

typedef struct {
    uint32_t count;
    Pattern  entries[PE_PATTERN_MAX];
} PatternTable;

typedef struct {
    uint16_t id;
    uint16_t group;                        /* which pattern group this template serves */
    uint8_t  intent;
    uint8_t  required_voice_flags;         /* must all be set in identity for use */
    int16_t  mood_min, mood_max;
    int16_t  drive_bias_id;                /* drive that boosts score, -1 if none */
    int16_t  base_score;
    uint32_t dialogue_mask_bit;            /* AND with today mask; 0 = always allowed */
    /* v2: rhetorical compatibility */
    uint16_t rhetorical_mask;              /* bitmask of compatible PE_RHET_* */
    uint16_t stance_mask;                  /* bitmask of compatible PE_STANCE_* */
    uint8_t  min_certainty;                /* 0..255; template needs >= plan.certainty */
    uint8_t  min_aggression;
    uint8_t  min_theatricality;
    uint8_t  source;                       /* PE_TEMPLATE_SRC_* */
    char     text[PE_TEMPLATE_TEXT];
} Template;

typedef struct {
    uint32_t count;
    Template entries[PE_TEMPLATE_MAX];
} TemplateTable;

typedef struct {
    char tier1[PE_FALLBACK_PER_TIER][PE_TEMPLATE_TEXT];
    char tier2[PE_FALLBACK_PER_TIER][PE_TEMPLATE_TEXT];
    char tier3[PE_FALLBACK_PER_TIER][PE_TEMPLATE_TEXT];
    uint8_t tier1_count, tier2_count, tier3_count, _pad;
} FallbackTable;

typedef struct {
    uint16_t id;
    uint8_t  base_priority;
    int8_t   drive_weight[PE_DRIVE_COUNT]; /* -128..127 */
    uint16_t intent_id;
    uint16_t bias_topic;                   /* 0xFFFF if none */
    char     name[16];
} GoalDef;

typedef struct {
    uint32_t count;
    GoalDef  entries[PE_GOAL_MAX];
} GoalTable;

typedef struct {
    char     label[PE_TODAY_LABEL];
    int16_t  mood_modifier;
    int16_t  drive_modifiers[PE_DRIVE_COUNT];
    uint16_t goal_override;                /* 0xFFFF = none */
    uint32_t dialogue_mask;                /* AND'd into template gating */
    uint32_t voice_flag_or;                /* OR'd into voice_flags for the day */
} TodayEntry;

typedef struct {
    uint32_t count;
    TodayEntry entries[PE_TODAY_MAX];
} TodayTable;

typedef struct {
    uint32_t user_hash;
    int16_t  disposition;                  /* 0..1000 */
    uint8_t  tags;
    uint8_t  _pad0;
    uint32_t last_contact;
    uint32_t first_contact;
    uint8_t  memorable_events[PE_RELATION_EVENTS];
    uint8_t  _pad1[6];
    char     known_as[PE_NAME_LEN];

    /* ---- v3.0: Theory-of-Mind (UserModel embedded in Relation) ----
     * Pretorius's running model of *this* interlocutor. Updated each turn
     * from input.  Used by the planner to bias stance / rhetorical mode. */
    int8_t   um_valence;          /* EMA of inferred speaker valence -127..127 */
    int8_t   um_arousal;          /* EMA of inferred arousal -127..127 */
    int8_t   um_dominance;        /* EMA of dominance -127..127 */
    int8_t   um_belief_about_me;  /* does the user seem to think me a genius
                                   * (+) or a fraud (-) -128..127 */
    uint8_t  um_knowledge_level;  /* 0=layman ... 255=peer */
    uint8_t  um_engagement;       /* 0..255 rolling attentiveness */
    uint8_t  um_last_intent;      /* mirror of PE_INTENT_* inferred from speaker */
    uint8_t  um_last_stance;      /* mirror of PE_STANCE_* inferred from speaker */
    uint16_t um_interest_topic;   /* topic id most recently engaged */
    uint16_t um_update_count;     /* turns this model has been updated */
    uint8_t  _pad_um[4];
} Relation;

typedef struct {
    uint32_t phrase_id;
    uint16_t count;
    uint16_t _pad;
    uint32_t last_turn;
} PhraseUsage;

/* v3.1: Chapter — one crystallised autobiographical chapter (exactly 64 bytes). */
typedef struct {
    uint64_t theme_sig;                       /* SimHash centroid of cluster */
    uint32_t start_time;                      /* earliest member timestamp (ms) */
    uint32_t end_time;                        /* latest member timestamp (ms) */
    int16_t  dominant_mood;                   /* weighted-avg emotion.valence*2 */
    uint8_t  memory_count;                    /* episodic memories consolidated */
    uint8_t  salience_peak;                   /* highest salience in chapter */
    int8_t   dominant_drives[PE_DRIVE_COUNT]; /* avg drive bias */
    char     phrase[PE_CHAPTER_PHRASE];       /* key phrase from peak-salience memory */
    uint8_t  _pad[4];                         /* pad to 64 bytes */
} Chapter;
/* layout: 8+4+4+2+1+1+8+32+4 = 64 bytes */

/* v3.1: ChapterBook — serialised as characters/<name>/chapters.bin (1124 bytes). */
typedef struct {
    Chapter  chapters[PE_CHAPTER_MAX];        /* 16 × 64 = 1024 bytes */
    uint8_t  chapter_count;
    uint8_t  dream_pending;                   /* 1 = prepend dream_phrase next turn */
    uint8_t  _pad[2];
    char     dream_phrase[PE_MEM_SUMMARY_LEN];/* composed dream recall text (96 bytes) */
} ChapterBook;
/* layout: 1024+1+1+2+96 = 1124 bytes */

typedef struct {
    uint32_t   user_hash;
    uint8_t    fact_count;
    uint8_t    _pad[3];
    char       facts[PE_SEMANTIC_FACTS][PE_FACT_LEN];
} SemanticBlock;

typedef struct {
    uint8_t type;
    uint8_t _pad[3];
    char    text[PE_SHORT_TERM_TEXT];
} ShortTermEntry;

typedef struct {
    MemoryNode      episodic[PE_EPISODIC_MAX];
    uint16_t        episodic_count;
    uint16_t        _pad0;
    uint32_t        next_memory_id;
    SemanticBlock   semantic[PE_SEMANTIC_USERS];
    uint8_t         semantic_count;
    uint8_t         _pad1[3];
    ShortTermEntry  short_term[PE_SHORT_TERM_LEN];
    uint8_t         short_term_pos;
    uint8_t         _pad2[3];
    PhraseUsage     phrase_usage[PE_PHRASE_USAGE];
} MemoryStore;

#pragma pack(pop)

/* V4: per-relation belief state (compressed identity interpretations).
 * Forward-included so Engine can carry a schema for the active user.
 * Schemas persist in a sibling file <relations_dir>/<hash>.schema.
 * Not part of the v3.x Relation struct — added without breaking the
 * existing on-disk relation layout. */
#include "../schema/schema_state.h"

/* ---------- v2: utterance plan (not serialized — per-turn scratch) ---------- */
typedef struct {
    uint16_t rhetorical_mode;     /* PE_RHET_* */
    uint16_t emotional_objective; /* what feeling to project (valence-ish, -100..100) */
    uint16_t stance;              /* PE_STANCE_* */
    uint16_t target_topic;
    uint16_t callback_memory;     /* episodic index, 0xFFFF if none */
    uint8_t  certainty;           /* 0..255 */
    uint8_t  verbosity;           /* 0..255 */
    uint8_t  aggression;          /* 0..255 */
    uint8_t  theatricality;       /* 0..255 */
    uint8_t  hedging;             /* 0..255 */
    uint8_t  negation_in_play;    /* 1 if input contained negation */
    uint8_t  _pad;
} UtterancePlan;

/* V6: canonical per-turn decision frame.
 *
 * Built after goal/plan selection and before rendering. Renderers receive it
 * read-only through RenderContext. It is symbolic only: no renderer prose and
 * no user prose are stored here. Layer 1 remains the authority.
 */
typedef struct {
    uint32_t actor_id;
    uint8_t  input_class;
    uint8_t  selected_intent;
    uint8_t  speech_act;
    uint8_t  stance;
    uint16_t primary_topic;
    uint16_t selected_goal;
    uint16_t rhetorical_mode;
    uint16_t recall_mode;
    uint16_t selected_memories[4];
    uint8_t  selected_memory_count;
    uint8_t  withhold_reason;
    uint16_t open_loop_pressure;
    uint16_t relation_trust;
    uint16_t relation_threat;
    uint16_t relation_intimacy;
    uint16_t relation_resentment;
    uint16_t relation_obligation;
    uint16_t relation_dependency;
    uint16_t relation_envy;
    uint16_t relation_admiration;
    uint16_t relation_embarrassment;
    uint16_t ideal_gap;
    uint16_t ought_gap;
    uint16_t feared_gap;
    uint16_t max_words;
    uint8_t  require_question;
    uint8_t  allow_empty;
    uint8_t  forbid_meta;
    uint8_t  fatigue_term_count;
    char     fatigue_terms[PE_SPEECH_FATIGUE_TERMS][PE_SPEECH_FATIGUE_LEN];
} CanonicalTurnFrame;

/* ---------- engine ---------- */
typedef struct Engine Engine;

/* Forward declaration for the plasticity subsystem (libplasticity.a).
 * Owning include is "ngram_lm.h"; we only need the type name here. */
typedef struct NGramLM NGramLM;

struct Engine {
    /* read-only after load */
    Identity       identity;
    DriveTable     drives;
    TopicTable     topics;
    PatternTable   patterns;
    TemplateTable  templates;
    FallbackTable  fallbacks;
    GoalTable      goals;
    TodayTable     todays;

    /* mutable */
    NPCState         state;
    MemoryStore      memory;
    pe_actor_index_t   actor_index;           /* V5 Phase 2: per-slot actor tagging sidecar */
    pe_speech_ledger_t speech_ledger;         /* V6 Phase 3: engine-authored speech events */
    Relation           relation;              /* current interlocutor */
    pe_relation_dims_t relation_dims;         /* V6 Phase 4: multi-dim relational profile for current actor */
    pe_dissonance_t    dissonance;            /* V6 Phase 5b: ideal/ought/feared accumulators */
    pe_long_arc_drift_t long_arc_drift;        /* V6: earned month-scale baseline drift */
    pe_open_loops_t    open_loops;            /* V6 Phase 6: carried intentions */
    pe_speech_habits_t speech_habits;         /* V6 Phase 6: conversation rhythm habits */
    pe_learned_knowledge_t learned_knowledge; /* V6: durable learned claims + correction graph */
    pe_tom_t             theory_of_mind;      /* V6: inferred active actor mind */
    uint8_t            current_recall_mode;   /* V6 Phase 5d: selected per-turn from state */
    SchemaState      schema;                /* V4: per-relation compressed beliefs */

    /* per-turn scratch (no heap) */
    uint16_t       active_memories[PE_ACTIVE_MAX];
    uint16_t       active_count;
    uint16_t       active_match[PE_ACTIVE_MAX];   /* match score 0..1000 */

    uint16_t       candidate_ids[64];
    int32_t        candidate_scores[64];
    uint16_t       candidate_count;

    char           char_dir[256];           /* base character directory */
    int            input_class;             /* set by classify_input */
    uint16_t       matched_group;           /* set by classify_input */
    uint16_t       primary_topic;           /* set by topic detection */
    uint16_t       last_template_group;     /* instrumentation for register tests */
    uint16_t       last_template_intent;    /* instrumentation for register tests */

    int            scheduled_delay_ms;
    char           out_buffer[512];
    uint8_t        last_audit_result;     /* PE_AUDIT_* for current/last turn */
    uint8_t        last_audit_violation;  /* PE_AUDIT_V_* diagnostic */
    uint8_t        last_audit_hardness;   /* PE_AUDIT_SOFT/HARD */
    uint8_t        last_audit_rewrite;    /* constrained retry attempted */

    /* v2 per-turn scratch */
    char           lowered[512];      /* cached lowercased input */
    uint8_t        char_present[32];  /* 256-bit bitmap of chars in input */
    uint8_t        negation_active;   /* set if input contains negation cues */
    UtterancePlan  plan;              /* computed by planner each turn */
    CanonicalTurnFrame frame;          /* V6 canonical render contract */

    /* v2.1: plasticity — n-gram LM for "Pretorianness" reranking.
     * NULL = subsystem disabled (LM file missing). */
    NGramLM       *lm;
    void          *lm_data;      /* owned cartridge LM bytes; ngram_lm_load_mem borrows */
    size_t         lm_size;

    /* v3.0: per-turn LSH signature of the input.  Computed once in
     * pe_prep_input, reused by pe_associative_recall for fuzzy match. */
    uint64_t      input_sig;

    /* v3.1: autobiographical chapters + dream state. */
    ChapterBook   chapters;

    /* V5: reflective memory consolidation (Park et al. UIST 2023).
     * The algorithm lives in memory/reflection.h; the state is embedded
     * here so persona.h remains the single authority for Engine layout. */
    struct {
        MemoryNode  memories[16];          /* PE_REFLECTION_MAX */
        uint16_t    count;
        uint16_t    head;
        uint32_t    last_consolidate_turn;
        uint8_t     abstraction_level[16];
        uint8_t     source_count[16];
        uint16_t    source_topic[16];
        uint8_t     _pad[8];
    } reflections;

    /* v3.2: AETHER long-term episodic storage.  Opaque pointer — engine.c
     * includes aether.h; cartridge compilers (compile_*.c) see only the
     * forward-declared struct, no header dependency. */
    struct aether_handle *aether;

    /* v3.2: per-turn cold-memory scratch.  When pe_associative_recall
     * queries AETHER, retrieved events are materialised into MemoryNode
     * form here and surfaced via active_memories[] using sentinel indices
     * (PE_EPISODIC_MAX + cold_idx).  Reset each turn. */
    MemoryNode    cold_scratch[PE_COLD_SCRATCH_MAX];
    uint16_t      cold_scratch_count;

    /* v3.2: cartridge-borne synonym banks.  Loaded from
     * <character_dir>/banks.bin in persona_open; falls back to the
     * built-in Pretorian default registry if the file is missing. */
    BankRegistry  banks;

    Environment   environment;
    uint8_t       identity_boost_remaining;
    int16_t       baseline_valence;
    int16_t       baseline_arousal;
    int32_t       recent_valence_sum;
    int32_t       recent_arousal_sum;
    uint8_t       recent_count;
    uint8_t       cold_open_callback_source;     /* 0 none, 1 generic, 2 cartridge */
    uint8_t       cold_open_callback_template_index;
    uint8_t       cold_open_callback_exclusive;
    uint8_t       _pad_cold_open_dbg;
    uint16_t      cold_open_callback_topic;
    uint16_t      cold_open_callback_memory_index;
    uint16_t      private_thought_topic;
    uint16_t      private_thought_pressure;
    uint8_t       private_thought_kind;       /* internal state, not renderer prose */
    uint8_t       expressed_thought_kind;     /* outward speech act/move */
    uint8_t       private_thought_withheld;
    uint8_t       expression_policy;
    uint32_t      private_thought_hash;
    uint32_t      expressed_thought_hash;
};

/* ---------- public API ---------- */

/* Open a character directory. Loads identity/drives/dialogue/topics/today
 * (read-only), then loads or initializes state/memory. Returns 0 on success. */
int  persona_open(Engine *eng, const char *character_dir);

/* Single-turn process. Writes a null-terminated response into out (size n).
 * Returns response length, or negative on error. */
int  persona_process_input(Engine *eng,
                           const char *user_id,
                           const char *input_text,
                           char *out, size_t n);

/* Force flush of mutable state (state.bin, memory.bin, current relation). */
int  persona_save(Engine *eng);

/* Release subsystem resources (e.g. the LM buffer). Call after save() at
 * end of session. Idempotent. Process exit also reclaims, so this is mostly
 * for hygienic shutdown and leak-checked tests. */
void persona_close(Engine *eng);

/* Switch interlocutor without ending the session. */
int  persona_set_user(Engine *eng, const char *user_id);

/* Reproduce every internal variable driving the last response. */
void persona_debug_dump(const Engine *eng);

/* v2: dump the trace ring with ASCII sparklines + intent timeline. */
void persona_trace_dump(const Engine *eng);

/* v2: dump the most recently built UtterancePlan. */
void persona_plan_dump(const Engine *eng);

/* Monotonic clock in ms. Override for tests/deterministic replay. */
uint32_t persona_now_ms(void);

/* xorshift32 — seeded from state.rng_state (which folds today_seed XOR turn). */
uint32_t persona_rng_u32(NPCState *s);
int32_t  persona_rng_range(NPCState *s, int32_t lo, int32_t hi); /* inclusive */

/* String hash used for user IDs and phrase IDs (FNV-1a 32). */
uint32_t persona_hash(const char *s);

/* Saturating integer math */
static inline int16_t pe_clamp16(int32_t v, int32_t lo, int32_t hi){
    if (v < lo) return (int16_t)lo;
    if (v > hi) return (int16_t)hi;
    return (int16_t)v;
}

#ifdef __cplusplus
}
#endif
#endif

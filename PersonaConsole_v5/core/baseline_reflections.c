/* baseline_reflections.c — universal reflection template pool.
 *
 * Adapted from Park et al. (2023) §3.2.  Their reflections are generated
 * by an LLM ("what high-level abstraction summarizes these recent
 * observations?"). We use a curated template pool instead, picked by
 * topic class.  A future cart format bump may let cartridges override
 * with character-specific reflection templates.
 *
 * Each topic class has 4 variants so the same reflection topic surfaces
 * differently on each occurrence.  Variant is selected deterministically
 * from the reflection's id ⊕ timestamp.
 *
 * Templates use {topic} as the substitution slot.  pe_reflection_render
 * (in memory/reflection.c) handles substitution at surface time.
 *
 * The templates are deliberately ABSTRACTING — they don't recount events,
 * they observe patterns.  Compare:
 *   raw episodic memory:   "User asked about creation"
 *   reflection:            "I notice we keep returning to creation."
 *
 * That distinction is what Park et al. demonstrated produces the
 * "lived-in" quality their agents exhibited.
 */
#include "persona.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Generic reflections — fit any topic. */
static const char *GENERIC[] = {
    "I notice we keep returning to {topic}.",
    "There is a pattern in how we talk about {topic}.",
    "These {topic} conversations are accumulating.",
    "I find I have more to say about {topic} each time.",
    "We have circled {topic} more than once now.",
    "Something about {topic} is unfinished between us.",
};
#define GENERIC_N (sizeof(GENERIC) / sizeof(GENERIC[0]))

/* Reflections for high-arousal / charged topics (anger / grief / fear). */
static const char *CHARGED[] = {
    "Twice now you have brought up {topic}, and I have not been ready.",
    "Each time we touch {topic} I feel the room change.",
    "I am beginning to suspect {topic} is the matter beneath the matter.",
    "We keep arriving at {topic} as if it were the only door.",
};
#define CHARGED_N (sizeof(CHARGED) / sizeof(CHARGED[0]))

/* Reflections for intimate topics (relationship / disclosure). */
static const char *INTIMATE[] = {
    "I find I trust you more easily when we speak of {topic}.",
    "{topic} is becoming our ground.",
    "I have noticed I tell you about {topic} that I would not tell others.",
    "Between us, {topic} carries weight it does not carry alone.",
};
#define INTIMATE_N (sizeof(INTIMATE) / sizeof(INTIMATE[0]))

/* Reflections for obsession-class topics (the character's actual focus). */
static const char *OBSESSION[] = {
    "Always {topic} returns to the conversation.  I begin to think you sense its weight.",
    "{topic}.  You have asked enough times that I no longer pretend not to expect it.",
    "We return to {topic} the way I do — as if drawn.",
    "I have stopped resisting talk of {topic}.  Perhaps that is fitting.",
};
#define OBSESSION_N (sizeof(OBSESSION) / sizeof(OBSESSION[0]))

/* Public entry: select a template for a topic.  Caller passes the
 * topic id + a variant seed.  Returns a static string pointer that
 * the engine substitutes {topic} into. */
const char *pe_reflection_template_for_topic(uint16_t topic_id,
                                             uint32_t variant){
    /* Topic-class routing: in the engine, the cartridge declares topic
     * adjacency.  Without per-cart reflection templates yet, we use a
     * rough mapping by topic id range.  Cart-specific reflection
     * templates are a v6 format extension. */
    const char **pool;
    int pool_n;

    /* Heuristic: low topic ids (1-8 in the Pretorius gallery) are
     * "core" obsessions; mid (9-15) are charged; intimacy/relationship
     * topics map by name when available.  Since we don't have the
     * topic name here, we use the variant low bits to spread across
     * pools — gives every reflection some variation while a fuller
     * topic-class system is added. */
    uint32_t pool_pick = (variant >> 4) & 3;
    switch (pool_pick){
        case 0: pool = OBSESSION; pool_n = (int)OBSESSION_N; break;
        case 1: pool = CHARGED;   pool_n = (int)CHARGED_N;   break;
        case 2: pool = INTIMATE;  pool_n = (int)INTIMATE_N;  break;
        default: pool = GENERIC;  pool_n = (int)GENERIC_N;   break;
    }
    (void)topic_id;
    return pool[variant % (uint32_t)pool_n];
}

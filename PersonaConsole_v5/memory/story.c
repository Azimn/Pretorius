/* story.c — v3.1: autobiographical chapters and dream recall.
 *
 * Pipeline position:
 *   persona_open  → pe_check_dream (detects ≥8 h gap, crystallises, sets flag)
 *   process_input → dream prefix prepended on first turn (engine.c)
 *
 * Crystallisation is allocation-free; all working space lives on the stack
 * (≈6 KB for PE_CHAPTER_MAX=16, PE_EPISODIC_MAX=50) — safe on PIII.
 */
#include "persona.h"
#include "persona_internal.h"
#include <string.h>
#include <stdio.h>

/* ---------- crystallise_chapters ----------
 *
 * Groups episodic memories into up to PE_CHAPTER_MAX temporal buckets.
 * For each bucket: majority-vote SimHash centroid, weighted mood average,
 * peak-salience phrase extraction.
 *
 * Overwrites eng->chapters (chapter_count, chapters[]).
 * Does NOT touch dream_pending / dream_phrase — those are set by pe_check_dream.
 */
void pe_crystallize_chapters(Engine *eng){
    ChapterBook      *cb = &eng->chapters;
    const MemoryStore *m  = &eng->memory;

    /* reset chapter array but preserve dream state */
    uint8_t  dp  = cb->dream_pending;
    char     dph[PE_MEM_SUMMARY_LEN];
    memcpy(dph, cb->dream_phrase, PE_MEM_SUMMARY_LEN);
    memset(cb->chapters, 0, sizeof(cb->chapters));
    cb->chapter_count = 0;
    cb->dream_pending = dp;
    memcpy(cb->dream_phrase, dph, PE_MEM_SUMMARY_LEN);

    if (m->episodic_count == 0) return;

    /* --- find timestamp range --- */
    uint32_t t_min = 0xFFFFFFFFu, t_max = 0;
    for (uint16_t i = 0; i < m->episodic_count; ++i){
        uint32_t ts = m->episodic[i].timestamp;
        if (ts < t_min) t_min = ts;
        if (ts > t_max) t_max = ts;
    }
    if (t_min == t_max) t_max = t_min + 1;

    uint8_t k = (m->episodic_count < PE_CHAPTER_MAX)
              ? (uint8_t)m->episodic_count
              : (uint8_t)PE_CHAPTER_MAX;
    uint64_t span = (uint64_t)(t_max - t_min) + 1;

    /* --- stack accumulators --- */
    Chapter  scratch[PE_CHAPTER_MAX];
    int32_t  mood_acc[PE_CHAPTER_MAX];
    int32_t  member_cnt[PE_CHAPTER_MAX];
    int      bit_votes[PE_CHAPTER_MAX][64]; /* 16×64×4 = 4096 bytes */

    memset(scratch,    0, sizeof(scratch));
    memset(mood_acc,   0, sizeof(mood_acc));
    memset(member_cnt, 0, sizeof(member_cnt));
    memset(bit_votes,  0, sizeof(bit_votes));

    /* --- assign each memory to a temporal bucket --- */
    for (uint16_t i = 0; i < m->episodic_count; ++i){
        const MemoryNode *n = &m->episodic[i];
        uint32_t bucket = (uint32_t)(
            ((uint64_t)(n->timestamp - t_min) * k) / span);
        if (bucket >= k) bucket = k - 1;

        Chapter *ch = &scratch[bucket];
        member_cnt[bucket]++;

        if (ch->start_time == 0 || n->timestamp < ch->start_time)
            ch->start_time = n->timestamp;
        if (n->timestamp > ch->end_time)
            ch->end_time = n->timestamp;

        /* weighted mood: valence × 2 gives [-200..+200] */
        mood_acc[bucket] += (int32_t)n->emotion.valence * 2;

        /* track peak salience to choose representative phrase */
        if (n->salience > ch->salience_peak){
            ch->salience_peak = n->salience;
            const char *src = n->summary;
            size_t L = strlen(src);
            if (L >= PE_CHAPTER_PHRASE) L = PE_CHAPTER_PHRASE - 1;
            memcpy(ch->phrase, src, L);
            ch->phrase[L] = '\0';
        }

        /* accumulate bits for vertical-majority SimHash centroid */
        if (n->lsh_sig != 0){
            for (int b = 0; b < 64; ++b)
                if (n->lsh_sig & ((uint64_t)1 << b))
                    bit_votes[bucket][b]++;
        }
    }

    /* --- finalise non-empty buckets → chapters --- */
    uint8_t out = 0;
    for (int c = 0; c < k && out < PE_CHAPTER_MAX; ++c){
        if (member_cnt[c] == 0) continue;
        Chapter *ch = &scratch[c];
        int32_t mc  = member_cnt[c];

        ch->memory_count  = (uint8_t)(mc > 255 ? 255 : mc);
        ch->dominant_mood = (int16_t)(mood_acc[c] / mc);

        /* majority-vote centroid (vertical bitwise) */
        uint64_t sig  = 0;
        int      half = mc / 2;
        for (int b = 0; b < 64; ++b)
            if (bit_votes[c][b] > half) sig |= ((uint64_t)1 << b);
        ch->theme_sig = sig;

        cb->chapters[out++] = *ch;
    }
    cb->chapter_count = out;
}

/* ---------- dream_tone ---------- */
static const char *dream_tone(int16_t mood){
    if (mood >  100) return "luminous";
    if (mood >   20) return "restless";
    if (mood >  -20) return "strange";
    if (mood >  -80) return "haunted";
    return "terrible";
}

/* ---------- pe_check_dream ----------
 *
 * Called from persona_open with gap_ms = now − last_update_time.
 * If gap >= 8 h: crystallise → pick highest-salience chapter →
 * compose dream_phrase → set dream_pending = 1.
 * engine.c prepends dream_phrase to the very first response.
 */
void pe_check_dream(Engine *eng, uint32_t gap_ms){
    ChapterBook *cb = &eng->chapters;
    cb->dream_pending = 0;

    /* threshold: 8 hours */
    if (gap_ms < 8u * 3600u * 1000u) return;

    pe_crystallize_chapters(eng);
    if (cb->chapter_count == 0) return;

    /* pick chapter with highest peak salience */
    int best = 0;
    for (int i = 1; i < cb->chapter_count; ++i)
        if (cb->chapters[i].salience_peak > cb->chapters[best].salience_peak)
            best = i;

    const Chapter *ch   = &cb->chapters[best];
    const char    *tone = dream_tone(ch->dominant_mood);
    const char    *frag = ch->phrase[0] ? ch->phrase : "something nameless";

    snprintf(cb->dream_phrase, sizeof(cb->dream_phrase),
             "[a %s dream, half-remembered] ...%s...",
             tone, frag);

    cb->dream_pending = 1;
}

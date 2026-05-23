/* reflection.c — V5 reflective memory consolidation (impl).
 *
 * See reflection.h for the architectural reasoning + citations.
 * No malloc.  No floats.  Deterministic.
 */
#include "reflection.h"
#include "persona_internal.h"
#include "lsh_memory.h"
#include <stdio.h>
#include <string.h>

/* ---------- helpers ---------- */

static int abs_i(int v){ return v < 0 ? -v : v; }
static int clamp_i(int v, int lo, int hi){
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Hamming distance between two 64-bit signatures. */
static int sig_hamming(uint64_t a, uint64_t b){
    return lsh_hamming_distance(a, b);
}

/* ---------- importance scoring (Park et al. §3.2 adaptation) ---------- */

int pe_memory_importance(const MemoryNode *m, uint32_t current_turn){
    if (!m || !m->summary[0]) return 0;
    int sal = m->salience;                              /* 0..255 */
    int val = abs_i((int)m->emotion.valence) * 255 / 100;   /* 0..255 */
    int aro = (int)m->emotion.arousal * 255 / 100;          /* 0..255 */

    /* recency_bonus: peaks for very recent memories (last 5 turns),
     * decays to zero by turn 100.  m->timestamp is a turn-counter
     * snapshot taken at commit time. */
    int age_turns = (int)current_turn - (int)m->timestamp;
    if (age_turns < 0) age_turns = 0;
    int recency_bonus = 0;
    if (age_turns < 100){
        recency_bonus = ((100 - age_turns) * 255) / 100;
    }

    /* weighted sum, weights × 100 so integer division stays clean.
     * salience 40 · |valence| 25 · arousal 20 · recency 15 = 100 */
    int total = (sal * 40 + val * 25 + aro * 20 + recency_bonus * 15) / 100;
    return clamp_i(total, 0, 255);
}

/* ---------- reflection state ---------- */

void pe_reflection_init(Engine *eng){
    if (!eng) return;
    memset(&eng->reflections, 0, sizeof(eng->reflections));
}

/* ---------- clustering ---------- */

/* Find a cluster of similar memories.  Greedy: pick first ungrouped
 * memory as the seed, then attach any other ungrouped memory whose
 * Hamming distance to the seed is ≤ threshold. */
typedef struct {
    int indices[PE_REFLECTION_MAX];
    int count;
    uint16_t dominant_topic;
    uint64_t consensus_sig;
} Cluster;

/* Bitwise majority vote across signatures.  Same primitive that
 * AETHER's consolidate.c uses to produce LSH centroids — we just
 * apply it to a smaller K here. */
static uint64_t majority_signature(const uint64_t *sigs, int n){
    if (n == 0) return 0;
    if (n == 1) return sigs[0];
    int bit_count[64] = {0};
    for (int i = 0; i < n; ++i){
        uint64_t s = sigs[i];
        for (int b = 0; b < 64; ++b) if (s & (1ULL << b)) bit_count[b]++;
    }
    uint64_t out = 0;
    int half = n / 2;
    for (int b = 0; b < 64; ++b){
        if (bit_count[b] > half) out |= (1ULL << b);
    }
    return out;
}

/* Average emotion across cluster members. */
static EmotionVector cluster_emotion(const MemoryNode *m[], int n){
    EmotionVector e = {0,0,0,0};
    if (n == 0) return e;
    int v_sum = 0, a_sum = 0, d_sum = 0;
    for (int i = 0; i < n; ++i){
        v_sum += m[i]->emotion.valence;
        a_sum += m[i]->emotion.arousal;
        d_sum += m[i]->emotion.dominance;
    }
    e.valence   = (int8_t)clamp_i(v_sum / n, -100, 100);
    e.arousal   = (int8_t)clamp_i(a_sum / n,    0, 100);
    e.dominance = (int8_t)clamp_i(d_sum / n, -100, 100);
    return e;
}

/* ---------- main consolidation ---------- */

int pe_consolidate_reflections(Engine *eng){
    if (!eng) return 0;
    /* alias for readability; persona.h owns the actual layout */
    __typeof__(eng->reflections) *r = &eng->reflections;
    uint32_t turn = eng->state.turn_count;

    /* Cooldown — don't consolidate every turn.  Park et al. trigger
     * reflection when accumulated importance exceeds a threshold; we
     * use a simpler periodic gate that's equivalent under steady-state
     * input rates. */
    if (turn < r->last_consolidate_turn + PE_REFLECTION_CONSOLIDATE_EVERY){
        return 0;
    }
    r->last_consolidate_turn = turn;

    /* Step 1: score every episodic memory by importance, pick top 12 */
    typedef struct { int idx; int score; } Cand;
    Cand cands[PE_EPISODIC_MAX];
    int n_cands = 0;
    for (int i = 0; i < eng->memory.episodic_count && i < PE_EPISODIC_MAX; ++i){
        MemoryNode *m = &eng->memory.episodic[i];
        if (!m->summary[0]) continue;
        cands[n_cands].idx = i;
        cands[n_cands].score = pe_memory_importance(m, turn);
        ++n_cands;
    }
    if (n_cands < PE_REFLECTION_CLUSTER_MIN) return 0;

    /* Sort by score descending (insertion sort; n_cands small) */
    for (int i = 1; i < n_cands; ++i){
        Cand v = cands[i];
        int j = i - 1;
        while (j >= 0 && cands[j].score < v.score){
            cands[j+1] = cands[j]; --j;
        }
        cands[j+1] = v;
    }
    int K = n_cands < 12 ? n_cands : 12;

    /* Step 2: cluster top-K by SimHash Hamming distance */
    int grouped[12] = {0};
    int new_reflections = 0;
    Cluster c;

    for (int seed = 0; seed < K; ++seed){
        if (grouped[seed]) continue;
        c.count = 0;
        c.indices[c.count++] = cands[seed].idx;
        grouped[seed] = 1;

        uint64_t seed_sig = eng->memory.episodic[cands[seed].idx].lsh_sig;
        uint16_t seed_topic = eng->memory.episodic[cands[seed].idx].topic_id;

        for (int j = seed + 1; j < K; ++j){
            if (grouped[j]) continue;
            uint64_t s = eng->memory.episodic[cands[j].idx].lsh_sig;
            int hd = sig_hamming(seed_sig, s);
            if (hd <= PE_REFLECTION_HAMMING_MAX){
                c.indices[c.count++] = cands[j].idx;
                grouped[j] = 1;
            }
        }

        if (c.count < PE_REFLECTION_CLUSTER_MIN) continue;

        /* Step 3: synthesize the reflection */
        const MemoryNode *src[PE_REFLECTION_MAX];
        uint64_t sigs[PE_REFLECTION_MAX];
        int max_sal = 0;
        uint16_t topic_votes[64] = {0};
        for (int k = 0; k < c.count && k < PE_REFLECTION_MAX; ++k){
            src[k] = &eng->memory.episodic[c.indices[k]];
            sigs[k] = src[k]->lsh_sig;
            if (src[k]->salience > max_sal) max_sal = src[k]->salience;
            uint16_t t = src[k]->topic_id;
            if (t != 0xFFFF && t < 64) topic_votes[t]++;
        }
        c.consensus_sig = majority_signature(sigs, c.count);
        /* dominant topic = mode */
        c.dominant_topic = seed_topic;
        int best_votes = 0;
        for (int t = 0; t < 64; ++t){
            if (topic_votes[t] > best_votes){
                best_votes = topic_votes[t];
                c.dominant_topic = (uint16_t)t;
            }
        }

        /* Dedup: don't add a reflection whose consensus signature is
         * already represented in the reflection ring within hamming 8. */
        int duplicate = 0;
        for (int e = 0; e < r->count; ++e){
            int hd = sig_hamming(c.consensus_sig, r->memories[e].lsh_sig);
            if (hd <= 8){ duplicate = 1; break; }
        }
        if (duplicate) continue;

        /* Write the new reflection into the ring (overwrite oldest if full) */
        int slot = r->head;
        MemoryNode *ref = &r->memories[slot];
        memset(ref, 0, sizeof(*ref));
        ref->id = 0xF0000000u | (uint32_t)slot;   /* high bit marks reflection id */
        ref->type = 0;
        ref->salience = (uint8_t)clamp_i(max_sal * 13 / 10, 0, 255);
        ref->emotion = cluster_emotion(src, c.count);
        ref->timestamp = turn;
        ref->core_memory = 0;
        ref->decay_counter = 0;
        ref->topic_id = c.dominant_topic;
        ref->private_threshold = 0;
        ref->retrieval_prob = 230;
        ref->memory_type = MEM_CACHE;   /* not core, not raw episodic */
        ref->lsh_sig = c.consensus_sig;
        /* Summary: placeholder until pe_reflection_render() resolves it.
         * Stored as a tag we recognize at render time. */
        snprintf(ref->summary, sizeof(ref->summary),
                 "[reflection topic=%u sources=%d]", c.dominant_topic, c.count);

        r->abstraction_level[slot] = 1;
        r->source_count[slot] = (uint8_t)c.count;
        r->source_topic[slot] = c.dominant_topic;
        r->head = (uint16_t)((slot + 1) % PE_REFLECTION_MAX);
        if (r->count < PE_REFLECTION_MAX) r->count++;
        ++new_reflections;
    }

    return new_reflections;
}

/* ---------- query ---------- */

int pe_query_reflections(const Engine *eng,
                         uint64_t query_sig,
                         uint16_t query_topic,
                         MemoryNode *out, int max_out){
    if (!eng || max_out <= 0) return 0;
    const __typeof__(eng->reflections) *r = &eng->reflections;
    if (r->count == 0) return 0;
    /* Score each reflection by topic match + inverse hamming */
    int idx[PE_REFLECTION_MAX], score[PE_REFLECTION_MAX];
    int n = 0;
    for (int i = 0; i < r->count; ++i){
        const MemoryNode *m = &r->memories[i];
        if (!m->summary[0]) continue;
        int s = 0;
        if (query_topic != 0xFFFF && m->topic_id == query_topic) s += 200;
        int hd = sig_hamming(query_sig, m->lsh_sig);
        s += (64 - hd) * 4;   /* up to +256 for identical, 0 for fully different */
        s += m->salience / 4; /* importance contributes */
        idx[n] = i; score[n] = s; ++n;
    }
    /* Insertion-sort top-K by score */
    for (int i = 1; i < n; ++i){
        int v_idx = idx[i], v_sc = score[i], j = i - 1;
        while (j >= 0 && score[j] < v_sc){
            idx[j+1] = idx[j]; score[j+1] = score[j]; --j;
        }
        idx[j+1] = v_idx; score[j+1] = v_sc;
    }
    int n_out = n < max_out ? n : max_out;
    for (int k = 0; k < n_out; ++k) out[k] = r->memories[idx[k]];
    return n_out;
}

/* ---------- persistence ---------- */

int pe_reflection_save(const Engine *eng, const char *char_dir){
    if (!eng || !char_dir) return -1;
    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, "reflections.bin") != 0) return -1;
    return pe_write_file_atomic(path, &eng->reflections, sizeof(eng->reflections));
}

int pe_reflection_load(Engine *eng, const char *char_dir){
    if (!eng || !char_dir) return -1;
    memset(&eng->reflections, 0, sizeof(eng->reflections));
    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, "reflections.bin") != 0) return -1;
    /* If the file exists but is the wrong size, init fresh (format change). */
    __typeof__(eng->reflections) tmp;
    if (pe_read_file(path, &tmp, sizeof(tmp)) == 0){
        eng->reflections = tmp;
    }
    return 0;
}

/* ---------- rendering ---------- */

/* Bring in the baseline reflection template pool.  This function lives
 * in core/baseline_reflections.c. */
extern const char *pe_reflection_template_for_topic(uint16_t topic_id,
                                                    uint32_t variant);

int pe_reflection_render(const Engine *eng,
                         const MemoryNode *reflection,
                         char *out, int out_cap){
    if (!eng || !reflection || !out || out_cap < 32) return -1;
    /* Resolve topic name */
    const char *topic_name = "the matter";
    for (uint32_t i = 0; i < eng->topics.count; ++i){
        if (eng->topics.topics[i].id == reflection->topic_id){
            topic_name = eng->topics.topics[i].name;
            break;
        }
    }
    /* Pick a template variant based on reflection id (deterministic) */
    uint32_t variant = reflection->id ^ reflection->timestamp;
    const char *tmpl = pe_reflection_template_for_topic(reflection->topic_id, variant);
    if (!tmpl) tmpl = "I notice we keep returning to {topic}.";

    /* Substitute {topic} */
    const char *slot = strstr(tmpl, "{topic}");
    if (slot){
        int head_len = (int)(slot - tmpl);
        return snprintf(out, (size_t)out_cap, "%.*s%s%s",
                        head_len, tmpl, topic_name, slot + 7);
    }
    return snprintf(out, (size_t)out_cap, "%s", tmpl);
}

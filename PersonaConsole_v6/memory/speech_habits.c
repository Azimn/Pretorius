/* speech_habits.c -- V6 lightweight conversation rhythm sidecar. */
#include "speech_habits.h"
#include "../core/persona_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static uint16_t clamp_u16_int(int v, int hi){
    if (v < 0) return 0;
    if (v > hi) return (uint16_t)hi;
    return (uint16_t)v;
}

static uint16_t count_words(const char *s){
    uint16_t n = 0;
    int in_word = 0;
    if (!s) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p){
        if (isalnum(*p)){
            if (!in_word){
                if (n < 65535u) n++;
                in_word = 1;
            }
        } else {
            in_word = 0;
        }
    }
    return n;
}

static int is_stop_word(const char *w){
    static const char *STOP[] = {
        "about","after","again","because","before","being","between","could",
        "every","everything","having","maybe","never","other","really","right",
        "should","something","still","their","there","these","thing","things",
        "think","those","through","trying","under","where","which","while",
        "whole","would","youre","you've","your","yours",
        "mock","ollama","api","reply","seed", NULL
    };
    if (!w || !w[0]) return 1;
    for (int i = 0; STOP[i]; ++i)
        if (!strcmp(w, STOP[i])) return 1;
    return 0;
}

static int term_index(const pe_speech_habits_t *h, const char *term){
    if (!h || !term) return -1;
    for (int i = 0; i < PE_SPEECH_FATIGUE_TERMS; ++i){
        if (h->fatigue_terms[i][0] && !strcmp(h->fatigue_terms[i], term))
            return i;
    }
    return -1;
}

static void add_fatigue_term(pe_speech_habits_t *h, const char *term){
    int idx;
    if (!h || !term || !term[0] || is_stop_word(term)) return;
    idx = term_index(h, term);
    if (idx >= 0){
        if (h->fatigue_hits[idx] < 255u) h->fatigue_hits[idx]++;
        return;
    }
    idx = h->fatigue_count < PE_SPEECH_FATIGUE_TERMS
        ? h->fatigue_count++ : (h->turns_observed % PE_SPEECH_FATIGUE_TERMS);
    snprintf(h->fatigue_terms[idx], PE_SPEECH_FATIGUE_LEN, "%s", term);
    h->fatigue_hits[idx] = 1;
}

void pe_speech_habits_init(pe_speech_habits_t *h){
    if (!h) return;
    memset(h, 0, sizeof(*h));
    h->header.magic       = PE_SPEECH_HABITS_MAGIC;
    h->header.version     = PE_SPEECH_HABITS_VERSION;
    h->header.flags       = PE_SIDECAR_F_OPTIONAL;
    h->header.entry_count = 1;
    h->header.capacity    = 1;
}

void pe_speech_habits_note_input(pe_speech_habits_t *h, const char *input){
    char seen[PE_SPEECH_FATIGUE_TERMS][PE_SPEECH_FATIGUE_LEN];
    uint8_t seen_count = 0;
    char term[PE_SPEECH_FATIGUE_LEN];
    int len = 0;
    if (!h || h->header.magic != PE_SPEECH_HABITS_MAGIC)
        pe_speech_habits_init(h);
    memset(seen, 0, sizeof(seen));
    if (!input) return;
    for (const unsigned char *p = (const unsigned char *)input;; ++p){
        int is_word = *p && (isalnum(*p) || *p == '\'');
        if (is_word){
            if (len < PE_SPEECH_FATIGUE_LEN - 1){
                unsigned char c = (unsigned char)tolower(*p);
                if (c != '\'') term[len++] = (char)c;
            }
            continue;
        }
        if (len >= 5){
            term[len] = 0;
            if (!is_stop_word(term)){
                int already_seen = 0;
                for (int i = 0; i < seen_count; ++i)
                    if (!strcmp(seen[i], term)) already_seen = 1;
                if (!already_seen && seen_count < PE_SPEECH_FATIGUE_TERMS){
                    snprintf(seen[seen_count++], PE_SPEECH_FATIGUE_LEN, "%s", term);
                    add_fatigue_term(h, term);
                }
            }
        }
        len = 0;
        if (!*p) break;
    }
    {
        int hot = 0;
        for (int i = 0; i < PE_SPEECH_FATIGUE_TERMS; ++i)
            if (h->fatigue_terms[i][0] && h->fatigue_hits[i] >= 2u) hot++;
        h->fatigue_bias = clamp_u16_int(hot * 180, 1000);
    }
}

int pe_speech_habits_load(pe_speech_habits_t *h, const char *char_dir){
    char path[512];
    pe_speech_habits_init(h);
    if (!h || !char_dir) return -1;
    if (pe_path_join(path, sizeof(path), char_dir, "speech_habits.bin") != 0)
        return -2;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    pe_speech_habits_t tmp;
    size_t got = fread(&tmp, 1, sizeof(tmp), f);
    fclose(f);
    if (got != sizeof(tmp)) return -3;
    if (pe_sidecar_validate(&tmp.header, PE_SPEECH_HABITS_MAGIC,
                            1, PE_SPEECH_HABITS_VERSION) != PE_SIDECAR_OK)
        return -4;
    *h = tmp;
    return 0;
}

int pe_speech_habits_save(const pe_speech_habits_t *h, const char *char_dir){
    char path[512];
    if (!h || !char_dir || h->header.magic != PE_SPEECH_HABITS_MAGIC) return -1;
    if (pe_path_join(path, sizeof(path), char_dir, "speech_habits.bin") != 0)
        return -2;
    return pe_write_file_atomic(path, h, sizeof(*h));
}

void pe_speech_habits_update(pe_speech_habits_t *h,
                             const char *reply,
                             int reply_had_question,
                             int input_class){
    if (!h || h->header.magic != PE_SPEECH_HABITS_MAGIC)
        pe_speech_habits_init(h);

    uint16_t words = count_words(reply);
    if (h->turns_observed < 0xFFFFFFFFu) h->turns_observed++;
    if (reply_had_question && h->questions_asked < 0xFFFFFFFFu)
        h->questions_asked++;

    if (h->avg_reply_words_q8 == 0)
        h->avg_reply_words_q8 = (uint16_t)(words << 8);
    else {
        uint32_t old = h->avg_reply_words_q8;
        uint32_t cur = ((old * 7u) + ((uint32_t)words << 8)) / 8u;
        h->avg_reply_words_q8 = (uint16_t)(cur > 0xFFFFu ? 0xFFFFu : cur);
    }

    if (words >= 34u){
        if (h->long_reply_streak < 65535u) h->long_reply_streak++;
        h->short_reply_streak = 0;
    } else if (words <= 10u){
        if (h->short_reply_streak < 65535u) h->short_reply_streak++;
        h->long_reply_streak = 0;
    } else {
        h->long_reply_streak = 0;
        h->short_reply_streak = 0;
    }

    if (reply_had_question)
        h->no_question_streak = 0;
    else if (h->no_question_streak < 65535u)
        h->no_question_streak++;

    pe_speech_habits_note_input(h, reply);

    {
        int avg_words = h->avg_reply_words_q8 >> 8;
        int neutral_bonus = (input_class == 0) ? 20 : 0;
        h->question_bias = clamp_u16_int((int)h->no_question_streak * 52
                                       + neutral_bonus, 1000);
        h->brevity_bias = clamp_u16_int((int)h->long_reply_streak * 140
                                      + (avg_words > 28 ? (avg_words - 28) * 14 : 0),
                                      1000);
        h->initiative_bias = clamp_u16_int((int)h->no_question_streak * 36
                                         + (input_class == 0 ? 24 : 0)
                                         - (input_class == 2 || input_class == 4 ? 120 : 0),
                                         1000);
    }
}

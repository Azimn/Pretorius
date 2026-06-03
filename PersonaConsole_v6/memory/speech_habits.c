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

void pe_speech_habits_init(pe_speech_habits_t *h){
    if (!h) return;
    memset(h, 0, sizeof(*h));
    h->header.magic       = PE_SPEECH_HABITS_MAGIC;
    h->header.version     = PE_SPEECH_HABITS_VERSION;
    h->header.flags       = PE_SIDECAR_F_OPTIONAL;
    h->header.entry_count = 1;
    h->header.capacity    = 1;
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

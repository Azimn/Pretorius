/* cartridge_stats.c -- quick cartridge authoring coverage report. */
#include "persona.h"
#include "persona_internal.h"
#include "cartridge.h"
#include "ngram_lm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_cart;

static int load_blob(const char *root, const char *name, void *buf, size_t n){
    char path[512];
    FILE *f;
    size_t got;
    if (is_cart) return pe_cart_load_section(root, name, buf, n);
    if (pe_path_join(path, sizeof(path), root, name) != 0) return -1;
    f = fopen(path, "rb");
    if (!f) return -1;
    got = fread(buf, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
}

static long file_size(const char *root, const char *name){
    char path[512];
    FILE *f;
    long n;
    if (is_cart) {
        void *buf = NULL;
        size_t sz = 0;
        if (pe_cart_load_section_alloc(root, name, &buf, &sz) != 0) return -1;
        free(buf);
        return (long)sz;
    }
    if (pe_path_join(path, sizeof(path), root, name) != 0) return -1;
    f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static int has_word(const char *s, const char *needle){
    return strstr(s, needle) != NULL;
}

static void coverage(const PatternTable *pt, int out[10]){
    memset(out, 0, sizeof(int) * 10);
    for (uint32_t i = 0; i < pt->count; ++i){
        const Pattern *p = &pt->entries[i];
        if (p->input_class == 1) out[0]++;
        if (p->input_class == 2) out[1]++;
        if (p->input_class == 4) out[2]++;
        if (p->input_class == 5) out[3]++;
        if (p->template_group == PE_BL_GROUP_GREETING
            || has_word(p->keyword, "hello") || has_word(p->keyword, "hi ")
            || has_word(p->keyword, "good morning") || has_word(p->keyword, "good evening"))
            out[4]++;
        if (p->template_group == PE_BL_GROUP_QUESTION
            || has_word(p->keyword, "why") || has_word(p->keyword, "how")
            || has_word(p->keyword, "what"))
            out[5]++;
        if (p->template_group == PE_BL_GROUP_ACK
            || !strcmp(p->keyword, "ok") || !strcmp(p->keyword, "okay")
            || !strcmp(p->keyword, "yes") || !strcmp(p->keyword, "no"))
            out[6]++;
        if (p->template_group == PE_BL_GROUP_STATUS
            || has_word(p->keyword, "how are you") || has_word(p->keyword, "how do you feel"))
            out[7]++;
        if (p->template_group == PE_BL_GROUP_APOLOGY
            || has_word(p->keyword, "sorry") || has_word(p->keyword, "forgive"))
            out[8]++;
        if (p->template_group == PE_BL_GROUP_WHO
            || has_word(p->keyword, "who are you") || has_word(p->keyword, "your name"))
            out[9]++;
    }
}

static const char *intent_name(uint8_t intent){
    static const char *N[] = {
        "answer","evade","accuse","flatter","threaten","probe",
        "redirect","monologue","reminisce","withdraw","joke","boast",
        "initiate","attend","clarify","pause"
    };
    if (intent < PE_INTENT_COUNT) return N[intent];
    return "unknown";
}

static int count_nonzero_u16(const uint16_t *v, int n){
    int c = 0;
    for (int i = 0; i < n; ++i)
        if (v[i] != 0 && v[i] != 0xFFFF) c++;
    return c;
}

static int count_voice_flags(uint32_t flags){
    int c = 0;
    for (int i = 0; i < 32; ++i)
        if (flags & (1u << i)) c++;
    return c;
}

static void print_cov_line(const char *label, const int c[10]){
    printf("  %-22s praise:%d insult:%d threat:%d intimacy:%d greeting:%d question:%d ack:%d status:%d apology:%d who:%d\n",
           label, c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9]);
}

int main(int argc, char **argv){
    Identity id;
    PatternTable patterns, effective_patterns;
    TemplateTable templates, effective_templates;
    TopicTable topics;
    GoalTable goals;
    TodayTable today;
    FallbackTable fallback;
    int cart_cov[10], eff_cov[10], baseline_cov[10];
    int intent_counts[PE_INTENT_COUNT] = {0};
    int baseline_intents[PE_INTENT_COUNT] = {0};
    int rc = 0;

    if (argc != 2){
        fprintf(stderr, "usage: %s <profile-dir-or-cart>\n", argv[0]);
        return 1;
    }
    is_cart = pe_is_cart_path(argv[1]);

    memset(&id, 0, sizeof(id));
    memset(&patterns, 0, sizeof(patterns));
    memset(&templates, 0, sizeof(templates));
    memset(&topics, 0, sizeof(topics));
    memset(&goals, 0, sizeof(goals));
    memset(&today, 0, sizeof(today));
    memset(&fallback, 0, sizeof(fallback));

    rc |= load_blob(argv[1], "identity.bin", &id, sizeof(id));
    rc |= load_blob(argv[1], "dialogue/patterns.bin", &patterns, sizeof(patterns));
    rc |= load_blob(argv[1], "dialogue/templates.bin", &templates, sizeof(templates));
    rc |= load_blob(argv[1], "dialogue/topics.bin", &topics, sizeof(topics));
    rc |= load_blob(argv[1], "dialogue/goals.bin", &goals, sizeof(goals));
    rc |= load_blob(argv[1], "today.bin", &today, sizeof(today));
    rc |= load_blob(argv[1], "dialogue/fallback.bin", &fallback, sizeof(fallback));
    if (rc != 0){
        fprintf(stderr, "cartridge_stats: failed to load one or more required sections\n");
        return 2;
    }

    effective_patterns = patterns;
    effective_templates = templates;
    pe_merge_baseline_patterns(&effective_patterns);
    pe_merge_baseline_templates(&effective_templates);

    coverage(&patterns, cart_cov);
    coverage(&effective_patterns, eff_cov);
    memset(&baseline_cov, 0, sizeof(baseline_cov));
    for (int i = 0; i < 10; ++i) baseline_cov[i] = eff_cov[i] - cart_cov[i];

    for (uint32_t i = 0; i < templates.count; ++i)
        if (templates.entries[i].intent < PE_INTENT_COUNT) intent_counts[templates.entries[i].intent]++;
    for (uint32_t i = 0; i < effective_templates.count; ++i)
        if (effective_templates.entries[i].source == PE_TEMPLATE_SRC_BASELINE
            && effective_templates.entries[i].intent < PE_INTENT_COUNT)
            baseline_intents[effective_templates.entries[i].intent]++;

    printf("Cartridge: %s\n", argv[1]);
    printf("  identity:              %s (%s)\n", id.character_name[0] ? "ok" : "missing-name",
           id.character_name[0] ? id.character_name : "?");
    printf("  patterns:              %u cart, %u effective\n",
           patterns.count, effective_patterns.count);
    print_cov_line("pattern coverage:", cart_cov);
    print_cov_line("baseline adds:", baseline_cov);
    print_cov_line("effective coverage:", eff_cov);

    printf("  templates:             %u cart, %u effective\n",
           templates.count, effective_templates.count);
    printf("  templates by intent:   ");
    for (int i = 0; i < PE_INTENT_COUNT; ++i)
        if (intent_counts[i]) printf("%s:%d ", intent_name((uint8_t)i), intent_counts[i]);
    printf("\n");
    printf("  baseline templates:    ");
    for (int i = 0; i < PE_INTENT_COUNT; ++i)
        if (baseline_intents[i]) printf("%s:%d ", intent_name((uint8_t)i), baseline_intents[i]);
    printf("\n");

    printf("  topics:                %u\n", topics.count);
    printf("  goals:                 %u\n", goals.count);
    printf("  today states:          %u\n", today.count);
    printf("  fallback lines:        tier1:%u tier2:%u tier3:%u\n",
           fallback.tier1_count, fallback.tier2_count, fallback.tier3_count);
    printf("  voice flags:           %d/13 set\n", count_voice_flags(id.voice_flags & 0x1FFFu));
    printf("  core memories:         %u/%u\n", id.core_memory_count, PE_CORE_SEED_MAX);
    printf("  obsessions:            %d/%d\n", count_nonzero_u16(id.obsessions, PE_OBSESSION_COUNT), PE_OBSESSION_COUNT);
    printf("  obsession strength:    ");
    for (int i = 0; i < PE_OBSESSION_COUNT; ++i){
        if (!id.obsessions[i]) break;
        printf("%u%s", id.obsession_strength[i] ? id.obsession_strength[i] : 50,
               (i + 1 < PE_OBSESSION_COUNT && id.obsessions[i + 1]) ? "," : "");
    }
    printf("\n");
    printf("  taboos:                %d/%d\n", count_nonzero_u16(id.taboos, PE_TABOO_COUNT), PE_TABOO_COUNT);
    printf("  voice LM size:         %ld bytes\n", file_size(argv[1], "LM "));
    printf("  authoring note:        low effective apology/status/ack counts are early warning signs\n");
    return 0;
}

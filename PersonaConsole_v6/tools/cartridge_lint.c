/* cartridge_lint.c — static authoring validator for Persona cartridges.
 *
 * Loads the *authored* sections of a .cart (or a profile directory)
 * WITHOUT opening the runtime engine — no state.bin, relations/, or
 * aether/ directories are created.  It then reports authoring mistakes
 * that produce a broken or lifeless character: a character that compiles
 * but has no proactive drives, dangles its obsessions at topics that do
 * not exist, never fires its milestones, or renders empty template slots.
 *
 * This is the pre-flight check for hand-authored cartridges and for the
 * output of the in-browser CartridgeForge: if a character passes lint it
 * has the structural prerequisites to behave like a character rather than
 * a fish tank.  It operationalizes the project rule "simulate only what
 * can be felt in conversation" by verifying the authored hooks exist.
 *
 * Severity:
 *   ERROR  will fail to load, or an authored feature is silently dead
 *   WARN   loads, but a dimension of life is missing or weak
 *   INFO   notable, not a problem
 *
 * Read-only.  Exit code = number of ERRORs (0 = clean), or 2 on usage
 * error, so it drops cleanly into CI / a Forge export gate.
 *
 * Usage: cartridge_lint <character.cart | profile_dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "persona.h"
#include "cartridge.h"

static int g_errors = 0, g_warns = 0, g_infos = 0;

static void err (const char *fmt, ...){ va_list a; va_start(a,fmt); fputs("  ERROR  ", stdout); vprintf(fmt,a); putchar('\n'); va_end(a); ++g_errors; }
static void warn(const char *fmt, ...){ va_list a; va_start(a,fmt); fputs("  WARN   ", stdout); vprintf(fmt,a); putchar('\n'); va_end(a); ++g_warns; }
static void info(const char *fmt, ...){ va_list a; va_start(a,fmt); fputs("  info   ", stdout); vprintf(fmt,a); putchar('\n'); va_end(a); ++g_infos; }
static void section(const char *title){ printf("\n[%s]\n", title); }

/* Load section `name` into `dst` (zeroed first), copying min(available,
 * dstsize) bytes so a V4 cart's shorter identity still loads into the V5
 * struct.  Returns the section's real size, or -1 if absent. */
static long load_section(const char *path, int is_cart, const char *name,
                         void *dst, size_t dstsize){
    memset(dst, 0, dstsize);
    if (is_cart){
        void *buf = NULL; size_t sz = 0;
        if (pe_cart_load_section_alloc(path, name, &buf, &sz) != 0) return -1;
        memcpy(dst, buf, sz < dstsize ? sz : dstsize);
        free(buf);
        return (long)sz;
    }
    char fp[1024];
    snprintf(fp, sizeof(fp), "%s/%s", path, name);
    FILE *f = fopen(fp, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0){ fclose(f); return -1; }
    size_t cp = (size_t)sz < dstsize ? (size_t)sz : dstsize;
    if (cp && fread(dst, 1, cp, f) != cp){ fclose(f); return -1; }
    fclose(f);
    return sz;
}

static int has_voice(const char *path, int is_cart){
    if (is_cart){
        void *b = NULL; size_t s = 0;
        if (pe_cart_load_section_alloc(path, "LM ", &b, &s) != 0) return 0;
        free(b);
        return s > 0;
    }
    char fp[1024];
    snprintf(fp, sizeof(fp), "%s/voice.lm", path);
    FILE *f = fopen(fp, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long s = ftell(f); fclose(f);
    return s > 0;
}

/* topic ids 0 and 0xFFFF are engine sentinels for "none" — a real target
 * must be a non-sentinel id present in the topic table. */
static int topic_is_none(uint32_t id){ return id == 0u || id == 0xFFFFu; }
static int topic_exists(const TopicTable *t, uint32_t id){
    if (topic_is_none(id)) return 0;
    for (uint32_t i = 0; i < t->count && i < PE_TOPIC_MAX; ++i)
        if (t->topics[i].id == id) return 1;
    return 0;
}

static int has_dash_smell(const char *s){
    return s && (strstr(s, "--")
        || strstr(s, "\xE2\x80\x94")
        || strstr(s, "\xE2\x80\x93"));
}

static void lint_style_bank(const char *label,
                            const char bank[][PE_FLOURISH_LEN],
                            int count,
                            int min_live){
    int live = 0, dashy = 0;
    for (int i = 0; i < count; ++i){
        if (!bank[i][0]) continue;
        ++live;
        if (has_dash_smell(bank[i])) ++dashy;
        for (int j = i + 1; j < count; ++j){
            if (bank[j][0] && !strcmp(bank[i], bank[j])){
                warn("%s slot %d duplicates slot %d exactly; style injections will feel repetitive",
                     label, j + 1, i + 1);
                break;
            }
        }
    }
    if (live < min_live)
        warn("%s has only %d live phrase(s); add more or disable the corresponding voice flag",
             label, live);
    if (dashy > live / 2 && live > 1)
        info("%s is dash-heavy (%d/%d); ordinary chat may read as LLM-ish",
             label, dashy, live);
}

static int is_valid_slot(const char *key){
    static const char *slots[] = {
        "user","address","topic","memory","name","preoccupation",
        "session_count","total_turns","repeated","hour"
    };
    for (size_t i = 0; i < sizeof(slots)/sizeof(slots[0]); ++i)
        if (!strcmp(key, slots[i])) return 1;
    return 0;
}

int main(int argc, char **argv){
    if (argc != 2){
        fprintf(stderr, "usage: %s <character.cart | profile_dir>\n", argv[0]);
        return 2;
    }
    const char *path = argv[1];
    int is_cart = pe_is_cart_path(path);

    printf("=== cartridge_lint: %s (%s) ===\n", path, is_cart ? "cart" : "profile dir");

    if (is_cart){
        int rc = pe_cart_validate_file(path);
        if (rc != 0)
            err("cart failed integrity validation (rc=%d) — checksum or header corrupt", rc);
    }

    /* heap-allocate the large tables */
    Identity      *id  = calloc(1, sizeof *id);
    TopicTable    *tp  = calloc(1, sizeof *tp);
    TemplateTable *tm  = calloc(1, sizeof *tm);
    GoalTable     *gl  = calloc(1, sizeof *gl);
    FallbackTable *fb  = calloc(1, sizeof *fb);
    TodayTable    *td  = calloc(1, sizeof *td);
    if (!id || !tp || !tm || !gl || !fb || !td){ fprintf(stderr, "oom\n"); return 2; }

    long idsize = load_section(path, is_cart, "identity.bin", id, sizeof *id);
    if (idsize < 0){
        err("identity.bin missing — not a loadable cartridge");
        printf("\n=== FAILED: %d error(s) ===\n", g_errors);
        return g_errors ? (g_errors > 100 ? 100 : g_errors) : 1;
    }
    int is_v5 = (idsize >= (long)sizeof *id);

    long tpsize = load_section(path, is_cart, "dialogue/topics.bin",    tp, sizeof *tp);
    long tmsize = load_section(path, is_cart, "dialogue/templates.bin", tm, sizeof *tm);
    load_section(path, is_cart, "dialogue/goals.bin",    gl, sizeof *gl);
    load_section(path, is_cart, "dialogue/fallback.bin", fb, sizeof *fb);
    load_section(path, is_cart, "today.bin",             td, sizeof *td);

    /* ---------------- identity core ---------------- */
    section("identity");
    if (!id->character_name[0])
        err("character_name is empty");
    else
        info("character: \"%s\"", id->character_name);
    info("format: %s (identity.bin = %ld bytes)", is_v5 ? "V5" : "V4-era", idsize);

    int n_addr = 0;
    for (int i = 0; i < PE_ADDRESS_COUNT; ++i) if (id->address_user_as[i][0]) ++n_addr;
    if (n_addr == 0)
        warn("no address forms set — the character can only call the user \"my dear\"");

    if (!id->openness && !id->conscientiousness && !id->extraversion
        && !id->agreeableness && !id->neuroticism)
        warn("all Big Five traits are 0 — flat personality; planner stance will not vary by trait");

    section("style banks");
    lint_style_bank("flourishes", id->flourishes, PE_FLOURISH_COUNT,
                    (id->voice_flags & PE_VF_METAPHOR) ? 3 : 1);
    lint_style_bank("expansions", id->expansions, PE_EXPANSION_COUNT, 2);
    /* ---------------- topics ---------------- */
    section("topics");
    if (tpsize < 0 || tp->count == 0)
        err("no topics defined — the character has nothing to talk about");
    else if (tp->count > PE_TOPIC_MAX)
        err("topic count %u exceeds PE_TOPIC_MAX (%d) — section is corrupt", tp->count, PE_TOPIC_MAX);
    else {
        info("%u topic(s) defined", tp->count);
        for (uint32_t i = 0; i < tp->count && i < PE_TOPIC_MAX; ++i){
            if (!tp->topics[i].name[0])
                warn("topic id=%u has an empty name", tp->topics[i].id);
            for (uint32_t j = i + 1; j < tp->count && j < PE_TOPIC_MAX; ++j)
                if (tp->topics[i].id == tp->topics[j].id){
                    warn("duplicate topic id=%u (slots %u and %u)", tp->topics[i].id, i, j);
                    break;
                }
            for (int a = 0; a < 6; ++a){
                uint16_t adj = tp->topics[i].adjacents[a];
                if (adj == 0xFFFF) break;
                if (!topic_exists(tp, adj))
                    warn("topic \"%s\" (id=%u) is adjacent to nonexistent topic id=%u",
                         tp->topics[i].name, tp->topics[i].id, adj);
            }
        }
    }

    /* ---------------- obsessions / taboos ---------------- */
    section("obsessions & taboos");
    int n_obs = 0;
    for (int i = 0; i < PE_OBSESSION_COUNT; ++i){
        uint16_t o = id->obsessions[i];
        if (topic_is_none(o)) continue;
        ++n_obs;
        if (!topic_exists(tp, o))
            err("obsession slot %d targets topic id=%u, which is not in the topic table "
                "(fixation will collapse to a fallback)", i, o);
    }
    if (n_obs == 0)
        warn("no obsessions set — the character has no fixation pull; offscreen activity defaults weakly");
    else
        info("%d obsession(s) set", n_obs);
    for (int i = 0; i < PE_TABOO_COUNT; ++i){
        uint16_t tb = id->taboos[i];
        if (topic_is_none(tb)) continue;
        if (!topic_exists(tp, tb))
            warn("taboo slot %d targets nonexistent topic id=%u", i, tb);
    }

    /* ---------------- wants (V5 proactivity) ---------------- */
    if (is_v5){
        section("wants (proactivity)");
        int n_wants = 0, n_live = 0;
        for (int i = 0; i < PE_WANT_COUNT; ++i){
            const CharacterWant *w = &id->wants[i];
            int named = w->name[0] != 0;
            int has_topic = !topic_is_none(w->target_topic_id);
            if (named) ++n_wants;
            if (named && !has_topic)
                err("want \"%s\" has no valid target topic (id=%u) — it will never drive "
                    "proactivity or offscreen activity", w->name, w->target_topic_id);
            else if (named && !topic_exists(tp, w->target_topic_id))
                err("want \"%s\" targets topic id=%u, which is not in the topic table",
                    w->name, w->target_topic_id);
            else if (named){
                ++n_live;
                if (w->intensity == 0)
                    info("want \"%s\" has intensity 0 (engine treats as default 100)", w->name);
            }
            if (!named && has_topic)
                warn("want slot %d has a target topic (id=%u) but no name — malformed/partial want",
                     i, w->target_topic_id);
        }
        if (n_wants == 0)
            warn("no wants defined — no proactive drives; offscreen autonomy and want-pull "
                 "are inert (the character only ever reacts)");
        else
            info("%d want(s) authored, %d with a live target topic", n_wants, n_live);
    }

    /* ---------------- preoccupations / resumption / milestones ---------------- */
    if (is_v5){
        section("preoccupations");
        int n_pre = 0;
        for (int i = 0; i < PE_PREOCCUPATION_COUNT; ++i) if (id->current_preoccupations[i][0]) ++n_pre;
        if (n_pre == 0)
            warn("no current preoccupations — offscreen activity falls back to \"the work\"; "
                 "identity surfacing will be bland");
        else
            info("%d preoccupation(s) set", n_pre);

        section("resumption lines");
        int n_res = 0;
        for (int i = 0; i < PE_RESUMPTION_BUCKETS; ++i) if (id->resumption_lines[i][0]) ++n_res;
        if (n_res == 0)
            warn("no resumption lines — returns after an absence use a generic greeting");
        else if (n_res < PE_RESUMPTION_BUCKETS)
            info("%d of %d gap buckets have a resumption line", n_res, PE_RESUMPTION_BUCKETS);
        else
            info("all %d gap buckets have resumption lines", PE_RESUMPTION_BUCKETS);

        section("relationship milestones");
        int n_ms = 0, prev_day = 0;
        for (int i = 0; i < PE_MILESTONE_COUNT; ++i){
            int has_line = id->milestone_lines[i][0] != 0;
            uint16_t day = id->milestone_days[i];
            if (!has_line && day == 0) continue;
            ++n_ms;
            if (has_line && day == 0)
                warn("milestone slot %d has a line but day=0 — it will not fire on a clean schedule", i);
            if (!has_line && day != 0)
                warn("milestone slot %d sets day=%u but has no line", i, day);
            if (day != 0 && day <= prev_day)
                warn("milestone days out of order at slot %d (day=%u <= previous %d) — "
                     "matching assumes ascending days", i, day, prev_day);
            if (day != 0) prev_day = day;
        }
        if (n_ms == 0)
            info("no milestones authored (optional feature)");
        else
            info("%d milestone(s) authored", n_ms);
    } else {
        section("V5 proactivity fields");
        info("V4-era cartridge — wants/preoccupations/resumption/milestones not present by "
             "design; skipping V5 proactivity checks");
    }

    /* ---------------- core memories ---------------- */
    section("seeded memories");
    if (id->core_memory_count > PE_CORE_SEED_MAX)
        err("core_memory_count %u exceeds PE_CORE_SEED_MAX (%d) — corrupt",
            id->core_memory_count, PE_CORE_SEED_MAX);
    else if (id->core_memory_count == 0)
        warn("no seeded core memories — cold recall has no backstory to surface");
    else {
        info("%u core memory(ies) seeded", id->core_memory_count);
        for (int i = 0; i < id->core_memory_count && i < PE_CORE_SEED_MAX; ++i){
            const MemoryNode *m = &id->core_memories_seed[i];
            if (!m->summary[0])
                warn("seeded memory slot %d has an empty summary", i);
            if (!topic_is_none(m->topic_id) && !topic_exists(tp, m->topic_id))
                warn("seeded memory slot %d references nonexistent topic id=%u", i, m->topic_id);
        }
    }

    /* ---------------- dialogue ---------------- */
    section("dialogue");
    if (tmsize < 0 || tm->count == 0)
        err("no dialogue templates — the character can only ever emit fallbacks");
    else {
        info("%u template(s), %u goal(s)", tm->count, gl->count);
        int fb_total = fb->tier1_count + fb->tier2_count + fb->tier3_count;
        if (fb_total == 0)
            warn("no fallback lines in any tier — degraded turns will emit \"...\"");

        int uses_memory = 0, unknown_warns = 0;
        for (uint32_t i = 0; i < tm->count && i < PE_TEMPLATE_MAX; ++i){
            const char *s = tm->entries[i].text;
            for (const char *p = strchr(s, '{'); p; p = strchr(p + 1, '{')){
                const char *e = strchr(p, '}');
                if (!e) break;
                size_t klen = (size_t)(e - p - 1);
                char key[32];
                if (klen >= sizeof key) klen = sizeof key - 1;
                memcpy(key, p + 1, klen); key[klen] = 0;
                if (!strcmp(key, "memory")) uses_memory = 1;
                if (!is_valid_slot(key)){
                    if (unknown_warns < 20)
                        warn("template id=%u uses unknown slot {%s} — renders empty",
                             tm->entries[i].id, key);
                    else if (unknown_warns == 20)
                        warn("(further unknown-slot warnings suppressed)");
                    ++unknown_warns;
                }
            }
        }
        if (uses_memory && id->core_memory_count == 0)
            info("templates reference {memory} but no core memories are seeded — "
                 "callbacks will rely on accrued conversation only");
    }

    /* ---------------- voice ---------------- */
    section("voice");
    if (has_voice(path, is_cart))
        info("voice.lm present — plasticity/SLM rendering available");
    else
        warn("no voice.lm — voice rendering is template-only (deterministic, lower fidelity)");

    /* ---------------- verdict ---------------- */
    printf("\n=== %s: %d error(s), %d warning(s), %d info ===\n",
           g_errors ? "FAILED" : "PASSED", g_errors, g_warns, g_infos);

    free(id); free(tp); free(tm); free(gl); free(fb); free(td);
    return g_errors > 100 ? 100 : g_errors;
}

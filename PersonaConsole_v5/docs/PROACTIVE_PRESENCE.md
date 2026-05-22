# Proactive Presence — Implementation Reference

Drop-in C99 code for each of the ten proactive-presence tricks.
Each section has WHY / WHERE / CODE / TEST. Snippets are ready to
paste; struct extensions are batched into one v5 cartridge bump at
the bottom to avoid format churn.

This file is a reference — none of these are integrated yet. The
existing tree compiles and tests green; pick the ones you want,
integrate in order, run `make v4_all_tests` after each.

---

## Section 1 — drop-in tricks (no cart format change)

These add behavior without breaking existing cartridges. NPCState
gets a few new fields but state.bin auto-rebuilds when missing, so
field additions cost users at most one session of fresh state.

### 1.1 Background time evolution

**Why:** the character should be *measurably different* after a
3-day absence. Currently `delta_ms` is capped at 1 hour, so the
engine never sees real gaps.

**Where:** `core/engine.c`, top of `persona_process_input`.

**Code:**

```c
/* V5: detect first turn of session + apply real elapsed time */
uint32_t now = persona_now_ms();
uint32_t delta = now - eng->state.last_update_time;
uint32_t real_gap_seconds = 0;
{
    uint32_t now_s = (uint32_t)time(NULL);
    if (eng->relation.last_contact > 0 && now_s > eng->relation.last_contact){
        real_gap_seconds = now_s - eng->relation.last_contact;
    }
}
int first_turn_of_session = (eng->environment.turns_this_session == 0);

/* First turn: allow up to 30 days of evolution.  Subsequent turns:
 * 1-hour cap (matches original safety behavior). */
uint32_t cap_ms = first_turn_of_session
                ? (uint32_t)(30u * 86400u * 1000u)
                : (uint32_t)(3600u * 1000u);
if (delta > cap_ms) delta = cap_ms;

/* Fold wall-clock gap in for first-turn cases — state.last_update_time
 * doesn't survive cross-session, so the relation file's last_contact
 * is the truth source for real elapsed time. */
if (first_turn_of_session && real_gap_seconds > 3600u){
    uint64_t gap_ms_64 = (uint64_t)real_gap_seconds * 1000u;
    uint32_t gap_ms = (gap_ms_64 < cap_ms) ? (uint32_t)gap_ms_64 : cap_ms;
    if (gap_ms > delta) delta = gap_ms;
}
pe_decay_drives(eng, delta);
pe_decay_episodic(eng);
pe_repetition_decay(eng);

/* Schema decay scales with absence: 1 schema_tick per hour of gap,
 * capped at 10 days' worth.  This is what makes "hostility fades over
 * time" actually true across sessions. */
if (first_turn_of_session && real_gap_seconds > 3600u){
    int extra_ticks = (int)(real_gap_seconds / 3600u);
    if (extra_ticks > 240) extra_ticks = 240;
    for (int i = 0; i < extra_ticks; ++i) schema_tick(&eng->schema);
}
```

**Test:** drive Pretorius to high USER_HOSTILE schema (insult arc),
quit. Wait. Set `relation.last_contact` back 7 days (manually edit
`<hash>.bin` or wait for real time). Reopen. First-turn state should
show hostile decayed substantially.

---

### 1.2 Initiative beat — character takes the lead

**Why:** modern chatbots only react. Real conversation has people who
pivot to their own concerns. After N neutral turns, the character
should *initiate* instead of probe.

**Where:** `core/persona.h` (enum extension), `core/engine.c`
(streak counter + intent override).

**Code:**

```c
/* core/persona.h — extend intent enum */
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
    PE_INTENT_ATTEND,      /* V5: active listening (see 1.3) */
    PE_INTENT_CLARIFY,     /* V5: ambiguity probe (see 1.3) */
    PE_INTENT_PAUSE,       /* V5: strategic silence (see 1.7) */
    PE_INTENT_COUNT
};
```

```c
/* core/persona.h — add to NPCState */
uint8_t  neutral_streak;   /* consecutive input_class==0 turns */
uint16_t turns_since_unprompted_recall;  /* used by 1.5 */
uint16_t unresolved_threads[8];          /* used by 1.4 */
uint8_t  unresolved_count;
uint8_t  unresolved_head;
uint8_t  _pad_v5[2];       /* alignment */
```

```c
/* core/engine.c — between pe_classify_input and pe_select_intent */
if (eng->input_class == 0) {
    if (eng->state.neutral_streak < 255) eng->state.neutral_streak++;
} else {
    eng->state.neutral_streak = 0;
}
```

```c
/* core/engine.c — after pe_select_intent, before pe_build_plan */
/* V5: initiative override.  Stochastic so it doesn't feel mechanical;
 * RNG is the engine's deterministic xorshift so replays match. */
if (eng->state.neutral_streak >= 3 && eng->matched_group == 0xFFFF){
    if ((persona_rng_u32(&eng->state) & 0xFFu) < 80u){   /* ~31% */
        eng->state.current_intent = PE_INTENT_INITIATE;
        eng->state.neutral_streak = 0;
    }
}
```

**Cartridge baseline templates** (add to whichever file produces the
shared baseline; if no baseline file exists yet, add to compile_*.c
per character):

```c
T_add(tt, 0xFFFF, PE_INTENT_INITIATE, 60, -1000, 1000, -1,
      "There is something I have been turning over, {address}. {topic}.");
T_add(tt, 0xFFFF, PE_INTENT_INITIATE, 55, -1000, 1000, -1,
      "Before we drift further — what do you make of {topic}?");
T_add(tt, 0xFFFF, PE_INTENT_INITIATE, 50, -1000, 1000, -1,
      "{address}, may I change the subject? {topic} has been on my mind.");
T_add(tt, 0xFFFF, PE_INTENT_INITIATE, 50, -200, 1000, -1,
      "I keep coming back to {memory}.  Did I tell you the rest of it?");
```

**Test:** drive 4 consecutive neutral utterances ("ok", "mm", "yes",
"alright"). Verify turn 4 or 5 produces a template tagged
`intent=initiate` in `/state`.

---

### 1.3 Active-listening + clarifying intents

**Why:** rich user input doesn't always need a substantive reply.
Sometimes the right move is *"go on."* And ambiguous input should
get a clarifying question instead of a best-guess substantive reply.

**Where:** `core/engine.c` (intent override), baseline templates.

**Code:**

```c
/* core/engine.c — after the initiative-beat check */

/* V5: active-listening intent — long, emotionally-rich input with
 * no concrete group matched.  The character signals presence without
 * performing. */
size_t input_len = strlen(input_text);
int rich_input = (ev.arousal > 50) || (input_len > 80);
if (rich_input && eng->input_class == 0 && eng->matched_group == 0xFFFF
    && eng->state.current_intent != PE_INTENT_INITIATE){
    if ((persona_rng_u32(&eng->state) & 0xFFu) < 60u){
        eng->state.current_intent = PE_INTENT_ATTEND;
    }
}

/* V5: clarifying intent — ambiguous mid-length input with low
 * classifier confidence.  Ask, don't guess. */
int ambiguous = (eng->input_class == 0
              && eng->matched_group == 0xFFFF
              && input_len > 20 && input_len < 80
              && eng->state.current_intent != PE_INTENT_INITIATE
              && eng->state.current_intent != PE_INTENT_ATTEND);
if (ambiguous && eng->state.neutral_streak < 3){
    if ((persona_rng_u32(&eng->state) & 0xFFu) < 30u){
        eng->state.current_intent = PE_INTENT_CLARIFY;
    }
}
```

**Baseline templates** (universal — fits any cartridge):

```c
/* ATTEND — short, present, no performance */
T_add(tt, 0xFFFF, PE_INTENT_ATTEND, 60, -1000, 1000, -1, "Go on.");
T_add(tt, 0xFFFF, PE_INTENT_ATTEND, 60, -1000, 1000, -1, "I am listening.");
T_add(tt, 0xFFFF, PE_INTENT_ATTEND, 55, -1000, 1000, -1, "Mm. Tell me more.");
T_add(tt, 0xFFFF, PE_INTENT_ATTEND, 50, -1000, 1000, -1, "{address}.");
T_add(tt, 0xFFFF, PE_INTENT_ATTEND, 50, -1000, 1000, -1, "And then?");
T_add(tt, 0xFFFF, PE_INTENT_ATTEND, 45, -1000, 200, -1, "...");

/* CLARIFY — asks, doesn't guess */
T_add(tt, 0xFFFF, PE_INTENT_CLARIFY, 55, -1000, 1000, -1,
      "Say that more plainly, {address}?");
T_add(tt, 0xFFFF, PE_INTENT_CLARIFY, 55, -1000, 1000, -1,
      "What do you mean by that, exactly?");
T_add(tt, 0xFFFF, PE_INTENT_CLARIFY, 50, -1000, 1000, -1,
      "Is that a complaint or a confession?");
T_add(tt, 0xFFFF, PE_INTENT_CLARIFY, 50, -1000, 1000, -1,
      "I want to be sure I follow.  Say it again.");
```

**Test:** input a long ambiguous statement ("I don't know, it's just
been one of those days, you know?"). Verify the reply is short
(ATTEND) or asks back (CLARIFY), not a generic monologue.

---

### 1.4 Half-spoken thought + unresolved-thread bookmark

**Why:** trailing off creates curiosity that LLMs can't fake because
they don't track what they almost said. Sometimes the character
*decides not to share*; the thread is saved and surfaces later.

**Where:** `render/templates/dialogue.c` (style transform),
NPCState fields (already added in 1.2 batch).

**Code:**

```c
/* render/templates/dialogue.c — inside apply_style(), near the end */

/* V5: half-spoken thought — 4% chance to truncate mid-sentence and
 * log the topic as an unresolved thread.  Surfaces later via 1.6. */
if ((eng->identity.voice_flags & PE_VF_ALLOW_CONTRADICT)
    && !grounded_turn && !hostile_turn
    && (style_rng(&seed) % 100u) < 4u)
{
    size_t L = strlen(buf);
    size_t cut = 0;
    /* find a comma or period in the middle third of the reply */
    for (size_t i = L / 4; i < L * 3 / 4 && i < cap; ++i){
        if (buf[i] == ',' || buf[i] == '.'){ cut = i; break; }
    }
    if (cut > 8){
        const char *trail = "... no, never mind.";
        size_t trail_len = strlen(trail);
        if (cut + trail_len + 1 < cap){
            memcpy(buf + cut, trail, trail_len + 1);
        }
        /* log to unresolved_threads ring buffer */
        if (eng->plan.target_topic != 0xFFFF){
            int h = eng->state.unresolved_head;
            eng->state.unresolved_threads[h] = eng->plan.target_topic;
            eng->state.unresolved_head = (uint8_t)((h + 1) % 8);
            if (eng->state.unresolved_count < 8) eng->state.unresolved_count++;
        }
    }
}
```

```c
/* core/engine.c or a helper file — surface unresolved threads on
 * idle_probe or initiative beats.  Call from ps_idle_probe and from
 * the INITIATE intent's template-fill path. */

const char *unresolved_resurface(const Engine *eng,
                                 char *buf, size_t cap){
    if (eng->state.unresolved_count == 0) return NULL;
    /* pick the most recent (likely most relevant) */
    int idx = (int)(((unsigned)(eng->state.unresolved_head + 7u)) % 8u);
    uint16_t topic_id = eng->state.unresolved_threads[idx];
    if (topic_id == 0xFFFF) return NULL;
    const char *tn = "what I almost said";
    for (uint32_t i = 0; i < eng->topics.count; ++i){
        if (eng->topics.topics[i].id == topic_id){
            tn = eng->topics.topics[i].name; break;
        }
    }
    snprintf(buf, cap,
             "I still owe you the rest of what I almost said about %s.",
             tn);
    return buf;
}
```

**Test:** drive a 30-turn conversation with `PE_VF_ALLOW_CONTRADICT`
flag set. Roughly 1 in 25 replies should trail off with "... no,
never mind." Verify `unresolved_count` increments. Call idle_probe
later; verify resurface line references one of the logged topics.

---

### 1.5 Memory-surface beat

**Why:** old salient memories should occasionally surface unprompted.
Across a long session, exactly one "do you remember when..." beat
fires from the engine's side.

**Where:** `core/engine.c`, end of `persona_process_input`.

**Code:**

```c
/* core/engine.c — append near end of persona_process_input,
 * after the response is generated but before persona_save */

/* V5: memory-surface beat — one stochastic unprompted recall per
 * ~12 turns.  Boost retrieval_prob on the highest-salience stale
 * memory; pe_associative_recall picks it up next turn naturally. */
eng->state.turns_since_unprompted_recall++;
if (eng->state.turns_since_unprompted_recall >= 12
    && (persona_rng_u32(&eng->state) & 0xFFu) < 40u)
{
    MemoryNode *best = NULL;
    int best_score = 0;
    for (uint16_t i = 0; i < eng->memory.episodic_count; ++i){
        MemoryNode *m = &eng->memory.episodic[i];
        if (m->core_memory) continue;
        if (m->retrieval_prob > 100) continue;       /* recently used */
        int sal = (int)m->salience;
        int stale = 200 - (int)m->retrieval_prob;
        int score = sal * 2 + stale;
        if (score > best_score){ best_score = score; best = m; }
    }
    if (best){
        best->retrieval_prob = 240;   /* nearly certain to fire next turn */
        eng->state.turns_since_unprompted_recall = 0;
    }
}
```

**Test:** drive a 30-turn conversation. Across runs, ~2-3 memories
should surface unprompted (above the baseline recall). Verify
through state trace: a memory with low retrieval_prob in turn N
suddenly has high retrieval_prob in turn N+1.

---

### 1.6 Time-of-day today_state weighting

**Why:** a character who acts the same at 3 AM and 10 AM is a
chatbot. The wall clock should bias today_state selection.

**Where:** `core/today.c` (or wherever today_index is selected).

**Code (no cart format change — substring matching on existing labels):**

```c
/* core/today.c — replace existing pe_pick_today body or add as
 * pre-filter to it */

#include <time.h>

static int hour_now(void){
    time_t t = time(NULL);
    struct tm lt;
#if defined(_WIN32)
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    return lt.tm_hour;
}

/* Returns 4 if label matches the current time band, 1 otherwise.
 * Substring match is a pragmatic shortcut — works on existing
 * cartridges without adding a time_band field.  Eventually replace
 * with a TodayEntry.time_band_affinity bitmask. */
static int today_band_weight(const char *label, int hour){
    if (!label || !*label) return 1;
    int match = 0;
    if (hour >= 6 && hour < 10){
        match = (strstr(label, "composed") || strstr(label, "restless")
              || strstr(label, "convalescent"));
    } else if (hour >= 10 && hour < 14){
        match = (strstr(label, "lectur") || strstr(label, "work")
              || strstr(label, "expansive"));
    } else if (hour >= 14 && hour < 18){
        match = (strstr(label, "expansive") || strstr(label, "bright")
              || strstr(label, "grandiose"));
    } else if (hour >= 18 && hour < 22){
        match = (strstr(label, "tipsy") || strstr(label, "intimate")
              || strstr(label, "evening"));
    } else {
        match = (strstr(label, "theatrical") || strstr(label, "manic")
              || strstr(label, "drunk") || strstr(label, "paranoid"));
    }
    return match ? 4 : 1;
}

void pe_pick_today(Engine *eng){
    if (eng->todays.count == 0){ eng->state.today_index = 0; return; }
    int h = hour_now();
    uint32_t scores[PE_TODAY_MAX];
    uint32_t total = 0;
    for (uint32_t i = 0; i < eng->todays.count; ++i){
        scores[i] = (uint32_t)today_band_weight(
            eng->todays.entries[i].label, h);
        total += scores[i];
    }
    if (total == 0){ eng->state.today_index = 0; return; }
    uint32_t r = persona_rng_u32(&eng->state) % total;
    for (uint32_t i = 0; i < eng->todays.count; ++i){
        if (r < scores[i]){
            eng->state.today_index = (uint16_t)i;
            return;
        }
        r -= scores[i];
    }
    eng->state.today_index = 0;
}
```

**Test:** run the engine at 03:00 vs 14:00 against the same cartridge
+ seed. Observe distinct today_index selections. Pretorius should
trend "manic_fixation" / "drunk_brilliant" late at night and
"composed" / "expansive_evening" by day.

---

### 1.7 Strategic silence — PE_INTENT_PAUSE

**Why:** sometimes the right response is no response. The character
"steps away" or "doesn't trust itself to answer."

**Where:** intent enum (already added in 1.2 batch), engine.c override,
host.c reply rendering.

**Code:**

```c
/* core/engine.c — after the other intent overrides */

/* V5: pause intent — high acute_spike + recent insult = character
 * doesn't trust itself to answer; or extreme exhaustion = stepping away. */
if (eng->state.acute_spike < -500 && eng->input_class == 2){
    if ((persona_rng_u32(&eng->state) & 0xFFu) < 80u){
        eng->state.current_intent = PE_INTENT_PAUSE;
    }
}
if (eng->state.exhaustion > 850){
    if ((persona_rng_u32(&eng->state) & 0xFFu) < 100u){
        eng->state.current_intent = PE_INTENT_PAUSE;
    }
}
```

```c
/* render/templates/dialogue.c — at start of pe_generate_response */
if (eng->state.current_intent == PE_INTENT_PAUSE){
    /* return empty + a soft delay marker.  Host UI interprets empty
     * reply as "show typing indicator, then nothing".  State is still
     * advanced normally; only the surface text is suppressed. */
    out[0] = 0;
    return 0;
}
```

```c
/* bridges/persona_host.c — in method_chat: when reply is empty, emit a
 * special JSON variant the UI recognizes */
if (n == 0 && ctx->reply_scratch[0] == 0){
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"reply\":null,\"pause\":true,\"state\":%s}",
                    ctx->state_scratch);
}
```

**Test:** insult the character into deep negative spike. Verify next
reply is `{"reply":null,"pause":true,...}`. The UI shows the typing
indicator briefly, then stops.

---

## Section 2 — coherent v5 cartridge format bump

These four tricks need cartridge-side data. Bundle into one format
bump so cart-format-version stays manageable.

### 2.1 Identity extensions

```c
/* core/persona.h — append after the existing #defines */
#define PE_PREOCCUPATION_COUNT   3
#define PE_PREOCCUPATION_LEN     96
#define PE_RESUMPTION_BUCKETS    4   /* <2h, 2-24h, 1-7d, >7d */
#define PE_RESUMPTION_LEN        96
#define PE_WANT_COUNT            3
#define PE_WANT_NAME_LEN         32
#define PE_MILESTONE_COUNT       6
#define PE_MILESTONE_LEN         96
```

```c
/* core/persona.h — new struct for character wants (Identity.wants[]) */
typedef struct {
    char     name[PE_WANT_NAME_LEN];   /* "convince user the work matters" */
    uint16_t target_topic_id;           /* topic that engages this want */
    uint8_t  target_pattern_class;      /* 0..5; 0=any */
    uint8_t  intensity;                 /* 0..255; how hard the character pulls */
    uint16_t turns_since_engaged;       /* runtime — incremented each turn */
    uint16_t _pad;
} CharacterWant;
```

```c
/* core/persona.h — extend Identity struct.  Append at end; on-disk
 * layout grows.  Bump cart version.  Old carts still loadable if
 * cartridge.c handles version migration (zero-init the new fields). */
typedef struct {
    /* ... existing Identity fields up through expansions[] ... */

    /* V5 additions: */
    char          current_preoccupations[PE_PREOCCUPATION_COUNT][PE_PREOCCUPATION_LEN];
    char          resumption_lines[PE_RESUMPTION_BUCKETS][PE_RESUMPTION_LEN];
    CharacterWant wants[PE_WANT_COUNT];
    char          milestone_lines[PE_MILESTONE_COUNT][PE_MILESTONE_LEN];
    uint16_t      milestone_days[PE_MILESTONE_COUNT];  /* 0 = unused */
    uint16_t      _v5_pad[2];
} Identity;
```

```c
/* core/cartridge.h — bump version */
#define PE_CART_VERSION_V4   1
#define PE_CART_VERSION_V5   2
#define PE_CART_VERSION_CURRENT  PE_CART_VERSION_V5

/* core/cartridge.c — in identity_load:
 *
 * if (header.identity_section_size == sizeof(IdentityV4)){
 *     read IdentityV4, copy into IdentityV5 with zero V5 fields,
 *     log "loaded v4 cartridge; v5 fields will be empty"
 * } else if (header.identity_section_size == sizeof(Identity)){
 *     read normally
 * } else {
 *     return -1;
 * }
 */
```

### 2.2 Cartridge author surfaces

**For compile_pretorius.c:**

```c
/* current_preoccupations — what the character is "working on now" */
snprintf(id->current_preoccupations[0], PE_PREOCCUPATION_LEN,
         "perfecting the bell-jar — the third one keeps clouding");
snprintf(id->current_preoccupations[1], PE_PREOCCUPATION_LEN,
         "a small treatise on weather and consequence");
snprintf(id->current_preoccupations[2], PE_PREOCCUPATION_LEN,
         "convincing the gin to last until Tuesday");

/* resumption_lines[bucket] — first line after a gap of this length */
snprintf(id->resumption_lines[0], PE_RESUMPTION_LEN,  /* <2h */
         "Back so soon, {address}? Sit. The bottle is still cold.");
snprintf(id->resumption_lines[1], PE_RESUMPTION_LEN,  /* 2-24h */
         "{address}. A day, was it? I had begun to find the silence productive.");
snprintf(id->resumption_lines[2], PE_RESUMPTION_LEN,  /* 1-7d */
         "Mm. The prodigal returns. I assumed you had been arrested.");
snprintf(id->resumption_lines[3], PE_RESUMPTION_LEN,  /* >7d */
         "Has it been so long? The candles have been replaced twice without you.");

/* wants — what the character is pulling for in the conversation */
snprintf(id->wants[0].name, PE_WANT_NAME_LEN,
         "be witnessed at the work");
id->wants[0].target_topic_id = T_WORK;
id->wants[0].target_pattern_class = 1;   /* praise of the work satisfies */
id->wants[0].intensity = 200;

snprintf(id->wants[1].name, PE_WANT_NAME_LEN,
         "an audience for the lightning");
id->wants[1].target_topic_id = T_LIGHTNING;
id->wants[1].target_pattern_class = 0;
id->wants[1].intensity = 160;

snprintf(id->wants[2].name, PE_WANT_NAME_LEN,
         "henry to come back");
id->wants[2].target_topic_id = T_HENRY;
id->wants[2].target_pattern_class = 0;
id->wants[2].intensity = 130;

/* milestone_lines + days — special replies on relationship anniversaries */
id->milestone_days[0] = 1;
snprintf(id->milestone_lines[0], PE_MILESTONE_LEN,
         "A second visit. Encouraging.");
id->milestone_days[1] = 7;
snprintf(id->milestone_lines[1], PE_MILESTONE_LEN,
         "A week of you, {address}. Some patterns are forming.");
id->milestone_days[2] = 30;
snprintf(id->milestone_lines[2], PE_MILESTONE_LEN,
         "A month, my boy. I find I expect you.");
id->milestone_days[3] = 100;
snprintf(id->milestone_lines[3], PE_MILESTONE_LEN,
         "A hundred nights. I have stopped counting wrong.");
id->milestone_days[4] = 365;
snprintf(id->milestone_lines[4], PE_MILESTONE_LEN,
         "A year. The candles have been replaced eleven times, and yet here you are.");
id->milestone_days[5] = 1000;
snprintf(id->milestone_lines[5], PE_MILESTONE_LEN,
         "A thousand. Do you understand what you have done, {address}? You have made me reliable.");
```

### 2.3 Engine-side usage of the new fields

**Resumption lines** — in `engine.c`, first-turn-of-session block:

```c
/* V5: prepend a resumption line based on gap bucket */
if (first_turn_of_session && eng->relation.last_contact > 0){
    int bucket = -1;
    if      (real_gap_seconds < 7200u)        bucket = 0;  /* <2h */
    else if (real_gap_seconds < 86400u)       bucket = 1;  /* 2-24h */
    else if (real_gap_seconds < 7u * 86400u)  bucket = 2;  /* 1-7d */
    else                                       bucket = 3;  /* >7d */
    if (bucket >= 0
        && eng->identity.resumption_lines[bucket][0]){
        /* stash the line; pe_generate_response prepends it once */
        snprintf(eng->state.resumption_pending,
                 sizeof(eng->state.resumption_pending),
                 "%s", eng->identity.resumption_lines[bucket]);
    }
}
```

```c
/* render/templates/dialogue.c — at start of pe_generate_response,
 * after the early-return for PAUSE intent */
if (eng->state.resumption_pending[0]){
    /* prepend resumption line + newline to the normal reply, then
     * clear the pending slot so it only fires once. */
    char normal_reply[PE_TEMPLATE_TEXT];
    pe_generate_response_inner(eng, input, normal_reply, sizeof(normal_reply));
    snprintf(out, n, "%s  %s",
             eng->state.resumption_pending, normal_reply);
    eng->state.resumption_pending[0] = 0;
    return 0;
}
```

(NPCState needs a `char resumption_pending[128]` field for this.)

**Wants** — in `pe_select_intent` or `pe_select_goal`:

```c
/* V5: wants — find the highest-intensity want with the longest
 * turns_since_engaged.  Bias goal selection toward its target_topic. */
const CharacterWant *active_want = NULL;
int best_pull = 0;
for (int i = 0; i < PE_WANT_COUNT; ++i){
    const CharacterWant *w = &eng->identity.wants[i];
    if (!w->name[0]) continue;
    int pull = (int)w->intensity + (int)w->turns_since_engaged * 2;
    if (pull > best_pull){ best_pull = pull; active_want = w; }
}
if (active_want && best_pull > 500){
    /* bias topic selection */
    eng->plan.target_topic = active_want->target_topic_id;
    /* gentle pull toward INITIATE intent */
    if ((persona_rng_u32(&eng->state) & 0xFFu) < 60u){
        eng->state.current_intent = PE_INTENT_INITIATE;
    }
}

/* when user input engages the want (correct topic OR correct class),
 * reset its turns_since_engaged AND boost mood */
for (int i = 0; i < PE_WANT_COUNT; ++i){
    CharacterWant *w = &eng->identity.wants[i];
    if (!w->name[0]) continue;
    int engaged = 0;
    if (w->target_topic_id != 0xFFFF
        && eng->primary_topic == w->target_topic_id) engaged = 1;
    if (w->target_pattern_class != 0
        && eng->input_class == w->target_pattern_class) engaged = 1;
    if (engaged){
        w->turns_since_engaged = 0;
        eng->state.mood = (int16_t)pe_clamp16(
            (int)eng->state.mood + (int)w->intensity / 4, -1000, 1000);
    } else {
        if (w->turns_since_engaged < 0xFFFE) w->turns_since_engaged++;
    }
}
```

**Note**: `Identity.wants[].turns_since_engaged` is a mutable runtime
field stored inside a struct that's also serialized in identity.bin.
For determinism this needs care — either move the runtime counters
to a parallel `eng->state.want_runtime[PE_WANT_COUNT]` array, or
accept that identity.bin tracks per-session want fatigue (which is
arguably correct — the character's pull toward an unmet want should
persist across sessions).

Recommended: move to NPCState as `uint16_t want_turns_since_engaged[PE_WANT_COUNT]`. Keeps Identity pure-readonly.

**Milestone lines** — in `engine.c` first-turn block:

```c
/* V5: anniversary milestone */
if (first_turn_of_session && eng->relation.first_contact > 0){
    uint32_t age_days = (uint32_t)((time(NULL) - eng->relation.first_contact) / 86400u);
    for (int i = 0; i < PE_MILESTONE_COUNT; ++i){
        if (eng->identity.milestone_days[i] == 0) continue;
        if (age_days == eng->identity.milestone_days[i]
            && !(eng->state.milestones_seen & (1u << i))){
            /* mark seen, queue for prepend */
            eng->state.milestones_seen |= (1u << i);
            snprintf(eng->state.resumption_pending,
                     sizeof(eng->state.resumption_pending),
                     "%s", eng->identity.milestone_lines[i]);
            break;
        }
    }
}
```

NPCState needs `uint8_t milestones_seen` (bitmask, supports up to 8 milestones).

**Current preoccupations** — surface in WORKCHAT/STATUS replies:

```c
/* render/templates/dialogue.c — inside fill_slots, add a {preoccupation} placeholder */
else if (!strcmp(key, "preoccupation")){
    /* round-robin through the cartridge's current preoccupations,
     * deterministic per turn+seed */
    int n_set = 0;
    for (int i = 0; i < PE_PREOCCUPATION_COUNT; ++i){
        if (eng->identity.current_preoccupations[i][0]) n_set++;
    }
    if (n_set == 0){
        pos = append_str(out, n, pos, "the work");
    } else {
        uint32_t pick = (eng->state.today_seed ^ eng->state.turn_count)
                      % (uint32_t)n_set;
        int found = 0;
        for (int i = 0; i < PE_PREOCCUPATION_COUNT; ++i){
            if (eng->identity.current_preoccupations[i][0]){
                if (found == (int)pick){
                    pos = append_str(out, n, pos,
                        eng->identity.current_preoccupations[i]);
                    break;
                }
                found++;
            }
        }
    }
}
```

Then in cartridge templates: `T_add(tt, G_WORKCHAT, PE_INTENT_ANSWER, ..., "Mostly: {preoccupation}.");`

---

## Section 3 — implementation order

Smallest first, biggest last. Each can be committed and tested
independently. The 19-suite harness should stay green after each.

1. **1.1 Background time evolution** — engine.c only, no struct change.
   Strong first signal that the character "evolved" during absence.

2. **1.5 Memory-surface beat** — engine.c only, one new NPCState field
   (no cart change).

3. **1.6 Time-of-day today_state weighting** — today.c only, no cart change.

4. **1.2 Initiative beat** — intent enum extension + NPCState fields +
   3 baseline templates. State.bin auto-rebuilds; carts unchanged.

5. **1.3 Active-listening + clarify intents** — same enum batch as 1.2,
   plus ~10 baseline templates.

6. **1.7 Strategic silence (PAUSE)** — same enum batch + host.c JSON
   variant. Smallest UI-side change.

7. **1.4 Half-spoken thought + unresolved ring** — dialogue.c style
   transform, NPCState fields (already added in batch with 1.2).

8. **2.x cartridge format bump (v5)** — single coherent identity-struct
   extension. Migrate compile_pretorius.c + compile_kiki.c. Add
   cartridge.c version handling. Update Forge constants (the
   capacity-bump precedent from last week shows this is straightforward).

9. **2.3 Resumption lines + milestones** — needs 2.x done first.
   Add NPCState `resumption_pending[]` and `milestones_seen` bitmask.

10. **2.3 Wants** — biggest behavioral change; needs 2.x done first
    plus want_runtime array in NPCState.

After step 10, the character has: real cross-session evolution,
initiative, attending, clarifying, half-spoken thoughts that come
back later, time-of-day mood, day-N anniversary awareness, stated
ongoing projects, and an inner agenda pulling the conversation.

The total LOC across all ten tricks is roughly 600 lines of C plus
~40 lines of new template entries per cartridge. None of it
breaks the existing four V4 invariants; all of it strengthens the
"feels less waiting" perception that's the core ask.

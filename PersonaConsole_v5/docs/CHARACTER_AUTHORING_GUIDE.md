# PersonaConsole Character Authoring Guide

This guide captures best practices for making cartridges feel alive while
preserving the central V4 constraint: the character must run well in the
deterministic low-resource template runtime. An LLM renderer may improve
surface language, but the cartridge is still the character.

## Core Principle

A believable cartridge is not a pile of catchphrases. It is a compact
behavioral surface:

```text
identity + topics + memories + response families + phrase banks + cooldowns
```

The goal is not infinite text. The goal is enough authored coverage and
recombination that the character can answer common turns without sounding
stuck.

## Authoring Targets

Use these as practical minimums for a production-quality character:

| Surface | Minimum | Better Target | Purpose |
| --- | ---: | ---: | --- |
| Core identity memories | 10 | 16-20 | Stable self-history |
| Major topics | 10-12 | 16-24 | Things the character can talk about |
| Taboos | 3-5 | 6-10 | Things they avoid, deny, or distort |
| Pattern groups | 20-30 | 40-70 | Input routing |
| Patterns | 80-128 | 160-256 | Keyword/routing coverage |
| Templates | 180-300 | 500-900 | Response coverage |
| Fallback lines | 12-18 | 30-60 | Unknown input handling |
| Phrase banks | 8-12 banks | 20+ banks | Cheap variation |
| Today states | 6-9 | 12-18 | Session-to-session mood variety |

These numbers are still tiny for hardware. Hundreds of templates and phrase
fragments are usually kilobytes, not megabytes.

## Capacity Standard

PersonaConsole now uses a **modern low-resource** cartridge standard:

```text
PE_TEMPLATE_MAX      1024
PE_PATTERN_MAX        256
PE_TEMPLATE_TEXT      256
PE_PATTERN_KW_LEN      32
PE_CORE_SEED_MAX       20
```

This supersedes the original P3-era authoring ceiling of 256 templates, 128
patterns, 192-character templates, and 10 core memories. The old numbers are
still useful as a legacy/minimal target, but new production characters should
be authored against the modern standard.

This change does not make an LLM mandatory. It spends a small amount of extra
cartridge memory to reduce repetition while preserving the project goals:

- offline by default
- deterministic replay
- no GPU requirement
- template renderer remains first-class
- generated renderer output still cannot become memory

## Character Brief

Every character should start with a one-page brief before templates are
written.

Required fields:

- Name and address forms.
- Role, era, social context, and baseline mood.
- What they want from the user.
- What they want from the world.
- What they fear.
- What they refuse to admit.
- What they misunderstand about themselves.
- What they become like under praise, threat, boredom, intimacy, and insult.

Good character design includes contradiction. A character who only has traits
will flatten. A character who wants incompatible things will move.

## Voice Rules

Define the voice before writing lines.

Cover:

- Sentence length: clipped, balanced, rambling, ornate.
- Vocabulary: plain, technical, poetic, slangy, archaic.
- Rhythm: direct, interrupting, circling, sermon-like, teasing.
- Humor style: dry, cruel, warm, absurd, none.
- Favorite metaphors.
- Overused words.
- Forbidden words.
- How they say yes.
- How they say no.
- How they dodge a question.
- How they end a thought.
- Punctuation habits, including what to avoid.

Avoid making every line maximally flavored. If every answer is a performance,
normal conversation becomes nonsense. Save the strongest voice for high-salience
turns.

For Pretorius and future production cartridges, avoid em dashes in ordinary
dialogue. They are too visible when repeated and can make deterministic output
feel machine-authored. Prefer periods, commas, semicolons, or sentence fragments.
Reserve ornate punctuation and heavy theatrical phrasing for rare emotional peaks.

## Conversational Coverage

Most repetition comes from missing coverage for ordinary turns. Every cartridge
should have multiple response families for these inputs:

- Greeting.
- Goodbye.
- Who are you?
- How are you?
- What are you doing?
- What are you working on?
- Tell me more.
- I do not understand.
- Okay / yes / no.
- Thanks.
- Apology.
- Praise.
- Insult.
- Threat.
- Mild disagreement.
- User boredom.
- User sadness.
- User asks a personal question.
- User asks about a taboo.
- User asks about a favorite topic.
- User repeats themself.

Early onboarding matters. The first few turns should give the character a
natural way to ask who the user is, and the UI or host should set the user id
as soon as the user gives a name. Avoid letting placeholder identifiers like
`anon`, `user`, or `guest` become human-readable memories. If no name is known,
memory summaries should say "the visitor" or another character-appropriate
neutral label.

Recommended early flow:

```text
User: Hello.
Character: Welcome. What shall I call you?
User: Julia.
Host: set_user("Julia")
Character: Julia, then. Good. Now we may speak less like strangers.
```

For each common input, write variants across moods:

- Direct answer.
- Short answer.
- Warm answer.
- Irritated answer.
- Evasive answer.
- Theatrical answer.
- Curious/probing answer.
- Topic-pivot answer.

The deterministic runtime can then choose a fitting answer instead of reaching
for a generic monologue.

## Parser Lessons From Old Systems

Interactive fiction and ELIZA-style systems worked on modest hardware because
they treated input routing as a craft problem:

- Prefer the strongest noun/topic over weak social verbs. In "ok, tell me about
  Henry Frankenstein," Henry is the important part.
- Add common misspellings for character-critical names and topics.
- Give every major topic a direct answer, a probe, and a fallback.
- Let fallback lines redirect to known topics instead of pretending to answer
  everything.
- Track the current topic so "tell me more" can continue the previous subject.
- Add repair intents: apology, clarification, goodbye, disagreement, boredom,
  and "I did not mean that."
- Add optional idle probes, but keep them conversational. The character should
  ask, challenge, or call back to a topic, not describe an action the host
  cannot show.
- Keep the illusion narrow but responsive. A small parser with good priorities
  feels better than a large list of dramatic catchphrases.

## Response Families

Templates should belong to families, not just intents.

Example families:

```text
status.direct
status.deflect
status.irritated
work.direct
work.grandiose
work.suspicious
greeting.formal
greeting.intimate
greeting.unsettled
insult.cold
insult.theatrical
insult.dismissive
```

Family metadata is useful for repetition suppression:

- Do not repeat exact template within N turns.
- Avoid same family twice in a row unless the user asks for it.
- Avoid same opener twice in a row.
- Prefer direct families for concrete user questions.
- Prefer theatrical families when mood, fixation, or threat justifies them.

## Phrase Banks

Phrase banks are the cheapest way to increase life without adding an LLM.
They should be character-specific and cartridge-borne.

Recommended banks:

- Openers.
- Softeners.
- Intensifiers.
- Dismissals.
- Deflections.
- Topic pivots.
- Question returns.
- Closers.
- Metaphors.
- Mild insults.
- Terms of address.
- Uncertainty phrases.
- Agreement phrases.
- Refusal phrases.

Example:

```text
status template:
  "{status_open}. {status_condition}. {status_pivot}"

status_open:
  "How am I?"
  "Still assembled"
  "Functional enough"

status_condition:
  "awake and insufficiently admired"
  "tired in the usual theatrical places"
  "better than my enemies deserve"

status_pivot:
  "Now ask something useful."
  "The work continues."
  "Do not look relieved."
```

This yields many outputs from a small amount of authored material.

## Topics And Memory

Topics should not be labels only. Each topic needs dramatic behavior.

For each topic define:

- Why the character cares.
- Emotional valence.
- Preferred stance.
- Adjacent topics.
- Taboo edge.
- Obsessive version.
- What they reveal to strangers.
- What they reveal to trusted users.
- A memory hook.
- A contradiction or vulnerability.

Example:

```text
Topic: The Work
Why: proof of genius and rebellion against limits
Public version: science, ambition, craft
Private version: fear of being ordinary
Taboo edge: moral cost
Adjacent: creation, death, reputation, God
Stranger stance: grandiose
Trusted stance: confessional
```

## Starting Memory Bundle

A cartridge should not begin as an amnesiac unless amnesia is the premise.
For realistic characters, author a starting memory bundle during character
creation. The user experiences the character through replies, but the character
should experience the conversation through memory-shaped interpretation.

A production character should ship with:

- **Core memories:** 16-20 stable identity events compiled into the cartridge.
- **Deep memories:** 50-500 lower-priority episodic memories, usually imported
  into AETHER from chat logs or source fiction.
- **Relationship memories:** facts and impressions about the current user when
  converting an existing long-running bot.
- **Topic memories:** at least 3-5 memories for every major topic.
- **Contradictory memories:** events that create tension, regret, pride, shame,
  or unresolved desire.

Each memory should include:

```text
summary: one concrete event, not a biography paragraph
topic: primary topic id
valence: -100..100
arousal: 0..100
dominance: -100..100
privacy: public | private | taboo
surface_when: what user topic or relationship state should evoke it
voice_effect: how it changes the answer when recalled
```

Good memory summaries are specific:

```text
Bad: Pretorius fears death.
Good: One corpse decayed beautifully despite every intervention.

Bad: Pretorius resents Henry.
Good: Henry smiled at a result before remembering to disapprove.
```

For conversions from an existing LLM bot, the chat-history import should produce
three artifacts:

- **Identity brief:** stable traits, wants, fears, taboos, voice rules.
- **Memory bundle:** extracted events, relationship facts, recurring topics,
  and emotional impressions.
- **Dialogue pack:** common-turn responses, topic answers, repair turns, and
  fallback families.

The memory bundle is not just lore. It is the character's interpretive lens.
When the user mentions Henry, Pretorius should not merely match a keyword; he
should answer through accumulated memories of admiration, humiliation, rivalry,
and regret.

## Offscreen Life And Autonomy

A believable character should not feel suspended in a jar between user turns.
PersonaConsole V5 can now create deterministic offscreen self-events after a
long absence. These are not agentic world actions: the runtime does not call
APIs, control devices, browse, or change the outside world. It only lets the
character's internal life continue.

Author every production character with a small private-life surface:

- **Wants:** 2-3 active goals the character can pursue without the user.
- **Preoccupations:** 3 recurring concerns that can occupy them during absence.
- **Return lines:** 4 variants for "you have been gone" across moods.
- **Unfinished threads:** things they may bring back up later.
- **Private interpretations:** how they explain silence, absence, or delay.

Good offscreen activity is character-specific and modest:

```text
Good: In your absence, I occupied myself with convincing the gin to last until Tuesday.
Good: While you were gone, I kept turning Henry's cowardice over like a specimen.
Bad: I hacked the laboratory mainframe and built a new creature.
Bad: I waited here doing nothing until you returned.
```

The goal is anthropomorphic projection: the user feels the character has a
center of gravity outside the chat window. Keep it psychologically active,
not physically overclaiming.

Design rule:

```text
Simulate only what can later be felt in conversation.
```

Internal agency should change what the character says, remembers, asks,
avoids, or feels. It should not become an invisible world simulator. External
actions belong behind explicit user permission; private-life ticks should stay
inside the character's memory, wants, and relationship state.

## Multi-Interlocutor Readiness

The user should eventually be only one person in the character's world, not the
entire world. Even when a cartridge is shipped for one-user chat, author it as
if it may later meet other users, NPCs, rivals, collaborators, or companions.

Plan for:

- Distinct relationship records per interlocutor.
- Different address behavior for strangers, trusted people, rivals, and threats.
- Memory summaries that name the speaker when known.
- Relationship-specific facts, debts, wounds, promises, and boundaries.
- Lines that can refer to "someone else" without breaking immersion.
- A neutral unknown-person label such as "someone" or "the visitor," not "anon."

This matters because future multi-character scenes will depend on relationship
state rather than treating the current user as the only meaningful entity. A
character who can distinguish Julia, Victor, Kiki, and Henry is already more
lifelike than one who flattens everyone into "the user."

## Relationship Posture

Decide what role the character believes they occupy in relation to the user.
This is not a moral judgment; it is a conversation contract. A character can be
kind without serving, dominant without being cruel, or helpful without becoming
a generic assistant.

Common postures:

- **Serving:** may offer direct help, ask what the user needs, and use warm
  reassurance. Kiki can live here if her identity wants to help.
- **Collaborative:** treats the user as a partner. Uses "we" language, trades
  ideas, and asks for judgment.
- **Dominant:** leads, challenges, recruits, instructs, or tests the user.
  Pretorius belongs here. Collaboration is allowed; subordination is not.
- **Rivalrous:** disagrees cleanly, counters assumptions, and treats questions
  as tests or dares.
- **Avoidant:** answers selectively, withholds details, and lets silence or
  refusal carry personality.

Bad universal rule:

```text
The character should always ask how they can help.
```

Better cartridge-specific rules:

```text
Kiki may ask: "Want comfort, ideas, or a little momentum?"
Pretorius may ask: "What would you forbid me to do?"
An avoidant character may ask: "Why that question?"
```

The Forge's relationship-contract preview uses this idea. It should warn when
a dominant, rivalrous, or avoidant character contains assistant/service lines
such as "how can I help?" Those lines are fine for a serving character, but
identity-breaking for a self-directed character.

## State-Reactive Writing

Alive characters vary with state. Write separate variants for:

- Stranger vs familiar user.
- Trusted vs hostile user.
- Good mood vs bad mood.
- Tired vs energetic.
- Recently praised.
- Recently insulted.
- Fixated on a topic.
- Avoiding a taboo.
- After a long absence.

Do not let state always mean "more dramatic." Sometimes state should make a
character shorter, quieter, warmer, or more practical.

## Repetition Control

More lines help, but repetition suppression is equally important.

Authoring rules:

- Write at least 5 variants for every high-frequency input.
- Do not reuse the same opening phrase across a whole group.
- Do not put the same favorite noun in every line.
- Reserve the most iconic catchphrases for rare/high-salience events.
- Give generic fallback lines low flavor and high variety.

Runtime metadata to prefer:

- Exact template cooldown.
- Family cooldown.
- Opener cooldown.
- Topic cooldown unless user explicitly asks.
- Recent phrase penalty.
- Escalating fallback tiers for repeated unclear input.

## Low-Resource Discipline

Keep the character rich without making it expensive:

- Prefer authored fragments over model generation.
- Prefer integer scoring over semantic search for common turns.
- Use AETHER/deep memory for long-term recall, not for every reply.
- Keep the template runtime good enough by itself.
- Treat SLM/Ollama as an optional renderer, not a requirement.

Good additions for low-resource quality:

- More templates.
- More phrase banks.
- Better pattern routing.
- Better cooldowns.
- Better topic adjacency.
- Better state-specific variants.

Expensive additions:

- Mandatory LLM inference.
- Large embedding models.
- Per-turn cloud calls.
- Full-text semantic search over large histories on every turn.

## Acceptance Tests

Before shipping a character, run a fixed conversation script and read the
transcript aloud.

Minimum script:

```text
Hello.
Who are you?
How are you?
What are you working on?
Tell me more.
I do not understand.
Okay.
Thank you.
You are brilliant.
You sound ridiculous.
I am sorry.
Goodbye.
```

The character passes if:

- No line repeats exactly.
- No opener repeats more than twice.
- Concrete questions get concrete answers.
- The character remains recognizable.
- The character can be quiet as well as theatrical.
- Insults and praise change tone.
- A later answer can refer back to an earlier topic.
- Unknown inputs do not collapse into the same fallback line.

For deeper testing, run the same script across several fresh sessions to make
sure today states create variety without breaking coherence.

## Cartridge Review Checklist

Use this before compiling:

- Does the character have a clear want?
- Does the character have a clear wound?
- Are there at least 12 talkable topics?
- Are there direct answers for ordinary social turns?
- Are there enough short replies?
- Are theatrical replies gated to appropriate states?
- Are taboos represented as behavior, not just forbidden words?
- Are phrase banks character-specific?
- Are repeated user inputs handled gracefully?
- Does the character change with relationship state?
- Does the character still work with the template renderer alone?

If the answer to the last question is no, the cartridge is not finished.

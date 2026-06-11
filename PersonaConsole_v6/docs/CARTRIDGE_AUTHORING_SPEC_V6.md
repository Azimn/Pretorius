# Cartridge Authoring Spec V6

A V6 cartridge is compact character data loaded by the deterministic runtime. The engine owns state and decisions; the cartridge supplies identity and voice.

Required sections: identity.bin, drives.bin, today.bin, banks.bin, dialogue/patterns.bin, dialogue/templates.bin, dialogue/fallback.bin, dialogue/topics.bin, dialogue/goals.bin.

Identity fields: character_name, Big Five traits, voice_flags, address slots, obsessions, taboos, obsession_strength, core memory seeds, flourishes, expansions, current_preoccupations, resumption_lines, wants, milestone_days, milestone_lines.

Voice schema: set voice_flags conservatively. Use PE_VF_SARDONIC only for sardonic characters. Use verbosity bits to keep quiet and mentor cartridges from sounding theatrical.

Imported persona constraints: define required register, forbidden register, knowledge/voice split, relationship posture, romance eligibility, boundary style, attachment style, protected memories, and NPC/entity relationships. These are cartridge constraints expressed through symbolic fields, templates, and validation notes. The engine must not hardcode a specific imported character.

Topics: assign stable topic ids. Topic ids are symbolic anchors for memory, goals, open loops, recall modes, and contradiction handling.

Patterns: map keywords to topic_id, input_class, affect deltas, and template_group. Keep baseline social patterns inherited by default; cartridge patterns should add character-specific coverage.

Templates: each template has group, intent, rhetorical masks, stance masks, thresholds, and text. Template text may use slots such as {address}, {topic}, {memory}, {preoccupation}. Do not put engine logic in prose.

Fallback behavior: provide plain fallbacks for normal ambiguity. Fallbacks should match the cartridge tone and should not all become monologues.

Wants and preoccupations: wants are compact internal pressures with target_topic_id, target_pattern_class, and intensity. Preoccupations are short current-life phrases used by initiative and resumption.

Symbolic self fields: ideal_self, ought_self, and feared_self should be authored as symbolic ids once the cart struct exposes them. Until then, document the intended symbolic self axes in cartridge notes and keep dissonance behavior in engine state.

Memory seeds: core memories need summary, valence, arousal, dominance, topic_id, salience, privacy threshold, and memory_type. Seeds are character history, not current user facts.

Relation defaults: use relation dimensions conceptually: trust, threat, intimacy, resentment, obligation, dependency, envy, admiration, embarrassment. Disposition remains compatibility state.

Relationship posture: author the default posture explicitly. Examples include subordinate, equal, superior, rival, caretaker, mentor, family-like, romantic, former-romantic, adversarial, or NPC-only. Romance is supported as part of the social model, but it must be explicit cartridge data, not the default response to user affection.

Open-loop hooks: author templates for refusal, repair, deferred topics, and questions. The engine creates open loops symbolically; cartridge prose should give those loops a voice.

Contradiction defaults: define whether the character tends to ask clarification, challenge, accept correction, or defer update. Never blindly overwrite core memory.

Refusal defaults: every character needs a refusal style. Kind characters can refuse kindly. Pretorius can refuse imperiously. Quiet characters can refuse briefly.

Repair defaults: every character needs apology/repair language matching personality.

Cooldown expectations: avoid repeated openers, repeated direct address, repeated metaphors, and repeated topic callbacks inside 8-12 turns.

Micro Mode limits: stay within fixed caps for actors, memory slots, open loops, speech events, templates, and patterns. Do not require SLM, cloud, GPU, model download, or Node at runtime.

Missing optional fields: engine should degrade to baseline patterns/templates, neutral relation defaults, no extra wants, no resumption line, no milestone line, and generic fallback.

Validation rules: cartridge must load, lint cleanly, answer a scripted chat, close/reopen with memory state intact, and pass tone checks for its archetype.

Demo archetypes: Pretorius proves high-style gothic; Mira proves warmth; Cassian proves rivalry; Eli proves restraint; Marin proves practical mentorship; Kiki proves retro-vernacular modern competence, where the character may understand current technology but should explain it through late-80s/90s slang, social metaphors, and speech habits.

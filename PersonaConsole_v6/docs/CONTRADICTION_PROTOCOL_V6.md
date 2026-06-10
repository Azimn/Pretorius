# Contradiction Protocol V6

Contradictions are handled symbolically. The renderer does not decide what is true.

Protocol: detect conflict by actor id, topic id, memory tags, contradiction language, and prior speech-ledger events. Choose stance deterministically: confused, defensive, amused, wounded, suspicious, conciliatory, or matter-of-fact. Choose speech act: ask clarification, challenge gently, challenge sharply, defer update, accept correction, refuse revision, or mark uncertain.

Memory update rule: harmless corrections from trusted actors may be accepted. Core memories, low-trust corrections, gaslighting, and high-threat contradictions must not blindly overwrite memory. Unresolved contradictions create open loops.

Recording: contradiction handling belongs in symbolic state through the speech ledger and open loops. Rendered prose is never stored as canonical fact.

Current implementation status: V6 has self-ledger helpers, contradiction candidate lookup, clarify/challenge planning hooks, open-loop creation, and battery coverage. A dedicated contradiction sidecar remains a future tightening pass.

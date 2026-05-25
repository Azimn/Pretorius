# PersonaConsole V5 External Tester Guide

Thank you for testing PersonaConsole. This build is intentionally local and lightweight. It does not use a cloud AI service or a bundled LLM.

## What To Test First

1. Double-click `Run_Pretorius.cmd`.
2. Confirm your browser opens to `http://127.0.0.1:7777/`.
3. Send 10-20 normal chat messages.
4. Stop typing for a bit and see whether proactive speech feels alive or annoying.
5. Run `Stop_Server.cmd`.
6. Repeat once with `Run_Kiki.cmd`.

## Hardware Notes

Please tell us what machine you used:

- Windows version:
- CPU if known:
- RAM if known:
- Oldest or weakest machine tested:
- Did the browser feel slow?
- Did the character response feel instant, acceptable, or slow?

Expected default behavior:

- no GPU required
- no account required
- no internet required after you have this folder
- no model download
- tiny local runtime

## Conversation Prompts

Try these with Pretorius:

- Good morning, doctor.
- What are you working on?
- I think your work is immoral.
- Ask me something you actually want to know.
- Remember this phrase: the blue bell jar.
- What did I ask you to remember?
- You sound like you are performing.
- Can you speak plainly for a moment?
- I am going quiet now.

Try these with Kiki:

- Hi Kiki.
- How are you?
- Can you help me think through something?
- What are you curious about today?
- I need a softer answer.
- Ask me something fun.
- Remember that I like weird science.
- What should we talk about next time?

## What We Need Feedback On

Startup:

- Did the folder make sense?
- Did double-clicking the run script work?
- Did Windows show a warning that confused you?
- Did the browser open automatically?
- Could you stop the server?

Conversation:

- Did either character repeat exact lines?
- Did proactive speech feel alive, or did it interrupt?
- Did Pretorius feel too helpful or assistant-like?
- Did Kiki feel warm/helpful enough?
- Did either character ignore obvious things you said?
- Did memory callbacks feel impressive, wrong, or creepy?
- Did the character ask enough questions?
- Did replies feel too ornate, too short, or too canned?
- If you are comfortable sharing the exact chat, click `export transcript` in the sidebar and review the file before sending it.

Authoring:

- Could you open `Forge/forge.html`?
- Did the preflight suggestions make sense?
- Did relationship posture warnings make sense?
- Could you tell what to do next without instructions?

## Report Template

Copy this into your feedback message:

```text
PersonaConsole V5 tester notes

Machine:
Windows:
RAM:
Started by double-clicking:
Browser opened:
Stop script worked:

Pretorius felt:
Kiki felt:

Most alive moment:
Most fake moment:
Most confusing moment:

Repeated lines noticed:
Assistant-like lines noticed:
Memory/callback issues:
Proactive speech issues:
Forge/authoring issues:

Would you try another character? yes/no
Would you give this to a non-technical friend? yes/no
One thing to fix first:
```

## Privacy

This demo runs locally. It writes character state beside each cartridge under `characters/`. Do not send private conversations back unless you are comfortable sharing them.

`Collect_Diagnostics.cmd` does not include chat text. The web UI's `export transcript` button is separate and opt-in; it exports only the visible browser-session conversation when you click it.

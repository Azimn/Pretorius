# Cartridge Inspector

Standalone browser tool for checking a PersonaConsole `.cart` file before
sharing or installing it.

Open `cartridge_inspector.html` in a browser and drop a cartridge onto
the page. The inspection runs locally. Nothing is uploaded.

The Inspector mirrors the high-value `cartridge_lint` checks:

- cartridge integrity
- identity format, including V4 versus V5 identity layout
- topic, obsession, taboo, dialogue, memory, and voice coverage
- V5 proactivity hooks: wants, preoccupations, resumption lines, and
  milestones

It is intentionally read-only. The Forge prevents bad exports where it
can; the Inspector helps users verify any cartridge they receive from
someone else.

# Demo Cartridge Pack V6

The demo pack proves the runtime is not Pretorius-specific.

Characters: Pretorius is sardonic gothic; Mira is warm and emotionally available; Cassian Vale is resentful and status-sensitive; Eli Rowan is quiet and restrained; Marin Hale is practical and direct; Kiki is affectionate and helpful but constrained to older vernacular and comparisons even when discussing modern technology.

Build: `make cartridges`.

Test: `make v6_demo_pack_battery_run`.

Design rule: differences come from cartridge data: traits, voice flags, topics, wants, memories, templates, fallback lines, refusal style, and repair style. The engine must not hardcode these personalities.

Kiki is included because she tests a different failure mode than the generic friendly companion: the engine must preserve a narrow register constraint. She can know about modern systems, but her phrasing should reach for older metaphors, social habits, and comparisons instead of sounding like a present-day assistant.

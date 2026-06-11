# START HERE - PersonaConsole V6 Demo

1. Build: `make host cartridges`
2. Choose a cartridge:
   - `profiles/pretorius/pretorius.cart`
   - `profiles/kiki/kiki.cart`
   - `profiles/friendly/friendly.cart`
   - `profiles/rival/rival.cart`
   - `profiles/quiet/quiet.cart`
   - `profiles/mentor/mentor.cart`
3. Run: `./build/persona_host <cart-path> --stdio`
4. Send chat JSON lines, then close with `{ "method":"close" }`.
5. Reopen the same cart and ask what it remembers.
6. Run checks: `make v6_demo_pack_battery_run`, `make micro_replay_run`, `make micro_size_report`.
7. Build a shareable test folder: `make demo_package`.
8. Verify the shareable folder: `make release_check`.

Template mode is the default. No GPU, account, internet, cloud service, model download, or SLM is required. Optional local Ollama rendering is available in the packaged build, with deterministic template fallback.

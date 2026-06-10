# Micro Mode V6

Micro Mode is the lowest-hardware proof path. It builds template-only with PE_MICRO_MODE, PE_DISABLE_AETHER, and PE_DISABLE_SLM, using size optimization and linker garbage collection.

Micro Mode keeps speech ledger, relation dimensions, open loops, render audit, recall modes, and deterministic replay. It may disable heavyweight extras such as AETHER, Forge UI, Inspector UI, provider adapters, and debug tracing.

Run: `make micro_replay_run`.

Size report: `make micro_size_report`.

Rules: no required SLM, no cloud, no GPU, no model download, no Node at runtime, fixed memory caps, no renderer authority over identity.

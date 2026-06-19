# Persona Console V6 - Makefile
#
# V6 layout (production-intent reorganization):
#   core/                canonical identity runtime (engine.c, plan.c, etc.)
#   memory/              episodic + AETHER + relations + serialize
#   render/templates/    template renderer (deterministic, default)
#   render/slm/          tiny SLM renderer (v4 addition, stubbed)
#   render/providers/    future cloud/local model adapters
#   schema/              compressed identity interpretations
#   instrumentation/     state trace, replay debugger, emotional graph
#   bridges/             HTTP, FFI, stdio, web UI
#   tests/               unit + integration + replay tests
#   tools/               cartridge compilers, build_lm
#   profiles/            character cartridges (was characters/)
#   docs/                project documentation
#
# Builds (unchanged from v3.3):
#   build/libpersona.a         engine library
#   build/liblsh.a             SimHash + consolidation
#   build/libplasticity.a      n-gram LM + template mutator
#   build/libaether.a          long-term episodic store
#   build/libpersona_host.a    FFI + HTTP + JSON
#   build/persona_host         runtime binary
#   build/repl                 interactive harness
#   build/compile_cartridge    manifest-driven .cart packer
#   build/build_lm             offline LM builder

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra \
           -Wno-unused-parameter -Wno-missing-field-initializers \
           -Wno-format-truncation
CFLAGS  += -Icore -Imemory -Irender/templates -Ischema -Iinstrumentation \
           -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

MICRO_CFLAGS  := $(filter-out -O2,$(CFLAGS)) -Os -ffunction-sections -fdata-sections \
                 -DPE_MICRO_MODE -DPE_DISABLE_AETHER -DPE_DISABLE_SLM
MICRO_LDFLAGS := $(LDFLAGS) -Wl,--gc-sections

# ----- module source lists -----

CORE_SRC := \
    core/engine.c       \
    core/engine_clock.c \
    core/baseline_patterns.c \
    core/baseline_templates.c \
    core/baseline_reflections.c \
    core/topic_goal.c   \
    core/plan.c         \
    core/today.c        \
    core/rng.c          \
    core/cartridge.c    \
    core/intent.c       \
    core/identity.c     \
    core/environment.c

MEMORY_SRC := \
    memory/memory.c          \
    memory/relations.c       \
    memory/serialize.c       \
    memory/user_model.c      \
    memory/story.c           \
    memory/memory_firewall.c \
    memory/affect_curve.c    \
    memory/reflection.c      \
    memory/recall_plasticity.c \
    memory/sidecar.c         \
    memory/actor_index.c     \
    memory/speech_ledger.c   \
    memory/relation_dims.c   \
    memory/dissonance.c       \
    memory/open_loops.c       \
    memory/speech_habits.c

SCHEMA_SRC := \
    schema/schema_state.c

INSTRUMENTATION_SRC := \
    instrumentation/state_trace.c

RENDER_TEMPLATES_SRC := \
    render/templates/dialogue.c        \
    render/templates/voice.c           \
    render/templates/template_backend.c

RENDER_CORE_SRC := \
    render/render_backend.c \
    render/prompt_compiler.c \
    render/slm/slm_backend.c \
    render/providers/ollama_provider.c \
    render/providers/api_provider.c

MICRO_RENDER_CORE_SRC := \
    render/render_backend.c

ENGINE_SRC := $(CORE_SRC) $(MEMORY_SRC) $(SCHEMA_SRC) $(INSTRUMENTATION_SRC) \
              $(RENDER_TEMPLATES_SRC) $(RENDER_CORE_SRC)

MICRO_ENGINE_SRC := $(CORE_SRC) $(MEMORY_SRC) $(SCHEMA_SRC) $(INSTRUMENTATION_SRC) \
                    $(RENDER_TEMPLATES_SRC) $(MICRO_RENDER_CORE_SRC)

# LSH subsystem (SimHash + consolidation) ??? self-contained.
LSH_SRC := \
    memory/lsh_memory.c \
    memory/consolidate.c

# Plasticity subsystem (n-gram LM + template mutator) ??? self-contained.
PLAS_SRC := \
    render/templates/ngram_lm.c \
    render/templates/mutator.c

# AETHER long-term episodic store.  Links liblsh.a.  Self-contained.
AETHER_SRC := \
    memory/aether.c            \
    memory/aether_wal.c        \
    memory/aether_bucket.c     \
    memory/aether_consolidate.c

# PersonaHost ??? HTTP + JSON-over-stdio + FFI wrapper.
HOST_SRC := \
    bridges/persona_ffi.c \
    bridges/json.c        \
    bridges/http.c

# Object file rules ??? strip module prefix so build/ stays flat.
ENGINE_OBJ := $(patsubst %.c,build/%.o,$(notdir $(ENGINE_SRC)))
MICRO_ENGINE_OBJ := $(patsubst %.c,build/micro_%.o,$(notdir $(MICRO_ENGINE_SRC)))
LSH_OBJ    := $(patsubst %.c,build/%.o,$(notdir $(LSH_SRC)))
PLAS_OBJ   := $(patsubst %.c,build/%.o,$(notdir $(PLAS_SRC)))
MICRO_PLAS_OBJ := $(patsubst %.c,build/micro_%.o,$(notdir $(PLAS_SRC)))
AETHER_OBJ := $(patsubst %.c,build/%.o,$(notdir $(AETHER_SRC)))
HOST_OBJ   := $(patsubst %.c,build/host_%.o,$(notdir $(HOST_SRC)))
MICRO_HOST_OBJ := $(patsubst %.c,build/micro_host_%.o,$(notdir $(HOST_SRC)))

# Header dependency aggregates.  Listed here so the per-object rules can
# reference them without expanding into the 20+ headers across the tree.
CORE_HEADERS := \
    core/persona.h core/persona_internal.h core/cartridge.h \
    core/intent.h core/identity.h core/environment.h
MEMORY_HEADERS := \
    memory/aether.h memory/aether_internal.h memory/lsh_memory.h \
    memory/consolidate.h memory/reflection.h memory/recall_plasticity.h
MEMORY_HEADERS += memory/speech_habits.h
RENDER_HEADERS := \
    render/templates/ngram_lm.h render/templates/mutator.h

.PHONY: all clean character kiki_character cartridges test lsh_test lint lint_test \
        plasticity_test plasticity_run lm_build aether_test aether_run \
        host host_run host_test \
        forge_cart_test forge_bundle_test forge_dialogue_test \
        forge_archetypes_test forge_parsers_test inspector_test web_presence_test \
        demo_package release_check micro micro_size_report micro_replay_run \
        strict_check strict_gate v6_believability_battery_run v6_demo_pack_battery_run society_pair_probe v6_recall_mode_order_run v6_contradiction_protocol_run \
        v6_greeting_status_routing_run v6_greeting_status_exhaustion_run \
        v6_cold_open_memory_realism_run v6_cold_open_memory_surface_templates_run \
        v6_packet_experiment_run v6_audit_rewrite_run v6_memory_probe_overlay_run \
        v6_wrong_addressee_repair_run v6_lore_act_fallback_run \
        v7_api_run

all: build/libpersona.a build/liblsh.a build/libplasticity.a build/libaether.a \
     build/compile_cartridge build/compile_pretorius build/compile_kiki build/compile_demo_pack \
     build/repl build/build_lm build/cartridge_stats build/cartridge_lint \
     profiles/pretorius/voice.lm profiles/kiki/voice.lm \
     profiles/pretorius/pretorius.cart profiles/kiki/kiki.cart $(DEMO_CARTS)

build:
	@mkdir -p build

# ----- per-module compile rules -----
# Engine objects use all module include paths since engine.c reaches
# into AETHER, LSH, mutator, etc.
build/%.o: core/%.c $(CORE_HEADERS) $(MEMORY_HEADERS) $(RENDER_HEADERS) | build
	$(CC) $(CFLAGS) -c $< -o $@
build/%.o: memory/%.c $(CORE_HEADERS) $(MEMORY_HEADERS) $(RENDER_HEADERS) | build
	$(CC) $(CFLAGS) -c $< -o $@
build/%.o: schema/%.c $(CORE_HEADERS) $(MEMORY_HEADERS) | build
	$(CC) $(CFLAGS) -c $< -o $@
build/%.o: instrumentation/%.c instrumentation/state_trace.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/%.o: render/%.c $(CORE_HEADERS) $(MEMORY_HEADERS) $(RENDER_HEADERS) render/render_backend.h render/prompt_compiler.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/%.o: render/templates/%.c $(CORE_HEADERS) $(MEMORY_HEADERS) $(RENDER_HEADERS) render/render_backend.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/%.o: render/slm/%.c render/render_backend.h render/prompt_compiler.h render/providers/ollama_provider.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/%.o: render/providers/%.c render/providers/ollama_provider.h render/providers/api_provider.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# PersonaHost objects ??? same include set + the host's own headers.
build/host_%.o: bridges/%.c bridges/persona_ffi.h bridges/http.h bridges/json.h core/persona.h | build
	$(CC) $(CFLAGS) -Ibridges -c $< -o $@

# Micro objects use template-only runtime flags and distinct filenames so
# standard and micro builds can coexist in build/.
build/micro_%.o: core/%.c $(CORE_HEADERS) $(MEMORY_HEADERS) $(RENDER_HEADERS) | build
	$(CC) $(MICRO_CFLAGS) -c $< -o $@
build/micro_%.o: memory/%.c $(CORE_HEADERS) $(MEMORY_HEADERS) $(RENDER_HEADERS) | build
	$(CC) $(MICRO_CFLAGS) -c $< -o $@
build/micro_%.o: schema/%.c $(CORE_HEADERS) $(MEMORY_HEADERS) | build
	$(CC) $(MICRO_CFLAGS) -c $< -o $@
build/micro_%.o: instrumentation/%.c instrumentation/state_trace.h | build
	$(CC) $(MICRO_CFLAGS) -c $< -o $@
build/micro_%.o: render/%.c $(CORE_HEADERS) $(MEMORY_HEADERS) $(RENDER_HEADERS) render/render_backend.h render/prompt_compiler.h | build
	$(CC) $(MICRO_CFLAGS) -c $< -o $@
build/micro_%.o: render/templates/%.c $(CORE_HEADERS) $(MEMORY_HEADERS) $(RENDER_HEADERS) render/render_backend.h | build
	$(CC) $(MICRO_CFLAGS) -c $< -o $@
build/micro_host_%.o: bridges/%.c bridges/persona_ffi.h bridges/http.h bridges/json.h core/persona.h | build
	$(CC) $(MICRO_CFLAGS) -Ibridges -c $< -o $@

# ----- static libs -----
build/libpersona.a: $(ENGINE_OBJ)
	ar rcs $@ $^
build/liblsh.a: $(LSH_OBJ)
	ar rcs $@ $^
build/libplasticity.a: $(PLAS_OBJ)
	ar rcs $@ $^
build/libaether.a: $(AETHER_OBJ)
	ar rcs $@ $^
build/libpersona_host.a: $(HOST_OBJ)
	ar rcs $@ $^

build/libpersona_micro.a: $(MICRO_ENGINE_OBJ)
	ar rcs $@ $^
build/libplasticity_micro.a: $(MICRO_PLAS_OBJ)
	ar rcs $@ $^
build/libpersona_host_micro.a: $(MICRO_HOST_OBJ)
	ar rcs $@ $^

# ----- shared lib for Unity/Unreal/Godot -----
build/libpersona_host.so: $(HOST_SRC) $(ENGINE_SRC) $(PLAS_SRC) $(AETHER_SRC) $(LSH_SRC)
	$(CC) $(CFLAGS) -Ibridges -fPIC -shared $^ -o $@ $(LDFLAGS)

# ----- tools -----
build/compile_cartridge: tools/compile_cartridge.c core/cartridge.c $(CORE_HEADERS)
	$(CC) $(CFLAGS) tools/compile_cartridge.c core/cartridge.c -o $@ $(LDFLAGS)

build/cartridge_lint: tools/cartridge_lint.c core/cartridge.c $(CORE_HEADERS)
	$(CC) $(CFLAGS) tools/cartridge_lint.c core/cartridge.c -o $@ $(LDFLAGS)

CART ?= profiles/pretorius/pretorius.cart
lint: build/cartridge_lint
	./build/cartridge_lint $(CART)

build/compile_pretorius: tools/compile_pretorius.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tools/compile_pretorius.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

build/compile_kiki: tools/compile_kiki.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tools/compile_kiki.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

build/compile_demo_pack: tools/compile_demo_pack.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tools/compile_demo_pack.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

build/build_lm: tools/build_lm.c build/libplasticity.a
	$(CC) $(CFLAGS) tools/build_lm.c build/libplasticity.a -o $@ $(LDFLAGS)

build/cartridge_stats: tools/cartridge_stats.c build/libpersona.a
	$(CC) $(CFLAGS) tools/cartridge_stats.c build/libpersona.a -o $@ $(LDFLAGS)

cartridge_stats: build/cartridge_stats profiles/pretorius/pretorius.cart
	./build/cartridge_stats profiles/pretorius/pretorius.cart

# ----- profile bins + cart bundling -----

DEMO_SLUGS := friendly rival quiet mentor
DEMO_CARTS := $(foreach c,$(DEMO_SLUGS),profiles/$(c)/$(c).cart)

profiles/demo_pack/.stamp: build/compile_demo_pack
	./build/compile_demo_pack
	@mkdir -p profiles/demo_pack
	@touch $@

profiles/friendly/friendly.cart profiles/rival/rival.cart profiles/quiet/quiet.cart profiles/mentor/mentor.cart: profiles/demo_pack/.stamp build/compile_cartridge
	./build/compile_cartridge profiles/$(notdir $(basename $@))/manifest.json

profiles/pretorius/.bins.stamp: build/compile_pretorius
	./build/compile_pretorius profiles/pretorius
	@touch $@

profiles/kiki/.bins.stamp: build/compile_kiki
	@mkdir -p profiles/kiki
	./build/compile_kiki profiles/kiki
	@touch $@

profiles/pretorius/voice.lm: data/corpus.txt build/build_lm
	./build/build_lm data/corpus.txt 5 profiles/pretorius/voice.lm

profiles/kiki/voice.lm: data/corpus_kiki.txt build/build_lm
	@mkdir -p profiles/kiki
	./build/build_lm data/corpus_kiki.txt 5 profiles/kiki/voice.lm

lm_build: profiles/pretorius/voice.lm profiles/kiki/voice.lm

profiles/pretorius/pretorius.cart: profiles/pretorius/manifest.json profiles/pretorius/.bins.stamp build/compile_cartridge profiles/pretorius/voice.lm
	./build/compile_cartridge profiles/pretorius/manifest.json

profiles/kiki/kiki.cart: profiles/kiki/manifest.json profiles/kiki/.bins.stamp build/compile_cartridge profiles/kiki/voice.lm
	./build/compile_cartridge profiles/kiki/manifest.json

cartridges: profiles/pretorius/pretorius.cart profiles/kiki/kiki.cart $(DEMO_CARTS)
# ----- REPL + host -----
build/repl: tests/repl.c build/libpersona.a build/libplasticity.a build/liblsh.a build/libaether.a
	$(CC) $(CFLAGS) tests/repl.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

build/persona_host: bridges/persona_host.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) -Ibridges bridges/persona_host.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

build/persona_host_micro: bridges/persona_host.c build/libpersona_host_micro.a build/libpersona_micro.a build/libplasticity_micro.a build/liblsh.a
	$(CC) $(MICRO_CFLAGS) -Ibridges bridges/persona_host.c build/libpersona_host_micro.a build/libpersona_micro.a build/libplasticity_micro.a build/liblsh.a -o $@ $(MICRO_LDFLAGS)

micro: build/persona_host_micro profiles/pretorius/pretorius.cart
	@echo "Micro PersonaHost built: ./build/persona_host_micro"

micro_size_report: micro
	@echo "--- micro binary sections ---"
	@size build/persona_host_micro || true
	@echo "--- cartridge bytes ---"
	@wc -c profiles/pretorius/pretorius.cart
	@echo "--- mutable sidecar bytes (micro structs) ---"
	@printf "speech_events.bin max: %u bytes\n" 6208
	@printf "open_loops.bin max:    %u bytes\n" 576

micro_replay_run: micro
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 1; }
	PE_MICRO_HOST=build/persona_host_micro node tests/replay/micro_replay_test.js

host: build/persona_host build/libpersona_host.so
	@echo "PersonaHost built: ./build/persona_host"

build/host_test: tests/host_test.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) -Ibridges tests/host_test.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

host_test: build/host_test profiles/pretorius/pretorius.cart profiles/kiki/kiki.cart $(DEMO_CARTS)

host_run: host_test
	./build/host_test

demo_package: build/persona_host cartridges
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/package_demo.ps1

release_check: demo_package
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/release_check.ps1

# ----- subsystem tests -----
build/lsh_test: tests/lsh_test.c build/liblsh.a
	$(CC) $(CFLAGS) tests/lsh_test.c build/liblsh.a -o $@ $(LDFLAGS)
lsh_test: build/lsh_test
	./build/lsh_test

build/plasticity_test: tests/plasticity_test.c build/libplasticity.a
	$(CC) $(CFLAGS) tests/plasticity_test.c build/libplasticity.a -o $@ $(LDFLAGS)
plasticity_test: build/plasticity_test profiles/pretorius/voice.lm
plasticity_run: plasticity_test
	./build/plasticity_test profiles/pretorius/voice.lm

build/aether_test: tests/aether_test.c build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tests/aether_test.c build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)
aether_test: build/aether_test
aether_run: aether_test
	./build/aether_test

build/aether_pe_test: tests/aether_pe_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tests/aether_pe_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)
aether_pe_test: build/aether_pe_test
aether_pe_run: aether_pe_test character
	./build/aether_pe_test

build/voice_test: tests/voice_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tests/voice_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)
voice_test: build/voice_test
voice_run: voice_test character
	./build/voice_test

build/recall_mode_order_test: tests/replay/recall_mode_order_test.c build/libpersona.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tests/replay/recall_mode_order_test.c build/libpersona.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)
v6_recall_mode_order_run: build/recall_mode_order_test
	./build/recall_mode_order_test

# ----- Forge end-to-end tests (node) -----
forge_cart_test: build/persona_host build/cartridge_lint
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH ??? skipping"; exit 0; }
	node tests/forge_cart_test.js
forge_bundle_test: build/persona_host
	@command -v node  >/dev/null 2>&1 || { echo "node not in PATH";  exit 0; }
	@command -v unzip >/dev/null 2>&1 || { echo "unzip not in PATH"; exit 0; }
	node tests/forge_bundle_test.js
forge_dialogue_test: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/forge_dialogue_test.js
forge_archetypes_test: build/persona_host build/cartridge_lint
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/forge_archetypes_test.js
forge_parsers_test:
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/forge_parsers_test.js

inspector_test: profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/inspector_test.js

web_presence_test:
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/web_presence_test.js

# ----- V4 architecture-hardening tests -----
build/v4_modules_test: tests/replay/v4_modules_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tests/replay/v4_modules_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

v4_modules_run: build/v4_modules_test
	./build/v4_modules_test

v4_replay_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/replay_determinism_test.js

v4_ollama_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/ollama_provider_test.js

v7_api_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	@command -v curl >/dev/null 2>&1 || { echo "curl not in PATH"; exit 0; }
	node tests/replay/api_provider_test.js

v4_schema_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/schema_integration_test.js

build/v4_instr_test: tests/replay/instrumentation_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tests/replay/instrumentation_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

v4_instr_run: build/v4_instr_test
	./build/v4_instr_test

# Canonical continuity benchmark ??? measures the properties the AI
# industry does NOT measure: emotional persistence, schema stability,
# relational continuity, autobiographical consistency, renderer
# invariance.  Pass threshold = 60/100.
v4_continuity_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/continuity_benchmark.js

v4_continuity_kiki: build/persona_host profiles/kiki/kiki.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	CART_OBSESSIONS="physics,entropy,Sagan,Scully,babe,cosmos" \
	  node tests/continuity/continuity_benchmark.js profiles/kiki/kiki.cart

v4_idle_probe_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/idle_probe_test.js

v4_baseline_inheritance_run: build/persona_host profiles/kiki/kiki.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/baseline_inheritance_test.js

v4_background_time_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/background_time_test.js

v4_proactive_intents_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/proactive_intents_test.js

v4_time_of_day_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/time_of_day_today_test.js

v4_strategic_pause_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/strategic_pause_test.js

v4_unresolved_thread_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/unresolved_thread_test.js

v5_identity_surface_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/v5_identity_surface_test.js

v5_resumption_lines_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/resumption_lines_test.js

v5_want_pull_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/v5_want_pull_test.js

# V5: Reflective Memory Consolidation (Park et al. 2023 adaptation).
v4_reflection_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/reflection_test.js

v5_reflection_surface_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/reflection_surface_test.js

v5_conversation_rhythm_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/conversation_rhythm_test.js

v6_greeting_status_routing_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/greeting_status_routing_test.js

v6_greeting_status_exhaustion_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/greeting_status_exhaustion_probe.js

v6_cold_open_memory_realism_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/cold_open_memory_realism_test.js

v6_cold_open_memory_surface_templates_run: build/persona_host profiles/pretorius/pretorius.cart profiles/kiki/kiki.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/cold_open_memory_surface_templates_test.js

v5_offscreen_autonomy_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/offscreen_autonomy_test.js

v5_relationships_roster_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/relationships_roster_test.js

build/offscreen_memory_render_test: tests/offscreen_memory_render_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tests/offscreen_memory_render_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

v5_offscreen_memory_render_run: build/offscreen_memory_render_test
	./build/offscreen_memory_render_test

v5_milestone_catchup_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/milestone_catchup_test.js

v5_legacy_identity_migration_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/legacy_identity_migration_test.js

v4_adversarial_arcs: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/multi_arc_benchmark.js

# V5: Recall-Coupled Plasticity.
v4_recall_plasticity_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/recall_plasticity_test.js

# Behavioral Drift Index ??? measures identity divergence between renderers.
v4_drift_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/behavioral_drift.js

# Hallucination firewall ??? operationally proves renderer output cannot
# contaminate Layer 1 state, even under adversarial SLM injection.
v4_firewall_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/hallucination_firewall_test.js

v5_transcript_quality_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/transcript_quality_test.js

v5_believability_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/believability_trial.js

v6_believability_battery_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/believability_battery.js

v6_packet_experiment_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/packet_experiment_ab.js

v6_audit_rewrite_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/audit_constrained_rewrite_test.js

v6_memory_probe_overlay_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/memory_probe_overlay_test.js

v6_wrong_addressee_repair_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/audit_wrong_addressee_repair_test.js

v6_lore_act_fallback_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/audit_lore_act_fallback_test.js

v6_demo_pack_battery_run: build/persona_host cartridges
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/demo_pack_battery.js

society_pair_probe: build/persona_host profiles/pretorius/pretorius.cart profiles/kiki/kiki.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/society_pair_probe.js

lint_test: build/cartridge_lint profiles/pretorius/pretorius.cart profiles/kiki/kiki.cart $(DEMO_CARTS)
	./build/cartridge_lint profiles/pretorius/pretorius.cart
	./build/cartridge_lint profiles/kiki/kiki.cart
	@echo "--- lint: showcase carts clean ---"

# Aggregate JSON reports from continuity/drift runs into a comparison table.
# Drop reports into results/ named <kind>_<model>_<quant>.json.
v4_aggregate:
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/aggregate_results.js

v4_all_tests: v4_modules_run v4_replay_run v4_ollama_run v4_schema_run \
              v4_instr_run v4_continuity_run v4_idle_probe_run \
              v4_baseline_inheritance_run v4_background_time_run \
              v4_proactive_intents_run v4_time_of_day_run \
              v4_strategic_pause_run v4_unresolved_thread_run \
              v5_identity_surface_run v5_resumption_lines_run \
              v5_want_pull_run cartridge_stats v4_drift_run \
                v4_reflection_run v5_reflection_surface_run \
                v5_conversation_rhythm_run v6_greeting_status_routing_run \
                v6_cold_open_memory_realism_run v6_cold_open_memory_surface_templates_run \
                v5_offscreen_autonomy_run \
              v5_relationships_roster_run v5_offscreen_memory_render_run \
              v5_milestone_catchup_run v5_legacy_identity_migration_run \
              v4_adversarial_arcs v4_recall_plasticity_run inspector_test \
              web_presence_test v5_transcript_quality_run v5_believability_run \
              v5_clock_replay_run v5_actor_tagged_memory_run \
              v6_self_ledger_run v6_relation_dims_run \
              v6_dissonance_run v6_withhold_logging_run v6_recall_modes_run \
              v6_open_loops_run v6_speech_habits_run v4_firewall_run lint_test
	@echo "--- V4 tests: green ---"

# Preferred V5 aliases for the pre-push gate. Same dep list as v4_all_tests so
# there is no flag day; the rename is purely conceptual (the gate now also
# covers V5 believability, transcript quality, milestone catch-up, and the
# clock-pinned determinism test).
runtime_gate: v4_all_tests
v5_gate: v4_all_tests

strict_check:
	@test "$(STRICT_TESTS)" = "1" || { echo "Set STRICT_TESTS=1 for strict gate"; exit 2; }
	@command -v node >/dev/null 2>&1 || { echo "STRICT_TESTS: node missing"; exit 1; }
	@command -v unzip >/dev/null 2>&1 || { echo "STRICT_TESTS: unzip missing"; exit 1; }
	@command -v $(CC) >/dev/null 2>&1 || { echo "STRICT_TESTS: compiler '$(CC)' missing"; exit 1; }
	@test -f profiles/pretorius/pretorius.cart || { echo "STRICT_TESTS: Pretorius cart missing"; exit 1; }
	@test -f tests/replay/replay_determinism_test.js || { echo "STRICT_TESTS: replay test missing"; exit 1; }
	@test -f tests/continuity/believability_trial.js || { echo "STRICT_TESTS: believability test missing"; exit 1; }

strict_gate: strict_check micro_replay_run v4_replay_run v6_recall_modes_run \
             v6_open_loops_run v6_self_ledger_run v6_recall_mode_order_run v6_contradiction_protocol_run \
             v6_believability_battery_run v6_demo_pack_battery_run
	@echo "--- STRICT_TESTS gate: green ---"
# Clock-pinned replay: proves Layer 1 state is reproducible from the
# canonical five-tuple (cartridge, seed, WAL, inputs, clock).
v5_clock_replay_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/clock_pinned_replay_test.js

# Actor-tagged memory (Phase 2): commit-time tagging, sidecar persistence,
# and pinned-replay reproducibility of the actor_index.bin file.
v5_actor_tagged_memory_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/actor_tagged_memory_test.js

# V6 Phase 3: self-ledger (engine-authored speech events). Asserts the
# speech_events.bin sidecar accumulates per turn, is exposed in state JSON,
# survives close+reopen, and is byte-identical under pinned-clock replay.
v6_self_ledger_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/self_ledger_test.js

# V6 Phase 4: multi-dim relation. Asserts the relation_dims sidecar is
# loaded per actor, defaulted from disposition when absent, exposed in
# state JSON, persisted across restart, and isolated per actor.
v6_relation_dims_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/relation_dims_test.js

# V6 Phase 5b: typed dissonance (ideal/ought/feared). Asserts the
# accumulators move per speech-act class, decay per turn, persist
# across restart, and are exposed in state JSON.
v6_dissonance_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/dissonance_test.js

# V6 Phase 5c: refusal / withhold-reason logging. Asserts that
# refusal-class speech acts populate withhold_reason from canonical
# state, that the reason is exposed in state JSON, and that the
# default for non-withhold acts is "none".
v6_withhold_logging_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/withhold_logging_test.js

# V6 Phase 5d: recall-mode selection. Asserts that the planner picks a
# mode from canonical state each turn, that the mode is exposed in
# state JSON, that mode selection is deterministic under the pinned
# five-tuple, and that mode-specific scoring (mood-congruent /
# shame-avoidant) actually shifts which memories surface.
v6_recall_modes_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/recall_modes_test.js


v6_contradiction_protocol_run: build/persona_host profiles/friendly/friendly.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/contradiction_protocol_test.js

# V6 Phase 6: open loops / carried intentions. Asserts that a
# half-spoken unresolved thread writes open_loops.bin, exposes count in
# state JSON, survives restart, and replays byte-identically.
v6_open_loops_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/open_loops_test.js

# V6 Phase 6: speech habits. Asserts that the conversation-rhythm
# sidecar is written, exposed in state JSON, persisted, and replay-stable.
v6_speech_habits_run: build/persona_host profiles/pretorius/pretorius.cart
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/speech_habits_test.js

# ----- profile sanity drivers -----
character: profiles/pretorius/pretorius.cart
kiki_character: profiles/kiki/kiki.cart

test: all character
	@echo "--- Pretorius sanity test: 8 turns + dumps ---"
	@rm -f profiles/pretorius/state.bin profiles/pretorius/memory.bin
	@rm -rf profiles/pretorius/relations profiles/pretorius/aether
	@printf "Good evening, doctor.\nWho are you?\nTell me about your work.\nYou are a madman.\nI am not insulting you.\nWhat about gin?\nLet us drink to homunculi.\n:dump\n:plan\n:trace\n:quit\n" | ./build/repl profiles/pretorius/pretorius.cart victor

kiki_test: all kiki_character
	@echo "--- Kiki sanity test: 8 turns + dumps ---"
	@rm -f profiles/kiki/state.bin profiles/kiki/memory.bin
	@rm -f profiles/kiki/chapters.bin
	@rm -rf profiles/kiki/relations profiles/kiki/aether
	@printf "Hi Kiki!\nWho are you?\nExplain entropy.\nAre you real?\nYou are amazing.\nYou are just a chatbot.\nWhat about Punky Brewster?\nDo you ever get lonely?\n:dump\n:plan\n:trace\n:quit\n" | ./build/repl profiles/kiki/kiki.cart user

clean:
	rm -rf build
	rm -f profiles/pretorius/state.bin profiles/pretorius/memory.bin
	rm -f profiles/pretorius/pretorius.cart profiles/pretorius/.bins.stamp
	rm -f profiles/pretorius/chapters.bin
	rm -rf profiles/pretorius/relations
	rm -rf profiles/pretorius/aether
	rm -f profiles/kiki/state.bin profiles/kiki/memory.bin
	rm -f profiles/kiki/kiki.cart profiles/kiki/.bins.stamp
	rm -f profiles/kiki/chapters.bin
	rm -rf profiles/kiki/relations
	rm -rf profiles/kiki/aether

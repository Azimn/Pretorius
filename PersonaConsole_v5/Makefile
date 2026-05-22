# Persona Console V4 — Makefile
#
# V4 layout (production-intent reorganization):
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

# ----- module source lists -----

CORE_SRC := \
    core/engine.c       \
    core/baseline_patterns.c \
    core/baseline_templates.c \
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
    memory/affect_curve.c

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
    render/providers/ollama_provider.c

ENGINE_SRC := $(CORE_SRC) $(MEMORY_SRC) $(SCHEMA_SRC) $(INSTRUMENTATION_SRC) \
              $(RENDER_TEMPLATES_SRC) $(RENDER_CORE_SRC)

# LSH subsystem (SimHash + consolidation) — self-contained.
LSH_SRC := \
    memory/lsh_memory.c \
    memory/consolidate.c

# Plasticity subsystem (n-gram LM + template mutator) — self-contained.
PLAS_SRC := \
    render/templates/ngram_lm.c \
    render/templates/mutator.c

# AETHER long-term episodic store.  Links liblsh.a.  Self-contained.
AETHER_SRC := \
    memory/aether.c            \
    memory/aether_wal.c        \
    memory/aether_bucket.c     \
    memory/aether_consolidate.c

# PersonaHost — HTTP + JSON-over-stdio + FFI wrapper.
HOST_SRC := \
    bridges/persona_ffi.c \
    bridges/json.c        \
    bridges/http.c

# Object file rules — strip module prefix so build/ stays flat.
ENGINE_OBJ := $(patsubst %.c,build/%.o,$(notdir $(ENGINE_SRC)))
LSH_OBJ    := $(patsubst %.c,build/%.o,$(notdir $(LSH_SRC)))
PLAS_OBJ   := $(patsubst %.c,build/%.o,$(notdir $(PLAS_SRC)))
AETHER_OBJ := $(patsubst %.c,build/%.o,$(notdir $(AETHER_SRC)))
HOST_OBJ   := $(patsubst %.c,build/host_%.o,$(notdir $(HOST_SRC)))

# Header dependency aggregates.  Listed here so the per-object rules can
# reference them without expanding into the 20+ headers across the tree.
CORE_HEADERS := \
    core/persona.h core/persona_internal.h core/cartridge.h \
    core/intent.h core/identity.h core/environment.h
MEMORY_HEADERS := \
    memory/aether.h memory/aether_internal.h memory/lsh_memory.h \
    memory/consolidate.h
RENDER_HEADERS := \
    render/templates/ngram_lm.h render/templates/mutator.h

.PHONY: all clean character kiki_character cartridges test lsh_test \
        plasticity_test plasticity_run lm_build aether_test aether_run \
        host host_run host_test \
        forge_cart_test forge_bundle_test forge_dialogue_test \
        forge_archetypes_test forge_parsers_test

all: build/libpersona.a build/liblsh.a build/libplasticity.a build/libaether.a \
     build/compile_cartridge build/compile_pretorius build/compile_kiki \
     build/repl build/build_lm build/cartridge_stats \
     profiles/pretorius/voice.lm profiles/kiki/voice.lm \
     profiles/pretorius/pretorius.cart profiles/kiki/kiki.cart

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
build/%.o: render/providers/%.c render/providers/ollama_provider.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# PersonaHost objects — same include set + the host's own headers.
build/host_%.o: bridges/%.c bridges/persona_ffi.h bridges/http.h bridges/json.h core/persona.h | build
	$(CC) $(CFLAGS) -Ibridges -c $< -o $@

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

# ----- shared lib for Unity/Unreal/Godot -----
build/libpersona_host.so: $(HOST_SRC) $(ENGINE_SRC) $(PLAS_SRC) $(AETHER_SRC) $(LSH_SRC)
	$(CC) $(CFLAGS) -Ibridges -fPIC -shared $^ -o $@ $(LDFLAGS)

# ----- tools -----
build/compile_cartridge: tools/compile_cartridge.c core/cartridge.c $(CORE_HEADERS)
	$(CC) $(CFLAGS) tools/compile_cartridge.c core/cartridge.c -o $@ $(LDFLAGS)

build/compile_pretorius: tools/compile_pretorius.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tools/compile_pretorius.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

build/compile_kiki: tools/compile_kiki.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tools/compile_kiki.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

build/build_lm: tools/build_lm.c build/libplasticity.a
	$(CC) $(CFLAGS) tools/build_lm.c build/libplasticity.a -o $@ $(LDFLAGS)

build/cartridge_stats: tools/cartridge_stats.c build/libpersona.a
	$(CC) $(CFLAGS) tools/cartridge_stats.c build/libpersona.a -o $@ $(LDFLAGS)

cartridge_stats: build/cartridge_stats profiles/pretorius/pretorius.cart
	./build/cartridge_stats profiles/pretorius/pretorius.cart

# ----- profile bins + cart bundling -----
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

cartridges: profiles/pretorius/pretorius.cart profiles/kiki/kiki.cart

# ----- REPL + host -----
build/repl: tests/repl.c build/libpersona.a build/libplasticity.a build/liblsh.a build/libaether.a
	$(CC) $(CFLAGS) tests/repl.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

build/persona_host: bridges/persona_host.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) -Ibridges bridges/persona_host.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

host: build/persona_host build/libpersona_host.so
	@echo "PersonaHost built: ./build/persona_host"

build/host_test: tests/host_test.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) -Ibridges tests/host_test.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

host_test: build/host_test profiles/pretorius/pretorius.cart profiles/kiki/kiki.cart

host_run: host_test
	./build/host_test

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

# ----- Forge end-to-end tests (node) -----
forge_cart_test: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH — skipping"; exit 0; }
	node tests/forge_cart_test.js
forge_bundle_test: build/persona_host
	@command -v node  >/dev/null 2>&1 || { echo "node not in PATH";  exit 0; }
	@command -v unzip >/dev/null 2>&1 || { echo "unzip not in PATH"; exit 0; }
	node tests/forge_bundle_test.js
forge_dialogue_test: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/forge_dialogue_test.js
forge_archetypes_test: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/forge_archetypes_test.js
forge_parsers_test:
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/forge_parsers_test.js

# ----- V4 architecture-hardening tests -----
build/v4_modules_test: tests/replay/v4_modules_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tests/replay/v4_modules_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

v4_modules_run: build/v4_modules_test
	./build/v4_modules_test

v4_replay_run: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/replay_determinism_test.js

v4_ollama_run: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/ollama_provider_test.js

v4_schema_run: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/replay/schema_integration_test.js

build/v4_instr_test: tests/replay/instrumentation_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tests/replay/instrumentation_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

v4_instr_run: build/v4_instr_test
	./build/v4_instr_test

# Canonical continuity benchmark — measures the properties the AI
# industry does NOT measure: emotional persistence, schema stability,
# relational continuity, autobiographical consistency, renderer
# invariance.  Pass threshold = 60/100.
v4_continuity_run: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/continuity_benchmark.js

v4_continuity_kiki: build/persona_host
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

# Behavioral Drift Index — measures identity divergence between renderers.
v4_drift_run: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/behavioral_drift.js

# Hallucination firewall — operationally proves renderer output cannot
# contaminate Layer 1 state, even under adversarial SLM injection.
v4_firewall_run: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node tests/continuity/hallucination_firewall_test.js

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
              cartridge_stats v4_drift_run v4_firewall_run
	@echo "--- V4 tests: green ---"

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

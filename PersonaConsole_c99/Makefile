# Persona Engine — Makefile
# Builds:
#   build/libpersona.a         engine library
#   build/compile_cartridge    manifest-driven .cart packer
#   build/repl                 interactive harness

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra \
           -Wno-unused-parameter -Wno-missing-field-initializers \
           -Wno-format-truncation
CFLAGS  += -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

ENGINE_SRC := \
    src/engine.c        \
    src/memory.c        \
    src/topic_goal.c    \
    src/dialogue.c      \
    src/plan.c          \
    src/relations.c     \
    src/today.c         \
    src/serialize.c     \
    src/rng.c           \
    src/user_model.c    \
    src/story.c         \
    src/voice.c         \
    src/cartridge.c     \
    src/intent.c        \
    src/identity.c      \
    src/environment.c

# Semantic memory subsystem (LSH signatures + nightly consolidation).
# Self-contained: no dependency on persona.h / persona_internal.h.
LSH_SRC := \
    src/lsh_memory.c    \
    src/consolidate.c

# Plasticity subsystem (n-gram LM + template mutator).
# Self-contained: no dependency on persona.h.
PLAS_SRC := \
    src/ngram_lm.c      \
    src/mutator.c

# AETHER episodic storage subsystem.  Links against liblsh.a.
# Self-contained: no dependency on persona.h / persona_internal.h.
AETHER_SRC := \
    src/aether.c            \
    src/aether_wal.c        \
    src/aether_bucket.c     \
    src/aether_consolidate.c

# PersonaHost — HTTP server + JSON-over-stdio + FFI wrapper around the
# engine.  Exposes a stable C ABI (ps_open / ps_reply / ps_close / …)
# that downstream consumers (web UI, Unity/Unreal/Godot plugins) link
# against without ever seeing the internal Engine struct.
HOST_SRC := \
    host/persona_ffi.c      \
    host/json.c             \
    host/http.c

ENGINE_OBJ := $(ENGINE_SRC:src/%.c=build/%.o)
LSH_OBJ    := $(LSH_SRC:src/%.c=build/%.o)
PLAS_OBJ   := $(PLAS_SRC:src/%.c=build/%.o)
AETHER_OBJ := $(AETHER_SRC:src/%.c=build/%.o)
HOST_OBJ   := $(HOST_SRC:host/%.c=build/host_%.o)

.PHONY: all clean character kiki_character cartridges test lsh_test plasticity_test plasticity_run lm_build aether_test aether_run host host_run host_test

all: build/libpersona.a build/liblsh.a build/libplasticity.a build/libaether.a \
     build/compile_cartridge build/compile_pretorius build/compile_kiki \
     build/repl build/build_lm \
     characters/pretorius/voice.lm characters/kiki/voice.lm \
     characters/pretorius/pretorius.cart characters/kiki/kiki.cart

build:
	@mkdir -p build

# Engine objects depend on engine + aether + mutator headers (engine wires
# AETHER and the cartridge-borne BankRegistry).
$(ENGINE_OBJ): build/%.o: src/%.c include/persona.h include/persona_internal.h include/aether.h include/mutator.h include/cartridge.h include/intent.h include/identity.h include/environment.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# LSH subsystem objects depend only on the LSH headers.
$(LSH_OBJ): build/%.o: src/%.c include/lsh_memory.h include/consolidate.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# Plasticity subsystem objects depend on the plasticity headers.
$(PLAS_OBJ): build/%.o: src/%.c include/ngram_lm.h include/mutator.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# AETHER subsystem objects depend on aether headers + lsh_memory.h.
$(AETHER_OBJ): build/%.o: src/%.c include/aether.h include/aether_internal.h include/lsh_memory.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# PersonaHost objects — need access to persona.h for the FFI wrapper
# plus the host's own three headers.  Output name prefixed so it
# doesn't collide with engine objects (which build from src/%.c).
$(HOST_OBJ): build/host_%.o: host/%.c host/persona_ffi.h host/http.h host/json.h include/persona.h | build
	$(CC) $(CFLAGS) -Ihost -c $< -o $@

build/libpersona.a: $(ENGINE_OBJ)
	ar rcs $@ $^

build/liblsh.a: $(LSH_OBJ)
	ar rcs $@ $^

build/libplasticity.a: $(PLAS_OBJ)
	ar rcs $@ $^

build/libaether.a: $(AETHER_OBJ)
	ar rcs $@ $^

# libpersona_host.a — bundled FFI + HTTP + JSON for external linkers.
# A separate shared-library target (libpersona_host.so) follows further down.
build/libpersona_host.a: $(HOST_OBJ)
	ar rcs $@ $^

# libpersona_host.so — public FFI shared lib for Unity/Unreal/Godot.
# Bundles the engine and host code in one PIC build with all symbols
# the FFI surface needs to resolve.
build/libpersona_host.so: $(HOST_SRC) $(ENGINE_SRC) $(PLAS_SRC) $(AETHER_SRC) $(LSH_SRC)
	$(CC) $(CFLAGS) -Ihost -fPIC -shared $^ -o $@ $(LDFLAGS)

build/compile_cartridge: tools/compile_cartridge.c src/cartridge.c include/cartridge.h
	$(CC) $(CFLAGS) tools/compile_cartridge.c src/cartridge.c -o $@ $(LDFLAGS)

# Per-character data authoring tools — produce the raw section bins
# that compile_cartridge then packs into a .cart.  Two-stage pipeline:
#   compile_<name>  → identity.bin, drives.bin, today.bin, banks.bin,
#                     dialogue/{patterns,templates,fallback,topics,goals}.bin
#   compile_cartridge → <name>.cart  (bundles the bins + voice.lm)
build/compile_pretorius: tools/compile_pretorius.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tools/compile_pretorius.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

build/compile_kiki: tools/compile_kiki.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) tools/compile_kiki.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

# A stamp file marks "all per-character bins were rewritten to the
# current persona.h schema."  The .cart rule depends on the stamp so
# cartridge packing always sees fresh bins.
characters/pretorius/.bins.stamp: build/compile_pretorius
	./build/compile_pretorius characters/pretorius
	@touch $@

characters/kiki/.bins.stamp: build/compile_kiki
	@mkdir -p characters/kiki
	./build/compile_kiki characters/kiki
	@touch $@

build/build_lm: tools/build_lm.c build/libplasticity.a
	$(CC) $(CFLAGS) tools/build_lm.c build/libplasticity.a -o $@ $(LDFLAGS)

# Auto-build each cartridge's voice LM whenever its corpus changes.
# The engine loads <character_dir>/voice.lm — each cartridge owns its voice.
characters/pretorius/voice.lm: data/corpus.txt build/build_lm
	./build/build_lm data/corpus.txt 5 characters/pretorius/voice.lm

characters/kiki/voice.lm: data/corpus_kiki.txt build/build_lm
	@mkdir -p characters/kiki
	./build/build_lm data/corpus_kiki.txt 5 characters/kiki/voice.lm

lm_build: characters/pretorius/voice.lm characters/kiki/voice.lm

characters/pretorius/pretorius.cart: characters/pretorius/manifest.json characters/pretorius/.bins.stamp build/compile_cartridge characters/pretorius/voice.lm
	./build/compile_cartridge characters/pretorius/manifest.json

characters/kiki/kiki.cart: characters/kiki/manifest.json characters/kiki/.bins.stamp build/compile_cartridge characters/kiki/voice.lm
	./build/compile_cartridge characters/kiki/manifest.json

cartridges: characters/pretorius/pretorius.cart characters/kiki/kiki.cart

build/repl: test/repl.c build/libpersona.a build/libplasticity.a build/liblsh.a build/libaether.a
	$(CC) $(CFLAGS) test/repl.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

# PersonaHost binary — HTTP + JSON-over-stdio wrapper.
build/persona_host: host/persona_host.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) -Ihost host/persona_host.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

host: build/persona_host build/libpersona_host.so
	@echo "PersonaHost built: ./build/persona_host"

build/host_test: test/host_test.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) -Ihost test/host_test.c build/libpersona_host.a build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

host_test: build/host_test characters/pretorius/pretorius.cart characters/kiki/kiki.cart

host_run: host_test
	./build/host_test

build/lsh_test: test/lsh_test.c build/liblsh.a
	$(CC) $(CFLAGS) test/lsh_test.c build/liblsh.a -o $@ $(LDFLAGS)

lsh_test: build/lsh_test
	./build/lsh_test

build/plasticity_test: test/plasticity_test.c build/libplasticity.a
	$(CC) $(CFLAGS) test/plasticity_test.c build/libplasticity.a -o $@ $(LDFLAGS)

plasticity_test: build/plasticity_test characters/pretorius/voice.lm

plasticity_run: plasticity_test
	./build/plasticity_test characters/pretorius/voice.lm

build/aether_test: test/aether_test.c build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) test/aether_test.c build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

aether_test: build/aether_test

aether_run: aether_test
	./build/aether_test

# End-to-end PE+AETHER integration test (Step 3): drives pe_commit_memory +
# pe_associative_recall directly, verifies demotion-on-eviction and
# cold-promotion-on-recall.
build/aether_pe_test: test/aether_pe_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) test/aether_pe_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

aether_pe_test: build/aether_pe_test

aether_pe_run: aether_pe_test character
	./build/aether_pe_test

# v3.2 The Voice — counterfactual rerank tests.
build/voice_test: test/voice_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a
	$(CC) $(CFLAGS) test/voice_test.c build/libpersona.a build/libplasticity.a build/libaether.a build/liblsh.a -o $@ $(LDFLAGS)

voice_test: build/voice_test

voice_run: voice_test character
	./build/voice_test

# Forge end-to-end: extract JS from CartridgeForge/forge.html, build a
# .cart in node, load it through persona_host, assert it replies.
# Requires `node` on PATH.  Run after `make host`.
forge_cart_test: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH — skipping forge_cart_test"; exit 0; }
	node test/forge_cart_test.js

# Deep-memory bundle (cart + AETHER WAL packaged as .zip).
# Verifies long-chat-log preservation via the engine's cold-fallback path.
forge_bundle_test: build/persona_host
	@command -v node  >/dev/null 2>&1 || { echo "node not in PATH";  exit 0; }
	@command -v unzip >/dev/null 2>&1 || { echo "unzip not in PATH"; exit 0; }
	node test/forge_bundle_test.js

# Custom dialogue pack overrides reach the engine.
forge_dialogue_test: build/persona_host
	@command -v node >/dev/null 2>&1 || { echo "node not in PATH"; exit 0; }
	node test/forge_dialogue_test.js

character: characters/pretorius/pretorius.cart

kiki_character: characters/kiki/kiki.cart

test: all character
	@echo "--- Pretorius sanity test: 8 turns + dumps ---"
	@rm -f characters/pretorius/state.bin characters/pretorius/memory.bin
	@rm -rf characters/pretorius/relations characters/pretorius/aether
	@printf "Good evening, doctor.\nWho are you?\nTell me about your work.\nYou are a madman.\nI am not insulting you.\nWhat about gin?\nLet us drink to homunculi.\n:dump\n:plan\n:trace\n:quit\n" | ./build/repl characters/pretorius/pretorius.cart victor

kiki_test: all kiki_character
	@echo "--- Kiki sanity test: 8 turns + dumps ---"
	@rm -f characters/kiki/state.bin characters/kiki/memory.bin
	@rm -f characters/kiki/chapters.bin
	@rm -rf characters/kiki/relations characters/kiki/aether
	@printf "Hi Kiki!\nWho are you?\nExplain entropy.\nAre you real?\nYou are amazing.\nYou are just a chatbot.\nWhat about Punky Brewster?\nDo you ever get lonely?\n:dump\n:plan\n:trace\n:quit\n" | ./build/repl characters/kiki/kiki.cart user

clean:
	rm -rf build
	rm -f characters/pretorius/state.bin characters/pretorius/memory.bin
	rm -f characters/pretorius/pretorius.cart characters/pretorius/.bins.stamp
	rm -f characters/pretorius/chapters.bin
	rm -rf characters/pretorius/relations
	rm -rf characters/pretorius/aether
	rm -f characters/kiki/state.bin characters/kiki/memory.bin
	rm -f characters/kiki/kiki.cart characters/kiki/.bins.stamp
	rm -f characters/kiki/chapters.bin
	rm -rf characters/kiki/relations
	rm -rf characters/kiki/aether

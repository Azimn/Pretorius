# Persona Engine — Makefile
# Builds:
#   build/libpersona.a         engine library
#   build/compile_pretorius    offline character compiler
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
    src/story.c

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

ENGINE_OBJ := $(ENGINE_SRC:src/%.c=build/%.o)
LSH_OBJ    := $(LSH_SRC:src/%.c=build/%.o)
PLAS_OBJ   := $(PLAS_SRC:src/%.c=build/%.o)

.PHONY: all clean character test lsh_test plasticity_test plasticity_run lm_build

all: build/libpersona.a build/liblsh.a build/libplasticity.a \
     build/compile_pretorius build/compile_kiki build/repl build/build_lm \
     characters/pretorius/voice.lm characters/kiki/voice.lm

build:
	@mkdir -p build

# Engine objects depend on the engine headers (static pattern rule).
$(ENGINE_OBJ): build/%.o: src/%.c include/persona.h include/persona_internal.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# LSH subsystem objects depend only on the LSH headers.
$(LSH_OBJ): build/%.o: src/%.c include/lsh_memory.h include/consolidate.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# Plasticity subsystem objects depend on the plasticity headers.
$(PLAS_OBJ): build/%.o: src/%.c include/ngram_lm.h include/mutator.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/libpersona.a: $(ENGINE_OBJ)
	ar rcs $@ $^

build/liblsh.a: $(LSH_OBJ)
	ar rcs $@ $^

build/libplasticity.a: $(PLAS_OBJ)
	ar rcs $@ $^

build/compile_pretorius: tools/compile_pretorius.c build/libpersona.a build/libplasticity.a build/liblsh.a
	$(CC) $(CFLAGS) tools/compile_pretorius.c build/libpersona.a build/libplasticity.a build/liblsh.a -o $@ $(LDFLAGS)

build/compile_kiki: tools/compile_kiki.c build/libpersona.a build/libplasticity.a build/liblsh.a
	$(CC) $(CFLAGS) tools/compile_kiki.c build/libpersona.a build/libplasticity.a build/liblsh.a -o $@ $(LDFLAGS)

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

build/repl: test/repl.c build/libpersona.a build/libplasticity.a build/liblsh.a
	$(CC) $(CFLAGS) test/repl.c build/libpersona.a build/libplasticity.a build/liblsh.a -o $@ $(LDFLAGS)

build/lsh_test: test/lsh_test.c build/liblsh.a
	$(CC) $(CFLAGS) test/lsh_test.c build/liblsh.a -o $@ $(LDFLAGS)

lsh_test: build/lsh_test
	./build/lsh_test

build/plasticity_test: test/plasticity_test.c build/libplasticity.a
	$(CC) $(CFLAGS) test/plasticity_test.c build/libplasticity.a -o $@ $(LDFLAGS)

plasticity_test: build/plasticity_test characters/pretorius/voice.lm

plasticity_run: plasticity_test
	./build/plasticity_test characters/pretorius/voice.lm

character: build/compile_pretorius
	./build/compile_pretorius characters/pretorius

kiki_character: build/compile_kiki
	./build/compile_kiki characters/kiki

test: all character
	@echo "--- Pretorius sanity test: 8 turns + dumps ---"
	@rm -f characters/pretorius/state.bin characters/pretorius/memory.bin
	@rm -rf characters/pretorius/relations
	@printf "Good evening, doctor.\nWho are you?\nTell me about your work.\nYou are a madman.\nI am not insulting you.\nWhat about gin?\nLet us drink to homunculi.\n:dump\n:plan\n:trace\n:quit\n" | ./build/repl characters/pretorius victor

kiki_test: all kiki_character
	@echo "--- Kiki sanity test: 8 turns + dumps ---"
	@rm -f characters/kiki/state.bin characters/kiki/memory.bin
	@rm -f characters/kiki/chapters.bin
	@rm -rf characters/kiki/relations
	@printf "Hi Kiki!\nWho are you?\nExplain entropy.\nAre you real?\nYou are amazing.\nYou are just a chatbot.\nWhat about Punky Brewster?\nDo you ever get lonely?\n:dump\n:plan\n:trace\n:quit\n" | ./build/repl characters/kiki user

clean:
	rm -rf build
	rm -f characters/pretorius/state.bin characters/pretorius/memory.bin
	rm -f characters/pretorius/chapters.bin
	rm -rf characters/pretorius/relations
	rm -f characters/kiki/state.bin characters/kiki/memory.bin
	rm -f characters/kiki/chapters.bin
	rm -rf characters/kiki/relations

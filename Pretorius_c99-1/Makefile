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
    src/rng.c

# Semantic memory subsystem (LSH signatures + nightly consolidation).
# Self-contained: no dependency on persona.h / persona_internal.h.
LSH_SRC := \
    src/lsh_memory.c    \
    src/consolidate.c

ENGINE_OBJ := $(ENGINE_SRC:src/%.c=build/%.o)
LSH_OBJ    := $(LSH_SRC:src/%.c=build/%.o)

.PHONY: all clean character test lsh_test

all: build/libpersona.a build/liblsh.a build/compile_pretorius build/repl

build:
	@mkdir -p build

# Engine objects depend on the engine headers (static pattern rule).
$(ENGINE_OBJ): build/%.o: src/%.c include/persona.h include/persona_internal.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# LSH subsystem objects depend only on the LSH headers.
$(LSH_OBJ): build/%.o: src/%.c include/lsh_memory.h include/consolidate.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/libpersona.a: $(ENGINE_OBJ)
	ar rcs $@ $^

build/liblsh.a: $(LSH_OBJ)
	ar rcs $@ $^

build/compile_pretorius: tools/compile_pretorius.c build/libpersona.a
	$(CC) $(CFLAGS) tools/compile_pretorius.c build/libpersona.a -o $@ $(LDFLAGS)

build/repl: test/repl.c build/libpersona.a
	$(CC) $(CFLAGS) test/repl.c build/libpersona.a -o $@ $(LDFLAGS)

build/lsh_test: test/lsh_test.c build/liblsh.a
	$(CC) $(CFLAGS) test/lsh_test.c build/liblsh.a -o $@ $(LDFLAGS)

lsh_test: build/lsh_test
	./build/lsh_test

character: build/compile_pretorius
	./build/compile_pretorius characters/pretorius

test: all character
	@echo "--- sanity test: 8 turns + dumps ---"
	@rm -f characters/pretorius/state.bin characters/pretorius/memory.bin
	@rm -rf characters/pretorius/relations
	@printf "Good evening, doctor.\nWho are you?\nTell me about your work.\nYou are a madman.\nI am not insulting you.\nWhat about gin?\nLet us drink to homunculi.\n:dump\n:plan\n:trace\n:quit\n" | ./build/repl characters/pretorius victor

clean:
	rm -rf build
	rm -f characters/pretorius/state.bin characters/pretorius/memory.bin
	rm -rf characters/pretorius/relations

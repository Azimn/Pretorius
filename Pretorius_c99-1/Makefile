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

ENGINE_OBJ := $(ENGINE_SRC:src/%.c=build/%.o)

.PHONY: all clean character test

all: build/libpersona.a build/compile_pretorius build/repl

build:
	@mkdir -p build

build/%.o: src/%.c include/persona.h include/persona_internal.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/libpersona.a: $(ENGINE_OBJ)
	ar rcs $@ $^

build/compile_pretorius: tools/compile_pretorius.c build/libpersona.a
	$(CC) $(CFLAGS) tools/compile_pretorius.c build/libpersona.a -o $@ $(LDFLAGS)

build/repl: test/repl.c build/libpersona.a
	$(CC) $(CFLAGS) test/repl.c build/libpersona.a -o $@ $(LDFLAGS)

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

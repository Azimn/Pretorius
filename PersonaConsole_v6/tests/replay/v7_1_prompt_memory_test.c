/* v7_1_prompt_memory_test.c -- renderer expressiveness and memory reconstruction. */
#include "../../render/prompt_compiler.h"
#include "../../render/render_backend.h"
#include "../../core/persona.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(c) printf("ok:   %s\n",m); else { printf("FAIL: %s\n",m); fails++; } }while(0)
static int has(const char *s, const char *n){ return s && n && strstr(s,n)!=NULL; }
static int count_sub(const char *s, const char *n){ int c=0; size_t l=strlen(n); const char *p=s; while((p=strstr(p,n))){ c++; p+=l; } return c; }

static void clear_packet_env(void){
    unsetenv("V6_PACKET_MODE");
    unsetenv("PE_OLLAMA_MODEL");
    unsetenv("PE_API_URL");
}

static void seed_memory(Engine *eng, RetrievedMemorySet *mem){
    memset(eng, 0, sizeof(*eng));
    memset(mem, 0, sizeof(*mem));
    snprintf(eng->identity.character_name, sizeof(eng->identity.character_name), "Test Character");
    eng->relation_dims.trust = 500;
    eng->relation_dims.intimacy = 500;
    eng->relation_dims.resentment = 0;
    eng->relation_dims.admiration = 500;
    eng->state.mood = 0;
    for (int i=0;i<8;i++){
        MemoryNode *m=&eng->memory.episodic[i];
        m->id=(uint32_t)(i+1);
        m->salience=100;
        m->topic_id=0xFFFFu;
        snprintf(m->summary,sizeof(m->summary),"episodic memory %d",i+1);
        if (i<6) mem->episodic_idx[i]=i;
    }
    eng->memory.episodic_count=8;
    mem->episodic_count=8;
    for (int i=0;i<8 && i<PE_CORE_SEED_MAX;i++){
        MemoryNode *m=&eng->identity.core_memories_seed[i];
        m->id=(uint32_t)(100+i);
        m->salience=100;
        m->topic_id=0xFFFFu;
        snprintf(m->summary,sizeof(m->summary),"core memory %d",i+1);
        if (i<6) mem->core_idx[i]=i;
    }
    eng->identity.core_memory_count=8;
    mem->core_count=8;
}

int main(void){
    Engine eng;
    RetrievedMemorySet mem;
    RenderContext ctx;
    PromptCompilerConfig cfg;
    char prompt[PE_PROMPT_MAX_BYTES];
    int n;

    printf("--- V7.1 prompt and memory reconstruction ---\n");
    clear_packet_env();
    memset(&ctx,0,sizeof(ctx));
    prompt_compiler_default_config(&cfg);
    n=prompt_compile_with_input(&ctx,&cfg,"What does the room feel like?",prompt,sizeof(prompt));
    CHECK(n>0,"default prompt compiles");
    CHECK(!has(prompt,"Only people, places, and things named"),"closed-world existence rule removed");
    CHECK(has(prompt,"Sensory detail, atmosphere, present-moment thought"),"expressive detail is allowed as performance");
    CHECK(has(prompt,"Unnamed sensory experience and present impression are not restricted"),"unnamed present impression is unrestricted");
    cfg.render_profile=PE_SLM_PROFILE_TINY;
    n=prompt_compile_with_input(&ctx,&cfg,"How does it smell?",prompt,sizeof(prompt));
    CHECK(n>0,"tiny prompt compiles");
    CHECK(has(prompt,"use concrete language; ground at least one detail"),"tiny profile asks for grounded concrete language");
    CHECK(!has(prompt,"no atmospheric filler"),"tiny profile no longer bans atmosphere broadly");

    seed_memory(&eng,&mem);
    memset(&ctx,0,sizeof(ctx));
    ctx.npc=&eng; ctx.memories=&mem;
    prompt_compiler_default_config(&cfg);
    clear_packet_env();
    n=prompt_compile_with_input(&ctx,&cfg,"continue",prompt,sizeof(prompt));
    CHECK(n>0,"default memory prompt compiles");
    CHECK(count_sub(prompt,"recent=")==4,"default mode surfaces four recent memories");
    CHECK(count_sub(prompt,"core=")==4,"default mode surfaces four core memories");

    setenv("V6_PACKET_MODE","situation",1);
    n=prompt_compile_with_input(&ctx,&cfg,"continue",prompt,sizeof(prompt));
    unsetenv("V6_PACKET_MODE");
    CHECK(n>0,"situation memory prompt compiles");
    CHECK(count_sub(prompt,"=motive topic:")==6,"situation mode surfaces six motive memories");

    seed_memory(&eng,&mem);
    eng.relation_dims.resentment=720;
    eng.memory.episodic[0].emotion.valence=-60;
    eng.memory.episodic[0].emotion.arousal=70;
    n=prompt_compile_with_input(&ctx,&cfg,"continue",prompt,sizeof(prompt));
    CHECK(has(prompt,"recent[weight=raw]=episodic memory 1"),"resentful angry memory is tagged raw");

    seed_memory(&eng,&mem);
    eng.relation_dims.intimacy=760;
    eng.memory.episodic[0].emotion.valence=70;
    n=prompt_compile_with_input(&ctx,&cfg,"continue",prompt,sizeof(prompt));
    CHECK(has(prompt,"recent[weight=tender]=episodic memory 1"),"intimate joyful memory is tagged tender");

    seed_memory(&eng,&mem);
    n=prompt_compile_with_input(&ctx,&cfg,"continue",prompt,sizeof(prompt));
    CHECK(has(prompt,"recent=episodic memory 1"),"neutral memory remains plain label");
    CHECK(!has(prompt,"recent[weight="),"neutral memory has no weight tag");

    clear_packet_env();
    setenv("PE_OLLAMA_MODEL","qwen3:8b",1);
    CHECK(v6_packet_mode_is_situation()!=0,"Ollama model env defaults to situation mode");
    setenv("V6_PACKET_MODE","off",1);
    CHECK(v6_packet_mode_is_situation()==0,"V6_PACKET_MODE=off overrides model default");
    setenv("V6_PACKET_MODE","minimal",1);
    CHECK(v6_packet_mode_is_situation()==0,"V6_PACKET_MODE=minimal overrides model default");
    unsetenv("PE_OLLAMA_MODEL"); unsetenv("V6_PACKET_MODE"); unsetenv("PE_API_URL");
    CHECK(v6_packet_mode_is_situation()==0,"template/no-model default stays minimal");

    if (fails){ printf("FAILED -- %d V7.1 prompt assertion(s)\n",fails); return 1; }
    printf("PASSED -- V7.1 prompt expressiveness and memory reconstruction\n");
    return 0;
}

/* memory_firewall.c — V4 contamination barrier (impl).
 * See memory_firewall.h for the rationale.  TL;DR: no generated text
 * ever becomes memory.  Source attribution is mandatory.
 */
#include "memory_firewall.h"
#include <string.h>

static FirewallStats g_stats = {0, 0, 0, 0};

FirewallVerdict memory_firewall_check(const MemoryWriteTicket *ticket){
    if (!ticket){
        ++g_stats.denied_unattributed;
        return PE_FW_DENY_UNATTRIBUTED;
    }
    switch (ticket->source){
    case PE_SRC_USER_INPUT:
    case PE_SRC_SYSTEM_TICK:
    case PE_SRC_CARTRIDGE_LOAD:
        ++g_stats.allowed;
        return PE_FW_OK;
    case PE_SRC_RENDERER_OUTPUT:
        ++g_stats.denied_renderer;
        return PE_FW_DENY_RENDERER_WRITE;
    }
    ++g_stats.denied_unattributed;
    return PE_FW_DENY_UNATTRIBUTED;
}

FirewallVerdict memory_firewall_check_episodic(const MemoryWriteTicket *ticket){
    FirewallVerdict v = memory_firewall_check(ticket);
    if (v != PE_FW_OK) return v;
    /* Episodic memory writes must originate from user input.  System
     * ticks may decay episodic memory but never CREATE it. */
    if (ticket->source != PE_SRC_USER_INPUT){
        ++g_stats.denied_text_event;
        return PE_FW_DENY_TEXT_AS_EVENT;
    }
    return PE_FW_OK;
}

void memory_firewall_stats(FirewallStats *out){
    if (out) *out = g_stats;
}

void memory_firewall_reset_stats(void){
    memset(&g_stats, 0, sizeof(g_stats));
}

const char *memory_firewall_verdict_str(FirewallVerdict v){
    switch (v){
    case PE_FW_OK:                   return "ok";
    case PE_FW_DENY_RENDERER_WRITE:  return "deny:renderer-write";
    case PE_FW_DENY_TEXT_AS_EVENT:   return "deny:text-as-event";
    case PE_FW_DENY_UNATTRIBUTED:    return "deny:unattributed";
    }
    return "deny:unknown";
}

const char *memory_source_str(MemorySource s){
    switch (s){
    case PE_SRC_USER_INPUT:      return "user_input";
    case PE_SRC_SYSTEM_TICK:     return "system_tick";
    case PE_SRC_CARTRIDGE_LOAD:  return "cartridge_load";
    case PE_SRC_RENDERER_OUTPUT: return "renderer_output";
    }
    return "unknown";
}

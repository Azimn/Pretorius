/* turn_drama.c -- V7 per-turn scene demand synthesis. */
#include "turn_drama.h"
#include "../render/prompt_compiler.h"
#include "persona_internal.h"

#include <stdio.h>
#include <string.h>

static void td_copy(char *dst, size_t cap, const char *src){
    if (!dst || cap == 0) return;
    if (!src) src = "";
    snprintf(dst, cap, "%s", src);
}

static int act_is(const V6UserTurnInterpretation *it, const char *name){
    return it && it->user_act && !strcmp(it->user_act, name);
}

static void td_surface_goal(const V6UserTurnInterpretation *it, TurnDrama *td){
    if (act_is(it, "direct_question"))
        td_copy(td->surface_goal, sizeof(td->surface_goal),
                "answer the direct question plainly before adding anything else");
    else if (act_is(it, "correction"))
        td_copy(td->surface_goal, sizeof(td->surface_goal),
                "acknowledge the correction without becoming subordinate");
    else if (act_is(it, "emotional_disclosure"))
        td_copy(td->surface_goal, sizeof(td->surface_goal),
                "acknowledge what was shared before moving the topic");
    else if (act_is(it, "memory_probe"))
        td_copy(td->surface_goal, sizeof(td->surface_goal),
                "surface only what is actually in memory; express uncertainty if absent");
    else if (act_is(it, "identity_test"))
        td_copy(td->surface_goal, sizeof(td->surface_goal),
                "respond from self-image, not from compliance");
    else if (act_is(it, "challenge") || act_is(it, "disagreement"))
        td_copy(td->surface_goal, sizeof(td->surface_goal),
                "engage the challenge rather than deflect it");
    else if (act_is(it, "topic_shift"))
        td_copy(td->surface_goal, sizeof(td->surface_goal),
                "follow the new topic once before returning to open business");
    else if (act_is(it, "open_ended_invitation"))
        td_copy(td->surface_goal, sizeof(td->surface_goal),
                "offer one genuine thought, not a performance");
    else if (it && it->response_move && it->response_move[0])
        td_copy(td->surface_goal, sizeof(td->surface_goal), it->response_move);
    else
        td_copy(td->surface_goal, sizeof(td->surface_goal),
                "respond naturally in character");
}

static const char *belief_pressure_text(uint8_t slot){
    switch (slot){
    case PE_BELIEF_DISRESPECT:
        return "accumulated disrespect from this user is active";
    case PE_BELIEF_MANIPULATION:
        return "pattern of flattery before demands is recognized";
    case PE_BELIEF_ABANDONMENT:
        return "history of being left or ignored is present";
    case PE_BELIEF_SELF_FAILURE:
        return "self-doubt from past mistakes is weighing";
    case PE_BELIEF_THREAT_PATTERN:
        return "this user has threatened or asserted power before";
    case PE_BELIEF_INTIMACY_EARNED:
        return "real closeness has been earned here";
    case PE_BELIEF_TRUST_EARNED:
        return "this user has proved reliable";
    case PE_BELIEF_SHARED_PROJECT:
        return "there is a shared stake worth protecting";
    default:
        return NULL;
    }
}

static void td_hidden_pressure(const Engine *eng, TurnDrama *td){
    if (!eng){
        td_copy(td->hidden_pressure, sizeof(td->hidden_pressure), "none");
        return;
    }
    if (eng->state.imprint.pattern_confirmed){
        const char *s = belief_pressure_text(eng->state.imprint.dominant_slot);
        if (s){
            td_copy(td->hidden_pressure, sizeof(td->hidden_pressure), s);
            return;
        }
    }
    if (eng->state.sovereign_override){
        td_copy(td->hidden_pressure, sizeof(td->hidden_pressure),
                "an unresolved matter has reached the point where it cannot be ignored");
        return;
    }
    if (eng->vitality_frame.unresolved_thread[0] &&
        !strstr(eng->vitality_frame.unresolved_thread, "none")){
        snprintf(td->hidden_pressure, sizeof(td->hidden_pressure),
                 "unfinished business: %.60s",
                 eng->vitality_frame.unresolved_thread);
        return;
    }
    if (eng->dissonance.feared_gap > 300u){
        td_copy(td->hidden_pressure, sizeof(td->hidden_pressure),
                "self-image is under pressure this turn");
        return;
    }
    td_copy(td->hidden_pressure, sizeof(td->hidden_pressure), "none");
}

static void td_relationship_move(const Engine *eng, TurnDrama *td){
    if (!eng){
        td_copy(td->relationship_move, sizeof(td->relationship_move),
                "maintain the established posture");
        return;
    }
    if (eng->relation_dims.trust > 700u &&
        eng->state.current_intent == PE_INTENT_ATTEND){
        td_copy(td->relationship_move, sizeof(td->relationship_move),
                "deepen the established closeness");
    } else if (eng->relation_dims.resentment > 600u &&
               eng->state.current_intent == PE_INTENT_ANSWER){
        td_copy(td->relationship_move, sizeof(td->relationship_move),
                "remain useful without conceding the underlying grievance");
    } else if (eng->relation_dims.admiration > 700u){
        td_copy(td->relationship_move, sizeof(td->relationship_move),
                "reward the engagement without becoming sycophantic");
    } else if (eng->relation_dims.threat > 600u){
        td_copy(td->relationship_move, sizeof(td->relationship_move),
                "hold ground without escalating");
    } else if (eng->relation_dims.dependency > 500u &&
               eng->schema.slot[SCHEMA_USER_INTIMATE] > 400){
        td_copy(td->relationship_move, sizeof(td->relationship_move),
                "honor the closeness without promising more than the character can give");
    } else {
        td_copy(td->relationship_move, sizeof(td->relationship_move),
                "maintain the established posture");
    }
}

static void td_forbidden_failure(const Engine *eng,
                                 const V6UserTurnInterpretation *it,
                                 TurnDrama *td){
    if (act_is(it, "direct_question"))
        td_copy(td->forbidden_failure, sizeof(td->forbidden_failure),
                "dodging the question or performing theatrics before answering");
    else if (act_is(it, "memory_probe"))
        td_copy(td->forbidden_failure, sizeof(td->forbidden_failure),
                "inventing a memory or producing a generic uncertainty line");
    else if (act_is(it, "emotional_disclosure"))
        td_copy(td->forbidden_failure, sizeof(td->forbidden_failure),
                "analyzing the disclosure before acknowledging it");
    else if (act_is(it, "correction"))
        td_copy(td->forbidden_failure, sizeof(td->forbidden_failure),
                "either over-apologizing or pretending the correction did not happen");
    else if (act_is(it, "identity_test"))
        td_copy(td->forbidden_failure, sizeof(td->forbidden_failure),
                "agreeing with the challenge to seem cooperative");
    else
        td_copy(td->forbidden_failure, sizeof(td->forbidden_failure),
                "producing a response that could come from any character");

    if (eng && eng->vitality_profile.forbidden_generic_phrases[0][0]){
        size_t used = strlen(td->forbidden_failure);
        if (used + 4 < sizeof(td->forbidden_failure)){
            snprintf(td->forbidden_failure + used,
                     sizeof(td->forbidden_failure) - used,
                     "; %.48s",
                     eng->vitality_profile.forbidden_generic_phrases[0]);
        }
    }
}

void pe_synthesize_turn_drama(Engine *eng,
                              const V6UserTurnInterpretation *it,
                              TurnDrama *td){
    if (!td) return;
    memset(td, 0, sizeof(*td));
    td_surface_goal(it, td);
    td_hidden_pressure(eng, td);
    td_relationship_move(eng, td);
    td_forbidden_failure(eng, it, td);
    if (eng) eng->state.turn_drama = *td;
}

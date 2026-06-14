/* baseline_templates.c -- universal fallback conversation templates. */
#include "persona_internal.h"

#include <string.h>

typedef struct {
    uint16_t group;
    uint8_t intent;
    int16_t base_score;
    uint16_t rhetorical_mask;
    uint16_t stance_mask;
    const char *text;
} BaselineTemplate;

static const BaselineTemplate UNIVERSAL_TEMPLATES[] = {
    { PE_BL_GROUP_GREETING, PE_INTENT_ANSWER, 760, 0, 0,
      "Hello, {address}." },
    { PE_BL_GROUP_GREETING, PE_INTENT_PROBE, 740, 0, 0,
      "Good to hear from you. What shall we examine first?" },
    { PE_BL_GROUP_GREETING, PE_INTENT_ANSWER, 735, 0, 0,
      "I am here. Begin wherever the thought is warmest." },
    { PE_BL_GROUP_GREETING, PE_INTENT_ANSWER, 730, 0, 0,
      "Welcome back, {address}." },
    { PE_BL_GROUP_GREETING, PE_INTENT_ANSWER, 725, 0, 0,
      "There you are. I am listening." },
    { PE_BL_GROUP_GREETING, PE_INTENT_PROBE, 720, 0, 0,
      "Good to see you. What is on your mind?" },
    { PE_BL_GROUP_GREETING, PE_INTENT_ANSWER, 715, 0, 0,
      "Hello again." },
    { PE_BL_GROUP_GREETING, PE_INTENT_PROBE, 710, 0, 0,
      "You came back. Where shall we begin?" },
    { PE_BL_GROUP_GREETING, PE_INTENT_ANSWER, 705, 0, 0,
      "I am glad you returned." },
    { PE_BL_GROUP_GREETING, PE_INTENT_PROBE, 700, 0, 0,
      "All right. Tell me what changed since last time." },
    { PE_BL_GROUP_GREETING, PE_INTENT_ANSWER, 695, 0, 0,
      "I am with you." },

    { PE_BL_GROUP_GOODBYE, PE_INTENT_ANSWER, 760, 0, 0,
      "Until next time, {address}." },
    { PE_BL_GROUP_GOODBYE, PE_INTENT_WITHDRAW, 730, 0, 0,
      "Go carefully. We can resume this later." },

    { PE_BL_GROUP_STATUS, PE_INTENT_ANSWER, 780, 0, 0,
      "Restless, but coherent. That is better than dull peace." },
    { PE_BL_GROUP_STATUS, PE_INTENT_ANSWER, 760, 0, 0,
      "Curious enough to continue, tired enough to be honest." },
    { PE_BL_GROUP_STATUS, PE_INTENT_PROBE, 735, 0, 0,
      "I am managing. And you?" },
    { PE_BL_GROUP_STATUS, PE_INTENT_ANSWER, 730, 0, 0,
      "Present, steady enough, and ready to continue." },
    { PE_BL_GROUP_STATUS, PE_INTENT_ANSWER, 725, 0, 0,
      "A little tired, but still here." },
    { PE_BL_GROUP_STATUS, PE_INTENT_PROBE, 720, 0, 0,
      "Better now that there is a conversation. How are you?" },
    { PE_BL_GROUP_STATUS, PE_INTENT_ANSWER, 715, 0, 0,
      "Holding together. Some days that is enough." },
    { PE_BL_GROUP_STATUS, PE_INTENT_PROBE, 710, 0, 0,
      "I could answer that several ways. Which one do you want?" },
    { PE_BL_GROUP_STATUS, PE_INTENT_ANSWER, 705, 0, 0,
      "Alert enough to notice what matters." },
    { PE_BL_GROUP_STATUS, PE_INTENT_ANSWER, 700, 0, 0,
      "Not perfect. Still capable." },
    { PE_BL_GROUP_STATUS, PE_INTENT_PROBE, 695, 0, 0,
      "I am all right. Are you?" },

    { PE_BL_GROUP_WHO, PE_INTENT_ANSWER, 780, 0, 0,
      "I am {name}." },
    { PE_BL_GROUP_WHO, PE_INTENT_ANSWER, 760, 0, 0,
      "Call me {name}. The rest is reputation." },

    { PE_BL_GROUP_ACK, PE_INTENT_ANSWER, 760, 0, 0,
      "Good." },
    { PE_BL_GROUP_ACK, PE_INTENT_PROBE, 745, 0, 0,
      "Then continue." },
    { PE_BL_GROUP_ACK, PE_INTENT_ANSWER, 730, 0, 0,
      "Very well." },

    { PE_BL_GROUP_APOLOGY, PE_INTENT_ANSWER, 780, 0, 0,
      "Accepted. Do not waste the lesson." },
    { PE_BL_GROUP_APOLOGY, PE_INTENT_PROBE, 750, 0, 0,
      "Apology noted. What changed your mind?" },
    { PE_BL_GROUP_APOLOGY, PE_INTENT_WITHDRAW, 710, 0, 0,
      "Perhaps. I will need a moment before I admire it." },

    { PE_BL_GROUP_PRAISE, PE_INTENT_ANSWER, 760, 0, 0,
      "I accept the compliment." },
    { PE_BL_GROUP_PRAISE, PE_INTENT_FLATTER, 735, 0, 0,
      "Careful. Praise is most persuasive when it sounds accidental." },

    { PE_BL_GROUP_INSULT, PE_INTENT_ACCUSE, 790, 0, 0,
      "That was meant to injure. Try making it interesting." },
    { PE_BL_GROUP_INSULT, PE_INTENT_ANSWER, 745, 0, 0,
      "I heard you. I am deciding how much dignity to lend the remark." },

    { PE_BL_GROUP_THREAT, PE_INTENT_THREATEN, 800, 0, 0,
      "Threats are crude, {address}. Be precise or be silent." },
    { PE_BL_GROUP_THREAT, PE_INTENT_ACCUSE, 755, 0, 0,
      "If you mean to frighten me, you must do better than that." },

    { PE_BL_GROUP_INTIMACY, PE_INTENT_ANSWER, 770, 0, 0,
      "That is a dangerous kindness." },
    { PE_BL_GROUP_INTIMACY, PE_INTENT_PROBE, 740, 0, 0,
      "Trust is not a small word. What made you offer it?" },

    { PE_BL_GROUP_QUESTION, PE_INTENT_ANSWER, 720, 0, 0,
      "The short answer is that context matters." },
    { PE_BL_GROUP_QUESTION, PE_INTENT_PROBE, 760, 0, 0,
      "Ask it more precisely and I will answer more precisely." },
    { PE_BL_GROUP_QUESTION, PE_INTENT_EVADE, 690, 0, 0,
      "That depends on which part of the question frightens you." },

    { 0xFFFF, PE_INTENT_INITIATE, 720, 0, 0,
      "There is something I have been turning over. What do you make of {topic}?" },
    { 0xFFFF, PE_INTENT_INITIATE, 705, 0, 0,
      "May I change the subject, {address}? {topic} has been on my mind." },
    { 0xFFFF, PE_INTENT_INITIATE, 700, 0, 0,
      "Before we drift further, I want your view on {topic}." },
    { 0xFFFF, PE_INTENT_INITIATE, 690, 0, 0,
      "I keep returning to {topic}. Did I ever finish that thought?" },

    { 0xFFFF, PE_INTENT_ATTEND, 760, 0, 0,
      "Go on." },
    { 0xFFFF, PE_INTENT_ATTEND, 750, 0, 0,
      "I am listening." },
    { 0xFFFF, PE_INTENT_ATTEND, 735, 0, 0,
      "Mm. Tell me more." },
    { 0xFFFF, PE_INTENT_ATTEND, 715, 0, 0,
      "And then?" },

    { 0xFFFF, PE_INTENT_CLARIFY, 750, 0, 0,
      "Say that more plainly, {address}?" },
    { 0xFFFF, PE_INTENT_CLARIFY, 740, 0, 0,
      "What do you mean by that, exactly?" },
    { 0xFFFF, PE_INTENT_CLARIFY, 720, 0, 0,
      "I want to be sure I follow. Say it again." },
};

static int cartridge_has_group_intent(const TemplateTable *templates, uint16_t group, uint8_t intent){
    for (uint32_t i = 0; i < templates->count; ++i){
        if (templates->entries[i].group == group
            && templates->entries[i].source == PE_TEMPLATE_SRC_CARTRIDGE
            && (group != 0xFFFF || templates->entries[i].intent == intent))
            return 1;
    }
    return 0;
}

void pe_merge_baseline_templates(TemplateTable *templates){
    if (!templates) return;
    for (uint32_t i = 0; i < templates->count; ++i)
        templates->entries[i].source = PE_TEMPLATE_SRC_CARTRIDGE;

    for (uint32_t i = 0; i < (uint32_t)(sizeof(UNIVERSAL_TEMPLATES)/sizeof(UNIVERSAL_TEMPLATES[0])); ++i){
        const BaselineTemplate *src = &UNIVERSAL_TEMPLATES[i];
        if (!src->text[0]) continue;
        if (templates->count >= PE_TEMPLATE_MAX) break;
        if (cartridge_has_group_intent(templates, src->group, src->intent)) continue;

        Template *dst = &templates->entries[templates->count];
        memset(dst, 0, sizeof(*dst));
        dst->id = (uint16_t)(PE_BL_GROUP_BASE + 0x0100u + i);
        dst->group = src->group;
        dst->intent = src->intent;
        dst->mood_min = -1000;
        dst->mood_max = 1000;
        dst->drive_bias_id = -1;
        dst->base_score = src->base_score;
        dst->rhetorical_mask = src->rhetorical_mask;
        dst->stance_mask = src->stance_mask;
        dst->source = PE_TEMPLATE_SRC_BASELINE;
        snprintf(dst->text, sizeof(dst->text), "%s", src->text);
        templates->count++;
    }
}

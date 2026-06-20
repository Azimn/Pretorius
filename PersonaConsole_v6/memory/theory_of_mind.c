/* theory_of_mind.c -- compact per-actor ToM sidecar. */
#include "theory_of_mind.h"
#include "persona.h"
#include "persona_internal.h"

#include <stdio.h>
#include <string.h>

static int clamp_i(int v, int lo, int hi){
    return v < lo ? lo : (v > hi ? hi : v);
}

static int tom_path(const char *char_dir, uint32_t user_hash,
                    char *out, size_t n){
    char rel_dir[256];
    char base[256];
    snprintf(base, sizeof(base), "%.220s", char_dir ? char_dir : "");
    if (pe_path_join(rel_dir, sizeof(rel_dir), base, "relations") != 0) return -1;
    pe_mkdir_p(rel_dir);
    int w = snprintf(out, n, "%s/%08x.tom", rel_dir, user_hash);
    return (w > 0 && (size_t)w < n) ? 0 : -1;
}

void pe_tom_init_default(pe_tom_t *tom, uint32_t user_hash){
    if (!tom) return;
    memset(tom, 0, sizeof(*tom));
    tom->header.magic       = PE_TOM_MAGIC;
    tom->header.version     = PE_TOM_VERSION;
    tom->header.flags       = PE_SIDECAR_F_OPTIONAL;
    tom->header.entry_count = 1;
    tom->header.capacity    = 1;
    tom->user_hash          = user_hash;
    tom->believed_goal_topic = 0xFFFFu;
    tom->confidence         = 120;
}

int pe_tom_load(pe_tom_t *tom, const char *char_dir, uint32_t user_hash){
    if (!tom || !char_dir) return -1;
    pe_tom_init_default(tom, user_hash);
    char path[512];
    if (tom_path(char_dir, user_hash, path, sizeof(path)) != 0) return -1;
    pe_tom_t tmp;
    if (pe_read_file(path, &tmp, sizeof(tmp)) != 0) return 0;
    if (pe_sidecar_validate(&tmp.header, PE_TOM_MAGIC, 1, PE_TOM_VERSION) != PE_SIDECAR_OK)
        return 0;
    if (tmp.user_hash != user_hash) return 0;
    *tom = tmp;
    return 0;
}

int pe_tom_save(const pe_tom_t *tom, const char *char_dir){
    if (!tom || !char_dir || tom->user_hash == 0) return 0;
    char path[512];
    if (tom_path(char_dir, tom->user_hash, path, sizeof(path)) != 0) return -1;
    return pe_write_file_atomic(path, tom, sizeof(*tom));
}

static int16_t class_valence(uint8_t input_class, int16_t observed){
    if (observed != 0) return observed;
    switch (input_class){
    case 1: return 45;
    case 2: return -55;
    case 4: return -75;
    case 5: return 20;
    default: return 0;
    }
}

int pe_tom_detect_mismatch(const pe_tom_t *tom, uint8_t input_class,
                           int16_t observed_valence){
    if (!tom || tom->confidence < 160) return 0;
    int16_t obs = class_valence(input_class, observed_valence);
    if (tom->believed_valence > 30 && obs < -30) return 1;
    if (tom->believed_valence < -30 && obs > 30) return 1;
    if (tom->believed_arousal > 55 && input_class == 0) return 1;
    return 0;
}

void pe_tom_update_from_input(pe_tom_t *tom, uint8_t input_class,
                              int8_t arousal_pct,
                              int16_t observed_valence,
                              uint16_t topic_id,
                              int8_t trait_amp_suggestibility){
    if (!tom) return;
    int sug = trait_amp_suggestibility;
    if (sug < 0) sug = 0;
    if (sug > 100) sug = 100;
    int weight = 12 + sug / 4; /* 12..37 percent of current signal */
    int16_t obs_v = class_valence(input_class, observed_valence);
    int16_t obs_a = (int16_t)clamp_i(arousal_pct, 0, 100);
    int mismatch = pe_tom_detect_mismatch(tom, input_class, obs_v);

    tom->believed_valence = (int16_t)clamp_i(
        ((int)tom->believed_valence * (100 - weight) + (int)obs_v * weight) / 100,
        -100, 100);
    tom->believed_arousal = (int16_t)clamp_i(
        ((int)tom->believed_arousal * (100 - weight) + (int)obs_a * weight) / 100,
        0, 100);
    if (topic_id != 0xFFFFu) tom->believed_goal_topic = topic_id;
    tom->confidence = (uint16_t)clamp_i((int)tom->confidence + 18 - (mismatch ? 30 : 0), 0, 1000);
    tom->stale_turns = 0;
    if (mismatch) tom->mismatch_count = (uint16_t)clamp_i((int)tom->mismatch_count + 1, 0, 1000);
    else if (tom->mismatch_count > 0) tom->mismatch_count--;
}

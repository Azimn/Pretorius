/* vitality.h -- V7.2 cartridge-authored vitality profile support. */
#ifndef PE_VITALITY_H
#define PE_VITALITY_H

#include "persona.h"

#ifdef __cplusplus
extern "C" {
#endif

void pe_vitality_profile_init(VitalityProfile *vp);
void pe_vitality_frame_init(VitalityFrame *vf);
int  pe_vitality_load_optional(Engine *eng, const char *character_dir, int is_cart);
void pe_vitality_synthesize(Engine *eng);

int  pe_vitality_text_has_assistant_leak(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* PE_VITALITY_H */

/* sidecar.c — implementation of the sidecar header validator. */
#include "sidecar.h"

int pe_sidecar_validate(const pe_sidecar_header_t *h,
                        uint32_t expected_magic,
                        uint16_t min_version,
                        uint16_t max_version){
    if (!h) return PE_SIDECAR_E_NULL;
    if (h->magic   != expected_magic) return PE_SIDECAR_E_MAGIC;
    if (h->version <  min_version)    return PE_SIDECAR_E_TOO_OLD;
    if (h->version >  max_version)    return PE_SIDECAR_E_TOO_NEW;
    if (h->entry_count > h->capacity) return PE_SIDECAR_E_OVERFLOW;
    return PE_SIDECAR_OK;
}

int pe_sidecar_is_forward_incompatible(int validate_result){
    return validate_result == PE_SIDECAR_E_TOO_NEW;
}

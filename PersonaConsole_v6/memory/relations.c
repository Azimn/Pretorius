/* relations.c — per-user relation files + V4 schema persistence + V6
 * multi-dim relation persistence. */
#include "persona.h"
#include "persona_internal.h"
#include "engine_clock.h"
#include "relation_dims.h"
#include "theory_of_mind.h"
#include <stdio.h>
#include <string.h>

static int relation_path(Engine *eng, uint32_t hash, char *out, size_t n){
    char rel_dir[256];
    char base[256];
    snprintf(base, sizeof(base), "%.220s", eng->char_dir);
    if (pe_path_join(rel_dir, sizeof(rel_dir), base, "relations") != 0) return -1;
    pe_mkdir_p(rel_dir);
    int w = snprintf(out, n, "%s/%08x.bin", rel_dir, hash);
    return (w > 0 && (size_t)w < n) ? 0 : -1;
}

static int schema_path(Engine *eng, uint32_t hash, char *out, size_t n){
    char rel_dir[256];
    char base[256];
    snprintf(base, sizeof(base), "%.220s", eng->char_dir);
    if (pe_path_join(rel_dir, sizeof(rel_dir), base, "relations") != 0) return -1;
    int w = snprintf(out, n, "%s/%08x.schema", rel_dir, hash);
    return (w > 0 && (size_t)w < n) ? 0 : -1;
}

int pe_load_relation(Engine *eng, const char *user_id){
    if (!user_id || !*user_id || !strcmp(user_id, "anon")) user_id = "Someone";
    uint32_t h = persona_hash(user_id);
    char path[512];
    relation_path(eng, h, path, sizeof(path));

    if (eng->state.user_id_hash == h && eng->relation.user_hash == h)
        return 0;  /* same user, already loaded */

    /* save outgoing relation first */
    if (eng->relation.user_hash) pe_save_relation(eng);

    memset(&eng->relation, 0, sizeof(eng->relation));
    if (pe_read_file(path, &eng->relation, sizeof(Relation)) != 0){
        /* new acquaintance */
        eng->relation.user_hash = h;
        eng->relation.disposition = 500;
        eng->relation.tags = PE_TAG_STRANGER;
        eng->relation.first_contact = pe_clock_now_s();
        eng->relation.last_contact  = eng->relation.first_contact;
        size_t L = strlen(user_id);
        if (L >= PE_NAME_LEN) L = PE_NAME_LEN - 1;
        memcpy(eng->relation.known_as, user_id, L);
        eng->relation.known_as[L] = 0;
    } else {
        /* disposition decays 1 pt per real day of no contact */
        uint32_t now_s = pe_clock_now_s();
        uint32_t days = (now_s > eng->relation.last_contact)
                      ? (now_s - eng->relation.last_contact) / 86400u : 0;
        int v = eng->relation.disposition - (int)days;
        eng->relation.disposition = pe_clamp16(v, 0, 1000);
    }
    eng->state.user_id_hash = h;
    eng->state.trust_user   = eng->relation.disposition; /* cached */

    /* V4: load the per-relation schema (compressed beliefs).  No file =
     * fresh acquaintance, init to zero. */
    char spath[512];
    schema_path(eng, h, spath, sizeof(spath));
    SchemaState fresh; schema_state_init(&fresh);
    if (pe_read_file(spath, &eng->schema, sizeof(SchemaState)) != 0
        || eng->schema.version != fresh.version){
        eng->schema = fresh;
    }

    /* V6 Phase 4: load the multi-dim relational profile for this actor.
     * Missing file ⇒ derive defaults from the just-loaded V5 disposition
     * so the dimensions start in a sensible neutral place for a new
     * acquaintance, or matching the stored disposition for a returning
     * actor that predates V6. */
    pe_relation_dims_load(&eng->relation_dims, eng->char_dir, h,
                          eng->relation.disposition);
    pe_tom_load(&eng->theory_of_mind, eng->char_dir, h);
    return 0;
}

int pe_save_relation(Engine *eng){
    if (eng->relation.user_hash == 0) return 0;
    char path[512];
    relation_path(eng, eng->relation.user_hash, path, sizeof(path));
    int rc = pe_write_file_atomic(path, &eng->relation, sizeof(Relation));
    if (rc != 0) return rc;

    /* V4: persist the schema as a sibling file.  Atomic write so a
     * crash mid-save leaves the previous schema intact. */
    char spath[512];
    schema_path(eng, eng->relation.user_hash, spath, sizeof(spath));
    rc = pe_write_file_atomic(spath, &eng->schema, sizeof(SchemaState));
    if (rc != 0) return rc;

    /* V6 Phase 4: persist the multi-dim relational profile beside the
     * schema. Skipped silently when no actor is loaded. */
    rc = pe_relation_dims_save(&eng->relation_dims, eng->char_dir);
    if (rc != 0) return rc;
    return pe_tom_save(&eng->theory_of_mind, eng->char_dir);
}

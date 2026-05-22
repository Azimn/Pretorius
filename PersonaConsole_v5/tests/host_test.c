/* host_test.c — exercises the PersonaSession FFI directly.
 *
 * Doesn't speak HTTP — that path is curl-tested separately.  This is the
 * minimum assertion set for the ps_* surface used by both transports.
 */
#include "persona_ffi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, m) do { \
    if (!(c)){ printf("FAIL: %s\n", m); failures++; } \
    else      { printf("ok:   %s\n", m); } \
} while (0)

int main(void){
    /* Clean Pretorius runtime state. */
    (void)!system("rm -f profiles/pretorius/state.bin "
                  "profiles/pretorius/memory.bin "
                  "profiles/pretorius/chapters.bin");
    (void)!system("rm -rf profiles/pretorius/relations "
                  "profiles/pretorius/aether");

    PersonaSession *s = ps_open("profiles/pretorius/pretorius.cart");
    CHECK(s != NULL, "ps_open with .cart returns non-NULL");
    if (!s) return 1;

    char name[64];
    int nlen = ps_name(s, name, sizeof(name));
    CHECK(nlen > 0 && strstr(name, "Pretorius") != NULL,
          "ps_name returns 'Dr. Septimus Pretorius'");

    CHECK(ps_set_user(s, "test_user") == 0, "ps_set_user succeeds");

    char reply[2048];
    int rlen = ps_reply(s, "Good evening.", reply, sizeof(reply));
    CHECK(rlen > 0, "ps_reply produces a response");
    CHECK(strlen(reply) > 0, "reply string is non-empty");

    char state_json[2048];
    int slen = ps_state(s, state_json, sizeof(state_json));
    CHECK(slen > 0 && state_json[0] == '{',
          "ps_state emits a JSON object");
    CHECK(strstr(state_json, "\"name\":\"Dr. Septimus Pretorius\"") != NULL,
          "state JSON includes character_name");
    CHECK(strstr(state_json, "\"turn_count\":1") != NULL,
          "state JSON shows turn_count=1 after one reply");

    /* Switch user, send another turn, verify user_id changed in state. */
    ps_set_user(s, "another_user");
    ps_reply(s, "Tell me about gin.", reply, sizeof(reply));
    ps_state(s, state_json, sizeof(state_json));
    CHECK(strstr(state_json, "\"user_id\":\"another_user\"") != NULL,
          "state JSON tracks active user across set_user");

    CHECK(ps_save(s) == 0, "ps_save returns 0");

    /* Hot-swap to Kiki. */
    int load_rc = ps_load(s, "profiles/kiki/kiki.cart");
    CHECK(load_rc == 0, "ps_load swaps to Kiki cartridge");
    ps_name(s, name, sizeof(name));
    CHECK(strstr(name, "Kiki") != NULL,
          "after ps_load, ps_name returns 'Kiki'");

    ps_close(s);
    printf("\n%s — %d failure(s)\n",
           failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

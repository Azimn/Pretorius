/* json.h — minimal JSON helpers for PersonaHost.
 *
 * Not a general-purpose JSON library.  Covers exactly what the host API
 * needs: write objects with string/int fields, extract a string by key
 * from a flat object body.  No nested arrays, no escape edge cases
 * beyond \" \\ \n \r \t.
 */
#ifndef PERSONA_HOST_JSON_H
#define PERSONA_HOST_JSON_H

#include <stddef.h>

/* Escape src into dst with surrounding quotes, advancing dst.  Returns
 * bytes written, or -1 on overflow. */
int json_emit_quoted(char *dst, int cap, const char *src);

/* Find "key":"value" in body and copy the value into out (NUL-terminated).
 * Returns 0 on success, -1 if key not found or value not a string. */
int json_get_string(const char *body, const char *key,
                    char *out, int out_cap);

/* Find "key":N (integer) in body.  Returns 0 on success, sets *out. */
int json_get_int(const char *body, const char *key, int *out);

#endif

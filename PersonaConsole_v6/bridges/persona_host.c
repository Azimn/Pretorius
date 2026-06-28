/* persona_host.c — PersonaHost main entry point.
 *
 * One binary, two transports:
 *   ./persona_host <cart_or_dir>                 (HTTP on :7777)
 *   ./persona_host --port 8000 <cart_or_dir>     (HTTP on custom port)
 *   ./persona_host --stdio <cart_or_dir>         (line-delimited JSON over stdio)
 *
 * The HTTP server exposes the same JSON contract that the stdio mode
 * speaks; both share the same dispatch table in route().
 *
 * Stdio protocol: one request per line.  Each request is a JSON object
 * with a "method" key and method-specific fields.  Response is a single
 * JSON line.  Useful for game engines / scripts that prefer subprocess
 * control over FFI.
 */
#include "persona_ffi.h"
#include "persona.h"
#include "http.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define DEFAULT_PORT     7777
#define REPLY_BUF        8192
#define STATE_BUF        4096
#define WEB_ROOT_DEFAULT "host/web"

typedef struct {
    PersonaSession *sess;
    const char     *web_root;
    char            reply_scratch[REPLY_BUF];
    char            state_scratch[STATE_BUF];
} HostCtx;

/* ---------- static file serving (for the web UI) ---------- */

static const char *mime_for(const char *path){
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".css"))  return "text/css; charset=utf-8";
    if (!strcmp(dot, ".js"))   return "application/javascript; charset=utf-8";
    if (!strcmp(dot, ".json")) return "application/json; charset=utf-8";
    if (!strcmp(dot, ".png"))  return "image/png";
    if (!strcmp(dot, ".jpg"))  return "image/jpeg";
    if (!strcmp(dot, ".svg"))  return "image/svg+xml";
    return "application/octet-stream";
}

/* Static-files use a per-process buffer.  Single-threaded server, single
 * connection in flight at a time, so this is safe.  Sized for typical
 * portrait images (PNG/JPG up to ~2 MB). */
static char  g_static_buf[2 * 1024 * 1024];
static int   g_static_len = 0;

static int serve_file(const char *path, HttpResponse *out){
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    g_static_len = (int)fread(g_static_buf, 1, sizeof(g_static_buf), f);
    fclose(f);
    out->status        = 200;
    out->content_type  = mime_for(path);
    out->body          = g_static_buf;
    out->body_len      = g_static_len;
    return 0;
}

static int serve_static(const char *web_root, const char *rel_path,
                        HttpResponse *out){
    char path[512];
    if (rel_path[0] == '/') rel_path++;
    if (rel_path[0] == 0)   rel_path = "index.html";
    /* refuse paths containing ".." to prevent traversal */
    if (strstr(rel_path, "..")) { out->status = 400; return -1; }
    snprintf(path, sizeof(path), "%s/%s", web_root, rel_path);
    if (serve_file(path, out) != 0){
        out->status = 404; out->content_type = "text/plain";
        out->body = "not found"; out->body_len = 9; return 0;
    }
    return 0;
}

/* Serve the active character's portrait, probing common extensions in
 * the cartridge's runtime directory.  The first one that opens wins.
 * If none exist, returns a 404 — the web UI will fall back to its
 * initial-letter placeholder. */
static int serve_portrait(PersonaSession *s, HttpResponse *out){
    char char_dir[256];
    if (ps_char_dir(s, char_dir, sizeof(char_dir)) <= 0){
        out->status = 500; out->content_type = "text/plain";
        out->body = "no char dir"; out->body_len = 11; return 0;
    }
    static const char *EXTS[] = {
        "portrait.png", "portrait.jpg", "portrait.jpeg",
        "portrait.webp", "portrait.svg", "portrait.gif", NULL
    };
    char path[512];
    for (int i = 0; EXTS[i]; ++i){
        snprintf(path, sizeof(path), "%s/%s", char_dir, EXTS[i]);
        if (serve_file(path, out) == 0) return 0;
    }
    out->status = 404; out->content_type = "text/plain";
    out->body = "no portrait"; out->body_len = 11; return 0;
}

/* ---------- request dispatch (shared by HTTP and stdio) ---------- */

/* Each handler writes a JSON object into out_buf and returns its length. */
typedef int (*MethodFn)(HostCtx *ctx, const char *body,
                        char *out_buf, int out_cap);

static void json_opt_string(const char *body, const char *key,
                            char *out, int out_cap){
    if (!out || out_cap <= 0) return;
    out[0] = 0;
    json_get_string(body, key, out, out_cap);
}

static int json_opt_int(const char *body, const char *key, int fallback){
    int v = fallback;
    json_get_int(body, key, &v);
    return v;
}

static int method_chat(HostCtx *ctx, const char *body,
                       char *out_buf, int out_cap){
    char input[4096];
    if (json_get_string(body, "text", input, sizeof(input)) != 0)
        return snprintf(out_buf, (size_t)out_cap,
                        "{\"error\":\"missing 'text' field\"}");
    int n = ps_reply(ctx->sess, input,
                     ctx->reply_scratch, sizeof(ctx->reply_scratch));
    if (n < 0) return snprintf(out_buf, (size_t)out_cap,
                               "{\"error\":\"ps_reply failed\",\"code\":%d}", n);
    /* Build {"reply":"…","state":{…}}. */
    int sn = ps_state(ctx->sess, ctx->state_scratch, sizeof(ctx->state_scratch));
    if (n == 0 && ctx->reply_scratch[0] == 0){
        if (sn > 0)
            return snprintf(out_buf, (size_t)out_cap,
                            "{\"reply\":null,\"pause\":true,\"state\":%s}",
                            ctx->state_scratch);
        return snprintf(out_buf, (size_t)out_cap,
                        "{\"reply\":null,\"pause\":true}");
    }
    int pos = 0;
    pos += snprintf(out_buf + pos, (size_t)(out_cap - pos), "{\"reply\":");
    int qn = json_emit_quoted(out_buf + pos, out_cap - pos, ctx->reply_scratch);
    if (qn < 0) return -1;
    pos += qn;
    if (sn > 0)
        pos += snprintf(out_buf + pos, (size_t)(out_cap - pos),
                        ",\"state\":%s", ctx->state_scratch);
    pos += snprintf(out_buf + pos, (size_t)(out_cap - pos), "}");
    return pos;
}

static int method_state(HostCtx *ctx, const char *body,
                        char *out_buf, int out_cap){
    (void)body;
    return ps_state(ctx->sess, out_buf, out_cap);
}

static int method_reflections(HostCtx *ctx, const char *body,
                              char *out_buf, int out_cap){
    (void)body;
    return ps_reflections(ctx->sess, out_buf, out_cap);
}

static int method_relationships(HostCtx *ctx, const char *body,
                                char *out_buf, int out_cap){
    (void)body;
    return ps_relationships(ctx->sess, out_buf, out_cap);
}

static int method_idle_probe(HostCtx *ctx, const char *body,
                             char *out_buf, int out_cap){
    (void)body;
    int n = ps_idle_probe(ctx->sess,
                          ctx->reply_scratch, sizeof(ctx->reply_scratch));
    if (n < 0) return snprintf(out_buf, (size_t)out_cap,
                               "{\"error\":\"ps_idle_probe failed\",\"code\":%d}", n);
    int pos = 0;
    pos += snprintf(out_buf + pos, (size_t)(out_cap - pos), "{\"reply\":");
    if (n == 0) {
        pos += snprintf(out_buf + pos, (size_t)(out_cap - pos), "null");
    } else {
        int qn = json_emit_quoted(out_buf + pos, out_cap - pos, ctx->reply_scratch);
        if (qn < 0) return -1;
        pos += qn;
    }
    int sn = ps_state(ctx->sess, ctx->state_scratch, sizeof(ctx->state_scratch));
    if (sn > 0)
        pos += snprintf(out_buf + pos, (size_t)(out_cap - pos),
                        ",\"state\":%s", ctx->state_scratch);
    pos += snprintf(out_buf + pos, (size_t)(out_cap - pos), "}");
    return pos;
}

static int method_save(HostCtx *ctx, const char *body,
                       char *out_buf, int out_cap){
    (void)body;
    int rc = ps_save(ctx->sess);
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"ok\":%s,\"code\":%d}",
                    rc == 0 ? "true" : "false", rc);
}

static int method_load(HostCtx *ctx, const char *body,
                       char *out_buf, int out_cap){
    char new_path[512];
    if (json_get_string(body, "path", new_path, sizeof(new_path)) != 0)
        return snprintf(out_buf, (size_t)out_cap,
                        "{\"error\":\"missing 'path' field\"}");
    int rc = ps_load(ctx->sess, new_path);
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"ok\":%s,\"code\":%d}",
                    rc == 0 ? "true" : "false", rc);
}

static int method_set_user(HostCtx *ctx, const char *body,
                           char *out_buf, int out_cap){
    char user_id[128];
    if (json_get_string(body, "user_id", user_id, sizeof(user_id)) != 0)
        return snprintf(out_buf, (size_t)out_cap,
                        "{\"error\":\"missing 'user_id' field\"}");
    int rc = ps_set_user(ctx->sess, user_id);
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"ok\":%s,\"code\":%d}",
                    rc == 0 ? "true" : "false", rc);
}

static int method_discard_changes(HostCtx *ctx, const char *body,
                                  char *out_buf, int out_cap){
    (void)body;
    int rc = ps_discard_unsaved(ctx->sess);
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"ok\":%s,\"code\":%d}",
                    rc == 0 ? "true" : "false", rc);
}

static int method_reset_runtime(HostCtx *ctx, const char *body,
                                char *out_buf, int out_cap){
    (void)body;
    int rc = ps_reset_runtime(ctx->sess);
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"ok\":%s,\"code\":%d}",
                    rc == 0 ? "true" : "false", rc);
}

static int method_import_memory(HostCtx *ctx, const char *body,
                                char *out_buf, int out_cap){
    char summary[PE_MEM_SUMMARY_LEN];
    char topic_key[PE_LK_TOPIC_LEN];
    char actor_name[PE_NAME_LEN];
    unsigned memory_id = 0;
    int rc;
    if (json_get_string(body, "summary", summary, sizeof(summary)) != 0)
        return snprintf(out_buf, (size_t)out_cap,
                        "{\"error\":\"missing 'summary' field\"}");
    json_opt_string(body, "topic_key", topic_key, sizeof(topic_key));
    json_opt_string(body, "actor_name", actor_name, sizeof(actor_name));
    rc = ps_import_memory(ctx->sess,
                          summary,
                          topic_key[0] ? topic_key : NULL,
                          actor_name[0] ? actor_name : NULL,
                          json_opt_int(body, "salience", 60),
                          json_opt_int(body, "emotional_impact", 0),
                          json_opt_int(body, "is_core", 0),
                          json_opt_int(body, "is_pinned", 0),
                          &memory_id);
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"ok\":%s,\"code\":%d,\"memory_id\":%u}",
                    rc == 0 ? "true" : "false", rc, memory_id);
}

static int method_import_relationship(HostCtx *ctx, const char *body,
                                      char *out_buf, int out_cap){
    char actor_name[PE_NAME_LEN];
    int rc;
    if (json_get_string(body, "actor_name", actor_name, sizeof(actor_name)) != 0)
        return snprintf(out_buf, (size_t)out_cap,
                        "{\"error\":\"missing 'actor_name' field\"}");
    rc = ps_import_relationship(ctx->sess,
                                actor_name,
                                (unsigned)json_opt_int(body, "trust", 500),
                                (unsigned)json_opt_int(body, "threat", 500),
                                (unsigned)json_opt_int(body, "intimacy", 0),
                                (unsigned)json_opt_int(body, "resentment", 0),
                                (unsigned)json_opt_int(body, "dependency", 0),
                                (unsigned)json_opt_int(body, "obligation", 0),
                                (unsigned)json_opt_int(body, "envy", 0),
                                (unsigned)json_opt_int(body, "admiration", 500),
                                (unsigned)json_opt_int(body, "embarrassment", 0));
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"ok\":%s,\"code\":%d}",
                    rc == 0 ? "true" : "false", rc);
}

static int method_import_open_loop(HostCtx *ctx, const char *body,
                                   char *out_buf, int out_cap){
    char actor_name[PE_NAME_LEN];
    char topic_key[PE_LK_TOPIC_LEN];
    char desired[32];
    unsigned loop_id = 0;
    int rc;
    if (json_get_string(body, "topic_key", topic_key, sizeof(topic_key)) != 0)
        return snprintf(out_buf, (size_t)out_cap,
                        "{\"error\":\"missing 'topic_key' field\"}");
    json_opt_string(body, "actor_name", actor_name, sizeof(actor_name));
    json_opt_string(body, "desired_speech_act", desired, sizeof(desired));
    rc = ps_import_open_loop(ctx->sess,
                             actor_name[0] ? actor_name : NULL,
                             topic_key,
                             desired[0] ? desired : NULL,
                             (unsigned)json_opt_int(body, "urgency", 500),
                             (unsigned)json_opt_int(body, "shame_cost", 0),
                             (unsigned)json_opt_int(body, "avoidance_pressure", 0),
                             &loop_id);
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"ok\":%s,\"code\":%d,\"loop_id\":%u}",
                    rc == 0 ? "true" : "false", rc, loop_id);
}

static int method_import_learned_knowledge(HostCtx *ctx, const char *body,
                                           char *out_buf, int out_cap){
    char topic_key[PE_LK_TOPIC_LEN];
    char claim_text[PE_LK_CLAIM_LEN];
    char source_actor_name[PE_LK_SOURCE_NAME_LEN];
    unsigned record_id = 0;
    int rc;
    if (json_get_string(body, "topic_key", topic_key, sizeof(topic_key)) != 0)
        return snprintf(out_buf, (size_t)out_cap,
                        "{\"error\":\"missing 'topic_key' field\"}");
    if (json_get_string(body, "claim_text", claim_text, sizeof(claim_text)) != 0)
        return snprintf(out_buf, (size_t)out_cap,
                        "{\"error\":\"missing 'claim_text' field\"}");
    json_opt_string(body, "source_actor_name", source_actor_name, sizeof(source_actor_name));
    rc = ps_import_learned_knowledge(ctx->sess,
                                     topic_key,
                                     claim_text,
                                     (unsigned)json_opt_int(body, "scope", PE_LK_SCOPE_REAL_WORLD),
                                     (unsigned)json_opt_int(body, "source_type", PE_LK_SRC_IMPORTED),
                                     (unsigned)json_opt_int(body, "source_tier", PE_LK_TIER_OFFLINE),
                                     (unsigned)json_opt_int(body, "status", PE_LK_STATUS_CANDIDATE),
                                     (unsigned)json_opt_int(body, "authority_rank", 35),
                                     (unsigned)json_opt_int(body, "confidence", 450),
                                     source_actor_name[0] ? source_actor_name : NULL,
                                     (unsigned)json_opt_int(body, "correction_of_record_id", 0),
                                     (unsigned)json_opt_int(body, "evidence_ref", 0),
                                     (unsigned)json_opt_int(body, "domain_tag", 0),
                                     &record_id);
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"ok\":%s,\"code\":%d,\"record_id\":%u}",
                    rc == 0 ? "true" : "false", rc, record_id);
}

static int method_import_learned_edge(HostCtx *ctx, const char *body,
                                      char *out_buf, int out_cap){
    unsigned edge_id = 0;
    int rc = ps_import_learned_edge(ctx->sess,
                                    (unsigned)json_opt_int(body, "source_record_id", 0),
                                    (unsigned)json_opt_int(body, "relation_type", 0),
                                    (unsigned)json_opt_int(body, "target_record_id", 0),
                                    (unsigned)json_opt_int(body, "weight", 500),
                                    (unsigned)json_opt_int(body, "confidence", 500),
                                    &edge_id);
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"ok\":%s,\"code\":%d,\"edge_id\":%u}",
                    rc == 0 ? "true" : "false", rc, edge_id);
}

static int dispatch(HostCtx *ctx, const char *method, const char *body,
                    char *out_buf, int out_cap){
    if (!strcmp(method, "chat"))     return method_chat(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "state"))    return method_state(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "idle_probe")) return method_idle_probe(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "save"))     return method_save(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "load"))     return method_load(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "set_user")) return method_set_user(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "discard_changes")) return method_discard_changes(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "reset_runtime")) return method_reset_runtime(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "import_memory")) return method_import_memory(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "import_relationship")) return method_import_relationship(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "import_open_loop")) return method_import_open_loop(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "import_learned_knowledge")) return method_import_learned_knowledge(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "import_learned_edge")) return method_import_learned_edge(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "reflections")) return method_reflections(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "relationships")) return method_relationships(ctx, body, out_buf, out_cap);
    return snprintf(out_buf, (size_t)out_cap,
                    "{\"error\":\"unknown method '%s'\"}", method);
}

/* ---------- HTTP request handler ---------- */

/* JSON response buffer is shared with state_scratch since the HTTP
 * lifecycle is single-threaded.  Real JSON output lives in here. */
static char g_http_response[16384];

static HttpResponse route(const HttpRequest *req, void *vctx){
    HostCtx *ctx = (HostCtx*)vctx;
    HttpResponse r;
    memset(&r, 0, sizeof(r));

    /* GET / and GET /web/ paths serve files from disk. */
    if (!strcmp(req->method, "GET")){
        const char *path = req->path;
        if (!strcmp(path, "/")) path = "/index.html";
        if (!strncmp(path, "/web/", 5)){
            /* /web/<file> → web_root/<file> */
            serve_static(ctx->web_root, path + 5, &r);
            return r;
        }
        if (strstr(path, ".html") || strstr(path, ".css")
            || strstr(path, ".js") || strstr(path, ".png")
            || strstr(path, ".jpg") || strstr(path, ".svg")){
            serve_static(ctx->web_root, path, &r);
            return r;
        }
        if (!strcmp(path, "/state")){
            int n = ps_state(ctx->sess, g_http_response, sizeof(g_http_response));
            r.status = (n > 0) ? 200 : 500;
            r.content_type = "application/json";
            r.body = g_http_response;
            r.body_len = (n > 0) ? n : 0;
            return r;
        }
        if (!strcmp(path, "/portrait")){
            serve_portrait(ctx->sess, &r);
            return r;
        }
        /* Default GET → index.html */
        serve_static(ctx->web_root, "index.html", &r);
        return r;
    }

    /* POST /<method> → dispatch JSON */
    if (!strcmp(req->method, "POST")){
        const char *m = req->path;
        if (m[0] == '/') m++;
        int n = dispatch(ctx, m, req->body,
                         g_http_response, (int)sizeof(g_http_response));
        r.status = 200;
        r.content_type = "application/json";
        r.body = g_http_response;
        r.body_len = (n > 0) ? n : (int)strlen(g_http_response);
        return r;
    }

    r.status = 400;
    r.content_type = "text/plain";
    r.body = "unsupported method";
    r.body_len = 18;
    return r;
}

/* ---------- stdio JSON-line loop ---------- */

static int stdio_loop(HostCtx *ctx){
    char  in_line[8192];
    char  out_buf[16384];
    while (fgets(in_line, sizeof(in_line), stdin)){
        char method[64] = "";
        json_get_string(in_line, "method", method, sizeof(method));
        if (method[0] == 0){
            fprintf(stdout, "{\"error\":\"missing 'method' field\"}\n");
            fflush(stdout);
            continue;
        }
        if (!strcmp(method, "close")){
            fprintf(stdout, "{\"ok\":true}\n");
            fflush(stdout);
            break;
        }
        int n = dispatch(ctx, method, in_line, out_buf, (int)sizeof(out_buf));
        if (n > 0) out_buf[n] = 0;
        fprintf(stdout, "%s\n", out_buf);
        fflush(stdout);
    }
    return 0;
}

/* ---------- main ---------- */

static void usage(const char *prog){
    fprintf(stderr,
        "usage: %s [--port N] [--stdio] [--web-root DIR] <cartridge_path>\n"
        "  cartridge_path: directory or .cart file\n"
        "  default port:   %d\n"
        "  default web:    %s\n",
        prog, DEFAULT_PORT, WEB_ROOT_DEFAULT);
}

int main(int argc, char **argv){
    int   port      = DEFAULT_PORT;
    int   use_stdio = 0;
    int   autosave_on_close = 1;
    const char *web_root = WEB_ROOT_DEFAULT;
    const char *cart_path = NULL;

    {
        const char *no_autosave = getenv("PE_NO_AUTOSAVE_ON_CLOSE");
        if (no_autosave && no_autosave[0] && strcmp(no_autosave, "0"))
            autosave_on_close = 0;
    }

    for (int i = 1; i < argc; ++i){
        if (!strcmp(argv[i], "--port") && i + 1 < argc){
            port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--stdio")){
            use_stdio = 1;
        } else if (!strcmp(argv[i], "--web-root") && i + 1 < argc){
            web_root = argv[++i];
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")){
            usage(argv[0]); return 0;
        } else {
            cart_path = argv[i];
        }
    }
    if (!cart_path){ usage(argv[0]); return 1; }

    PersonaSession *sess = ps_open(cart_path);
    if (!sess){
        fprintf(stderr, "persona_host: failed to open '%s'\n", cart_path);
        return 1;
    }
    char namebuf[64];
    ps_name(sess, namebuf, sizeof(namebuf));
    fprintf(stderr, "[persona_host] loaded '%s' from %s\n", namebuf, cart_path);

    HostCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.sess     = sess;
    ctx.web_root = web_root;

    int rc = use_stdio ? stdio_loop(&ctx)
                       : http_serve(port, route, &ctx);

    if (autosave_on_close) ps_close(sess);
    else ps_close_without_save(sess);
    return rc;
}

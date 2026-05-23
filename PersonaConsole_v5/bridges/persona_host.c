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

static int dispatch(HostCtx *ctx, const char *method, const char *body,
                    char *out_buf, int out_cap){
    if (!strcmp(method, "chat"))     return method_chat(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "state"))    return method_state(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "idle_probe")) return method_idle_probe(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "save"))     return method_save(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "load"))     return method_load(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "set_user")) return method_set_user(ctx, body, out_buf, out_cap);
    if (!strcmp(method, "reflections")) return method_reflections(ctx, body, out_buf, out_cap);
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
    const char *web_root = WEB_ROOT_DEFAULT;
    const char *cart_path = NULL;

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

    ps_close(sess);
    return rc;
}

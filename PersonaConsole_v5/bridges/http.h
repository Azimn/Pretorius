/* http.h — minimal HTTP/1.1 server for PersonaHost (localhost-only).
 *
 * Single-threaded request/response loop on POSIX sockets.  No keep-alive,
 * no chunked encoding, no TLS — this binds to 127.0.0.1 and only handles
 * one conversation at a time from a local web UI / curl client.
 *
 * Request shape: GET or POST, path ≤ 255, headers parsed up to 4 KB,
 * body up to HTTP_MAX_BODY (16 KB) read by Content-Length.
 *
 * Handler callback receives the parsed request + context, fills response.
 * Response body is borrowed (handler owns the buffer; server copies it
 * onto the socket before returning).
 */
#ifndef PERSONA_HOST_HTTP_H
#define PERSONA_HOST_HTTP_H

#include <stddef.h>

#define HTTP_MAX_PATH      256
#define HTTP_MAX_METHOD     8
#define HTTP_MAX_BODY    16384

typedef struct {
    char  method[HTTP_MAX_METHOD];
    char  path[HTTP_MAX_PATH];
    char  body[HTTP_MAX_BODY];
    int   body_len;
} HttpRequest;

typedef struct {
    int         status;             /* 200, 400, 404, 500, … */
    const char *content_type;       /* "application/json", "text/html", … */
    const char *body;               /* borrowed; handler owns lifetime */
    int         body_len;
} HttpResponse;

typedef HttpResponse (*HttpHandler)(const HttpRequest *req, void *ctx);

/* Run the accept loop on 127.0.0.1:port.  Blocks until a SIGINT or an
 * accept error.  Returns 0 on graceful shutdown, negative on bind/listen
 * failure. */
int http_serve(int port, HttpHandler handler, void *ctx);

#endif

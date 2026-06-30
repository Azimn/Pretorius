/* ollama_provider.c — minimal POSIX HTTP/1.1 client for Ollama.
 *
 * Zero dependencies beyond libc + sockets.  No SSL.  No threads.
 * Single-shot non-streamed generate.  Suitable for personas where
 * latency budget is ~3s and replies are <1KB.
 */
#include "ollama_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>

void ollama_load_config(OllamaConfig *cfg){
    if (!cfg) return;
    const char *host = getenv("PE_OLLAMA_HOST");
    const char *port = getenv("PE_OLLAMA_PORT");
    const char *model = getenv("PE_OLLAMA_MODEL");
    const char *to    = getenv("PE_OLLAMA_TIMEOUT_MS");
    const char *temp  = getenv("PE_OLLAMA_TEMP");
    const char *npred = getenv("PE_OLLAMA_NUM_PRED");
    const char *raw   = getenv("PE_OLLAMA_RAW");
    const char *allow_template = getenv("PE_ALLOW_MODEL_TEMPLATE");
    const char *think = getenv("PE_OLLAMA_THINK");

    snprintf(cfg->host,  sizeof(cfg->host),  "%s", host  ? host  : "127.0.0.1");
    snprintf(cfg->model, sizeof(cfg->model), "%s", model ? model : "gemma2:2b");
    cfg->port                  = port  ? atoi(port)  : 11434;
    cfg->timeout_ms            = to    ? atoi(to)    : 8000;
    cfg->temperature_per_mille = temp  ? atoi(temp)  : 0;
    cfg->num_predict           = npred ? atoi(npred) : 160;
    cfg->raw                   = raw   ? atoi(raw)   : 1;
    if (!cfg->raw && (!allow_template || atoi(allow_template) == 0))
        cfg->raw = 1;
    cfg->think                 = think ? atoi(think) : 0;

    if (cfg->port <= 0 || cfg->port > 65535) cfg->port = 11434;
    if (cfg->timeout_ms < 100) cfg->timeout_ms = 100;
    if (cfg->num_predict < 16) cfg->num_predict = 16;
    if (cfg->num_predict > 1024) cfg->num_predict = 1024;
}

/* ----- JSON helpers (intentionally minimal) ----- */

/* Append str into buf, JSON-escaping ", \, control bytes. */
static int json_escape_append(char *buf, int cap, int *pos, const char *s){
    if (!s) return 1;
    while (*s){
        if (*pos >= cap - 2) return 0;
        unsigned char c = (unsigned char)*s++;
        if (c == '"' || c == '\\'){
            buf[(*pos)++] = '\\';
            if (*pos >= cap - 1) return 0;
            buf[(*pos)++] = (char)c;
        } else if (c == '\n'){
            buf[(*pos)++] = '\\'; buf[(*pos)++] = 'n';
        } else if (c == '\r'){
            buf[(*pos)++] = '\\'; buf[(*pos)++] = 'r';
        } else if (c == '\t'){
            buf[(*pos)++] = '\\'; buf[(*pos)++] = 't';
        } else if (c < 0x20){
            int n = snprintf(buf + *pos, cap - *pos, "\\u%04x", c);
            if (n < 0 || n >= cap - *pos) return 0;
            *pos += n;
        } else {
            buf[(*pos)++] = (char)c;
        }
    }
    return 1;
}

/* Extract value of the FIRST occurrence of "key": "..." from JSON.
 * Returns bytes written on success (excl NUL), -1 on missing key.
 * Handles \" \\ \n \r \t and \uXXXX (only basic-plane → ASCII drop). */
static int json_extract_string(const char *json, const char *key,
                               char *out, int out_cap){
    char pat[64];
    int  pn = snprintf(pat, sizeof(pat), "\"%s\":", key);
    if (pn < 0) return -1;
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += pn;
    while (*p == ' ' || *p == '\t' || *p == '\n') ++p;
    if (*p != '"') return -1;
    ++p;
    int w = 0;
    while (*p && w < out_cap - 1){
        if (*p == '\\' && p[1]){
            char c = p[1];
            ++p;
            switch (c){
            case 'n':  out[w++] = '\n'; break;
            case 'r':  out[w++] = '\r'; break;
            case 't':  out[w++] = '\t'; break;
            case '"':  out[w++] = '"';  break;
            case '\\': out[w++] = '\\'; break;
            case '/':  out[w++] = '/';  break;
            case 'u':
                /* \uXXXX — basic plane only; map non-ASCII to '?' */
                if (p[1] && p[2] && p[3] && p[4]){
                    unsigned int v;
                    if (sscanf(p+1, "%4x", &v) == 1){
                        out[w++] = (v < 0x80) ? (char)v : '?';
                    }
                    p += 4;
                }
                break;
            default:   out[w++] = c;
            }
            ++p;
        } else if (*p == '"'){
            break;
        } else {
            out[w++] = *p++;
        }
    }
    out[w] = 0;
    return w;
}

/* ----- TCP I/O with select-based timeout ----- */

static int set_nonblock(int fd){
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int connect_with_timeout(const char *host, int port, int timeout_ms){
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1){
        /* Hostname lookup deliberately omitted — Ollama is local-only. */
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    set_nonblock(fd);

    int r = connect(fd, (struct sockaddr*)&sa, sizeof(sa));
    if (r == 0) return fd;
    if (errno != EINPROGRESS){ close(fd); return -1; }

    fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    r = select(fd + 1, NULL, &wfds, NULL, &tv);
    if (r <= 0){ close(fd); return -1; }
    int err = 0; socklen_t errlen = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0 || err){
        close(fd); return -1;
    }
    return fd;
}

static int send_all(int fd, const char *buf, int len, int timeout_ms){
    int sent = 0;
    while (sent < len){
        fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        int r = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (r <= 0) return -1;
        ssize_t s = send(fd, buf + sent, len - sent, 0);
        if (s < 0){
            if (errno == EAGAIN || errno == EINTR) continue;
            return -1;
        }
        sent += (int)s;
    }
    return sent;
}

static int recv_all(int fd, char *buf, int cap, int timeout_ms){
    int got = 0;
    while (got < cap - 1){
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        int r = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) return -1;
        if (r == 0) break;        /* idle past timeout = done */
        ssize_t s = recv(fd, buf + got, cap - 1 - got, 0);
        if (s == 0) break;        /* server closed */
        if (s < 0){
            if (errno == EAGAIN || errno == EINTR) continue;
            return -1;
        }
        got += (int)s;
    }
    buf[got] = 0;
    return got;
}

/* ----- public entry ----- */

int ollama_generate(const OllamaConfig *cfg,
                    const char *prompt,
                    unsigned int seed,
                    char *out, int out_cap){
    if (!cfg || !prompt || !out || out_cap < 64) return -2;

    /* Build the JSON request body. */
    char body[8192];
    int  p = 0;
    p += snprintf(body + p, sizeof(body) - p,
                  "{\"model\":\"%s\",\"stream\":false,"
                  "\"think\":%s,"
                  "\"options\":{\"temperature\":%.3f,\"num_predict\":%d,\"seed\":%u},"
                  "\"raw\":%s,"
                  "\"prompt\":\"",
                  cfg->model,
                  cfg->think ? "true" : "false",
                  (double)cfg->temperature_per_mille / 1000.0,
                  cfg->num_predict,
                  seed,
                  cfg->raw ? "true" : "false");
    if (p < 0 || p >= (int)sizeof(body)) return -2;
    if (!json_escape_append(body, (int)sizeof(body), &p, prompt)) return -2;
    if (p >= (int)sizeof(body) - 2) return -2;
    body[p++] = '"'; body[p++] = '}'; body[p] = 0;

    /* Build the HTTP request. */
    char req[10240];
    int rp = snprintf(req, sizeof(req),
                      "POST /api/generate HTTP/1.1\r\n"
                      "Host: %s:%d\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %d\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      cfg->host, cfg->port, p);
    if (rp < 0 || rp + p >= (int)sizeof(req)) return -2;
    memcpy(req + rp, body, (size_t)p);
    rp += p;

    int fd = connect_with_timeout(cfg->host, cfg->port, cfg->timeout_ms);
    if (fd < 0) return -1;
    if (send_all(fd, req, rp, cfg->timeout_ms) < 0){ close(fd); return -1; }

    /* Receive — large enough to hold a 1KB reply plus HTTP headers. */
    char buf[16384];
    int n = recv_all(fd, buf, sizeof(buf), cfg->timeout_ms);
    close(fd);
    if (n <= 0) return -3;

    /* Locate the start of the body (skip headers). */
    char *body_start = strstr(buf, "\r\n\r\n");
    if (!body_start) return -2;
    body_start += 4;

    int w = json_extract_string(body_start, "response", out, out_cap);
    if (w < 0) return -2;

    /* Trim trailing whitespace.  Ollama often emits trailing newline. */
    while (w > 0 && (out[w-1] == '\n' || out[w-1] == '\r' || out[w-1] == ' '))
        out[--w] = 0;

    return w;
}

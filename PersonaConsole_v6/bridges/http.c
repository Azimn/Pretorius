/* http.c — minimal HTTP/1.1 server, see http.h. */
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     /* strncasecmp */
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig){ (void)sig; g_stop = 1; }

/* Read until "\r\n\r\n" or buffer full.  Returns total bytes read,
 * or -1 on error.  Sets *body_offset to first byte of body. */
static int read_headers(int fd, char *buf, int cap, int *body_offset){
    int total = 0;
    while (total < cap - 1){
        ssize_t r = recv(fd, buf + total, (size_t)(cap - 1 - total), 0);
        if (r <= 0) return -1;
        total += (int)r;
        buf[total] = 0;
        char *end = strstr(buf, "\r\n\r\n");
        if (end){
            *body_offset = (int)(end - buf) + 4;
            return total;
        }
    }
    return -1; /* headers too large */
}

static int parse_request_line(const char *line, HttpRequest *req){
    int i = 0;
    while (line[i] && line[i] != ' ' && i < HTTP_MAX_METHOD - 1){
        req->method[i] = line[i]; i++;
    }
    req->method[i] = 0;
    if (line[i] != ' ') return -1;
    int j = 0; i++;
    while (line[i] && line[i] != ' ' && j < HTTP_MAX_PATH - 1){
        req->path[j++] = line[i++];
    }
    req->path[j] = 0;
    return 0;
}

/* Case-insensitive substring search. */
static const char *find_header(const char *headers, const char *name){
    size_t nlen = strlen(name);
    const char *p = headers;
    while (*p){
        const char *eol = strstr(p, "\r\n");
        if (!eol) break;
        if ((size_t)(eol - p) > nlen + 1
            && (p[nlen] == ':' )
            && strncasecmp(p, name, nlen) == 0){
            const char *v = p + nlen + 1;
            while (*v == ' ' || *v == '\t') v++;
            return v;
        }
        p = eol + 2;
    }
    return NULL;
}

static int handle_connection(int fd, HttpHandler handler, void *ctx){
    char buf[8192];
    int  body_offset = 0;
    int  total = read_headers(fd, buf, (int)sizeof(buf), &body_offset);
    if (total < 0) return -1;

    HttpRequest req;
    memset(&req, 0, sizeof(req));
    if (parse_request_line(buf, &req) != 0) return -1;

    int body_len = 0;
    const char *cl = find_header(buf, "Content-Length");
    if (cl) body_len = atoi(cl);
    if (body_len > HTTP_MAX_BODY) body_len = HTTP_MAX_BODY;

    int have = total - body_offset;
    if (have > 0){
        int copy = have > body_len ? body_len : have;
        memcpy(req.body, buf + body_offset, (size_t)copy);
        req.body_len = copy;
    }
    while (req.body_len < body_len){
        ssize_t r = recv(fd, req.body + req.body_len,
                         (size_t)(body_len - req.body_len), 0);
        if (r <= 0) break;
        req.body_len += (int)r;
    }
    req.body[req.body_len] = 0;

    HttpResponse resp = handler(&req, ctx);
    if (!resp.content_type) resp.content_type = "text/plain";

    char head[512];
    int  hlen = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n",
        resp.status,
        resp.status == 200 ? "OK" :
        resp.status == 400 ? "Bad Request" :
        resp.status == 404 ? "Not Found" :
        resp.status == 500 ? "Internal Server Error" : "Status",
        resp.content_type, resp.body_len);
    if (send(fd, head, (size_t)hlen, 0) < 0) return -1;
    if (resp.body_len > 0 && resp.body){
        if (send(fd, resp.body, (size_t)resp.body_len, 0) < 0) return -1;
    }
    return 0;
}

int http_serve(int port, HttpHandler handler, void *ctx){
    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);
    signal(SIGPIPE, SIG_IGN);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0){ perror("socket"); return -1; }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        perror("bind"); close(sock); return -1;
    }
    if (listen(sock, 8) < 0){
        perror("listen"); close(sock); return -1;
    }
    fprintf(stderr, "[persona_host] listening on http://127.0.0.1:%d\n", port);

    while (!g_stop){
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int cfd = accept(sock, (struct sockaddr*)&client, &clen);
        if (cfd < 0){
            if (errno == EINTR) continue;
            break;
        }
        handle_connection(cfd, handler, ctx);
        close(cfd);
    }
    close(sock);
    fprintf(stderr, "[persona_host] shutting down\n");
    return 0;
}

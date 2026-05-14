#include "persona.h"
#include "persona_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

int pe_path_join(char *out, size_t n, const char *a, const char *b){
    size_t la = strlen(a);
    int sep = (la > 0 && a[la-1] != '/');
    int written = snprintf(out, n, "%s%s%s", a, sep ? "/" : "", b);
    return (written > 0 && (size_t)written < n) ? 0 : -1;
}

int pe_mkdir_p(const char *path){
    char tmp[512];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    for (size_t i = 1; i < len; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            tmp[i] = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

int pe_read_file(const char *path, void *buf, size_t n){
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    return (got == n) ? 0 : -1;
}

int pe_write_file_atomic(const char *path, const void *buf, size_t n){
    char tmp[512];
    int w = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (w <= 0 || (size_t)w >= sizeof(tmp)) return -1;
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    if (fwrite(buf, 1, n, f) != n) { fclose(f); unlink(tmp); return -1; }
    if (fflush(f) != 0)            { fclose(f); unlink(tmp); return -1; }
    fclose(f);
    if (rename(tmp, path) != 0)    { unlink(tmp); return -1; }
    return 0;
}

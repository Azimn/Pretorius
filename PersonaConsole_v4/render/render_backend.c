/* render_backend.c — registry + default selection.
 *
 * Backends register themselves at startup.  The runtime picks one by
 * name; if missing, falls back to "template".
 */
#include "render_backend.h"
#include <string.h>
#include <stdlib.h>

static RenderBackend *g_backends[PE_RENDER_MAX_BACKENDS];
static int            g_backend_count = 0;

int render_backend_register(RenderBackend *backend){
    if (!backend || !backend->name) return -1;
    if (g_backend_count >= PE_RENDER_MAX_BACKENDS) return -1;
    /* idempotent: if the same name is already registered, replace it */
    for (int i = 0; i < g_backend_count; ++i){
        if (g_backends[i] && !strcmp(g_backends[i]->name, backend->name)){
            g_backends[i] = backend;
            return 0;
        }
    }
    g_backends[g_backend_count++] = backend;
    return 0;
}

RenderBackend *render_backend_find(const char *name){
    if (!name) return NULL;
    for (int i = 0; i < g_backend_count; ++i){
        if (g_backends[i] && !strcmp(g_backends[i]->name, name))
            return g_backends[i];
    }
    return NULL;
}

int            render_backend_count(void){ return g_backend_count; }
RenderBackend *render_backend_at(int i){
    if (i < 0 || i >= g_backend_count) return NULL;
    return g_backends[i];
}

RenderBackend *render_backend_default(void){
    const char *want = getenv("PE_RENDER_BACKEND");
    if (want){
        RenderBackend *b = render_backend_find(want);
        if (b) return b;
    }
    /* preferred order: template first (always available), then slm */
    RenderBackend *b = render_backend_find("template");
    if (b) return b;
    return render_backend_count() > 0 ? render_backend_at(0) : NULL;
}

void render_backends_init(void){
    /* Idempotent — render_backend_register handles duplicate names. */
    render_backend_register(render_template_backend());
    render_backend_register(render_slm_backend());
}

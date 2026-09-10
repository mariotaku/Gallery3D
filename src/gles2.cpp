#include "gles2.h"

#include <SDL3/SDL.h>

#define GLES2_DEFINE(ret, name, args) PFN_##name name = nullptr;
GLES2_FUNCTIONS(GLES2_DEFINE)
#undef GLES2_DEFINE

static bool sRealES = false;

bool GLES2_IsRealES() {
    return sRealES;
}

void GLES2_SetRealES(bool isES) {
    sRealES = isES;
}

bool GLES2_Load() {
    bool ok = true;
#define GLES2_LOAD(ret, name, args)                                   \
    name = (PFN_##name)SDL_GL_GetProcAddress(#name);                  \
    if (!name) {                                                      \
        SDL_Log("GL entry point missing: %s", #name);                 \
        ok = false;                                                   \
    }
    GLES2_FUNCTIONS(GLES2_LOAD)
#undef GLES2_LOAD
    return ok;
}

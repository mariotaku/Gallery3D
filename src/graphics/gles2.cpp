#include "graphics/gles2.h"

#include <SDL3/SDL.h>

static bool sRealES = false;

bool GLES2_IsRealES() {
    return sRealES;
}

void GLES2_SetRealES(bool isES) {
    sRealES = isES;
}

bool GLES2_Load() {
    return gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress) != 0;
}

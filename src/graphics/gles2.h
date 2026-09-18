// GLAD's vcpkg GLES 2 declarations and loader. The same entry points also
// work with the desktop GL compatibility context used as a fallback.
#pragma once

#include <glad/glad.h>

// GL_EXT_texture_format_BGRA8888 uses the desktop GL_BGRA value. GLAD's
// GLES 2 profile omits extension tokens unless an extension feature requests
// them, so retain this platform-neutral spelling for the checked format.
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

// Loads GLAD from the current SDL GL context. This must happen after creating
// the context.
bool GLES2_Load();

// True when the context is a real ES context. Shaders need "#version 100" plus
// precision qualifiers there, and plain GLSL 1.20 on desktop GL.
bool GLES2_IsRealES();
void GLES2_SetRealES(bool isES);

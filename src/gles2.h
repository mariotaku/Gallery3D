// Minimal OpenGL ES 2.0 entry points, loaded through SDL.
//
// The port targets the ES2 API. On desktops that only expose a compatibility
// GL context the same entry points exist from GL 2.0 onward, so the loader
// accepts either and only the shader prelude changes. See gles2.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef void GLvoid;
typedef char GLchar;
typedef std::ptrdiff_t GLintptr;
typedef std::ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_OUT_OF_MEMORY 0x0505

#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000

#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005

#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_CULL_FACE 0x0B44
#define GL_DITHER 0x0BD0

#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303

#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_ALWAYS 0x0207

#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406

#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_UNSIGNED_SHORT_5_6_5 0x8363

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
// GL_EXT_texture_filter_anisotropic. Not core in ES 2.0, but present nearly
// everywhere, and it is what keeps a mipmapped thumbnail sharp when it is drawn
// close to its own size.
#define GL_EXTENSIONS 0x1F03
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_REPEAT 0x2901

#define GL_UNPACK_ALIGNMENT 0x0CF5

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

#define GL_DEPTH_BITS 0x0D56
#define GL_DEPTH_FUNC 0x0B74
#define GL_DEPTH_WRITEMASK 0x0B72
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

#define GL_APIENTRYP APIENTRY_PTR
#if defined(_WIN32)
#define GLES2_CALL __stdcall
#else
#define GLES2_CALL
#endif

// X-macro list of every entry point the port uses.
#define GLES2_FUNCTIONS(X)                                                                             \
    X(void, glActiveTexture, (GLenum texture))                                                         \
    X(void, glAttachShader, (GLuint program, GLuint shader))                                           \
    X(void, glBindAttribLocation, (GLuint program, GLuint index, const GLchar *name))                  \
    X(void, glBindBuffer, (GLenum target, GLuint buffer))                                              \
    X(void, glBindTexture, (GLenum target, GLuint texture))                                            \
    X(void, glBlendFunc, (GLenum sfactor, GLenum dfactor))                                             \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage))            \
    X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data))      \
    X(void, glClear, (GLbitfield mask))                                                                \
    X(void, glClearColor, (GLclampf r, GLclampf g, GLclampf b, GLclampf a))                            \
    X(void, glCompileShader, (GLuint shader))                                                          \
    X(GLuint, glCreateProgram, (void))                                                                 \
    X(GLuint, glCreateShader, (GLenum type))                                                           \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint *buffers))                                       \
    X(void, glDeleteProgram, (GLuint program))                                                         \
    X(void, glDeleteShader, (GLuint shader))                                                           \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *textures))                                     \
    X(void, glDepthFunc, (GLenum func))                                                                \
    X(void, glDepthMask, (GLboolean flag))                                                             \
    X(void, glDisable, (GLenum cap))                                                                   \
    X(void, glDisableVertexAttribArray, (GLuint index))                                                \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))                                   \
    X(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void *indices))            \
    X(void, glEnable, (GLenum cap))                                                                    \
    X(void, glEnableVertexAttribArray, (GLuint index))                                                 \
    X(void, glGenBuffers, (GLsizei n, GLuint *buffers))                                                \
    X(void, glGenerateMipmap, (GLenum target))                                                          \
    X(void, glGenTextures, (GLsizei n, GLuint *textures))                                              \
    X(GLenum, glGetError, (void))                                                                      \
    X(void, glGetFloatv, (GLenum pname, GLfloat *params))                                           \
    X(void, glGetIntegerv, (GLenum pname, GLint *params))                                          \
    X(GLboolean, glIsEnabled, (GLenum cap))                                                        \
    X(void, glGetProgramInfoLog, (GLuint p, GLsizei bufSize, GLsizei *length, GLchar *infoLog))        \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint *params))                             \
    X(void, glGetShaderInfoLog, (GLuint s, GLsizei bufSize, GLsizei *length, GLchar *infoLog))         \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint *params))                               \
    X(const GLubyte *, glGetString, (GLenum name))                                                     \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar *name))                               \
    X(void, glLinkProgram, (GLuint program))                                                           \
    X(void, glPixelStorei, (GLenum pname, GLint param))                                                \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type,            \
                          void *pixels))                                                               \
    X(void, glScissor, (GLint x, GLint y, GLsizei width, GLsizei height))                              \
    X(void, glShaderSource, (GLuint s, GLsizei count, const GLchar *const *str, const GLint *len))     \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width,            \
                           GLsizei height, GLint border, GLenum format, GLenum type,                   \
                           const void *pixels))                                                        \
    X(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param))                             \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))                               \
    X(void, glUniform1f, (GLint location, GLfloat v0))                                                 \
    X(void, glUniform1i, (GLint location, GLint v0))                                                   \
    X(void, glUniform4f, (GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))                  \
    X(void, glUniformMatrix4fv, (GLint loc, GLsizei count, GLboolean transpose, const GLfloat *value)) \
    X(void, glUseProgram, (GLuint program))                                                            \
    X(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized,       \
                                    GLsizei stride, const void *pointer))                              \
    X(void, glViewport, (GLint x, GLint y, GLsizei width, GLsizei height))

#define GLES2_DECLARE(ret, name, args) \
    typedef ret(GLES2_CALL *PFN_##name) args;  \
    extern PFN_##name name;
GLES2_FUNCTIONS(GLES2_DECLARE)
#undef GLES2_DECLARE

// Loads every entry point from the current GL context. Returns false and logs
// the first missing name if the context cannot supply one.
bool GLES2_Load();

// True when the context is a real ES context. Shaders need "#version 100" plus
// precision qualifiers there, and plain GLSL 1.20 on desktop GL.
bool GLES2_IsRealES();
void GLES2_SetRealES(bool isES);

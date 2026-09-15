#include "graphics/RenderView.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "app/App.h"
#include "graphics/Layer.h"
#include "core/Shared.h"

namespace {

// Fills the part of the bound power of two texture past the bitmap uploaded in
// its corner. A texture that builds mipmaps repeats its edge pixels there,
// since a reduced level averages across the boundary. Any other is left
// transparent, which a quad drawn to the bitmap's extents never samples.
// Filled here on the GPU rather than by copying the whole bitmap into a padded
// one first.
void uploadPadding(const Bitmap &bitmap, int paddedWidth, int paddedHeight, bool clampEdges, GLenum format) {
    const int width = bitmap.width();
    const int height = bitmap.height();
    // Right of the bitmap, beside each of its rows.
    if (paddedWidth > width) {
        const int extra = paddedWidth - width;
        std::vector<uint8_t> strip((size_t)extra * (size_t)height * 4, 0);
        if (clampEdges) {
            for (int y = 0; y < height; ++y) {
                const uint8_t *last = bitmap.pixels() + ((size_t)y * (size_t)width + (size_t)width - 1) * 4;
                uint8_t *row = strip.data() + (size_t)y * (size_t)extra * 4;
                for (int x = 0; x < extra; ++x) {
                    std::memcpy(row + (size_t)x * 4, last, 4);
                }
            }
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, width, 0, extra, height, format, GL_UNSIGNED_BYTE, strip.data());
    }
    // Below it, across the whole width: one row, uploaded once for each row it
    // covers.
    if (paddedHeight > height) {
        std::vector<uint8_t> row((size_t)paddedWidth * 4, 0);
        if (clampEdges) {
            const uint8_t *last = bitmap.pixels() + (size_t)(height - 1) * (size_t)width * 4;
            std::memcpy(row.data(), last, (size_t)width * 4);
            for (int x = width; x < paddedWidth; ++x) {
                std::memcpy(row.data() + (size_t)x * 4, last + (size_t)(width - 1) * 4, 4);
            }
        }
        for (int y = height; y < paddedHeight; ++y) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, paddedWidth, 1, format, GL_UNSIGNED_BYTE, row.data());
        }
    }
}

const char *const kVertexSingle = R"(
attribute vec4 aPosition;
attribute vec2 aTexCoord0;
uniform mat4 uMVP;
varying vec2 vTexCoord0;
void main() {
    vTexCoord0 = aTexCoord0;
    gl_Position = uMVP * aPosition;
}
)";

// Shared by both fragment shaders. Interleaved gradient noise: a fixed pattern
// over the screen whose values spread evenly, so a colour between two 8-bit
// steps comes out as a fine mix of both instead of a band. uDither is 1 for a
// backdrop stretched far past its texels, and 0 for everything else.
const char *const kFragmentDither = R"(
uniform float uDither;
vec3 dither(vec3 color) {
    HIGHP vec2 pixel = gl_FragCoord.xy;
    HIGHP float noise = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
    return color + (noise - 0.5) * (uDither / 255.0);
}
)";

const char *const kFragmentSingle = R"(
uniform sampler2D uTex0;
uniform vec4 uColor;
varying vec2 vTexCoord0;
void main() {
    vec4 color = texture2D(uTex0, vTexCoord0) * uColor;
    gl_FragColor = vec4(dither(color.rgb), color.a);
}
)";

const char *const kVertexMix = R"(
attribute vec4 aPosition;
attribute vec2 aTexCoord0;
attribute vec2 aTexCoord1;
uniform mat4 uMVP;
varying vec2 vTexCoord0;
varying vec2 vTexCoord1;
void main() {
    vTexCoord0 = aTexCoord0;
    vTexCoord1 = aTexCoord1;
    gl_Position = uMVP * aPosition;
}
)";

// Reproduces the fixed function chain the original set up: unit 0 modulates by
// the primary colour, then unit 1 interpolates between that and its own texture
// with the constant alpha as the factor.
const char *const kFragmentMix = R"(
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform vec4 uColor;
uniform float uRatio;
varying vec2 vTexCoord0;
varying vec2 vTexCoord1;
void main() {
    vec4 previous = texture2D(uTex0, vTexCoord0) * uColor;
    vec4 current = texture2D(uTex1, vTexCoord1);
    vec4 color = mix(previous, current, uRatio);
    gl_FragColor = vec4(dither(color.rgb), color.a);
}
)";

std::string shaderPrelude(bool fragment) {
    if (GLES2_IsRealES()) {
        // HIGHP for the dither's screen coordinates, which lose their fraction
        // at mediump on a tall screen. Not every ES fragment shader has it.
        return fragment ? "#version 100\nprecision mediump float;\n"
                          "#ifdef GL_FRAGMENT_PRECISION_HIGH\n#define HIGHP highp\n#else\n#define HIGHP mediump\n#endif\n"
                        : "#version 100\n";
    }
    // GLSL 1.20 has no precision qualifiers, and its floats are full width.
    return fragment ? "#version 120\n#define HIGHP\n" : "#version 120\n";
}

}  // namespace

RenderView::RenderView() : mModelView(32) {
    for (int i = 0; i < NUM_TEXTURE_LOAD_THREADS; ++i) {
        mThreadIsLoading[i].store(false);
    }
}

RenderView::~RenderView() {
    shutdown();
}

// Programs

RenderView::Program RenderView::buildProgram(const char *vertexSource, const char *fragmentSource) {
    Program program;

    auto compile = [](GLenum type, const std::string &source) -> GLuint {
        GLuint shader = glCreateShader(type);
        const char *text = source.c_str();
        GLint length = (GLint)source.length();
        glShaderSource(shader, 1, &text, &length);
        glCompileShader(shader);
        GLint status = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) {
            char log[1024] = {0};
            glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
            SDL_Log("Shader compile failed: %s", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vertex = compile(GL_VERTEX_SHADER, shaderPrelude(false) + vertexSource);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, shaderPrelude(true) + kFragmentDither + fragmentSource);
    if (vertex == 0 || fragment == 0) {
        return program;
    }

    program.id = glCreateProgram();
    glAttachShader(program.id, vertex);
    glAttachShader(program.id, fragment);
    // Fixed slots so both programs share one attribute layout.
    glBindAttribLocation(program.id, 0, "aPosition");
    glBindAttribLocation(program.id, 1, "aTexCoord0");
    glBindAttribLocation(program.id, 2, "aTexCoord1");
    glLinkProgram(program.id);

    GLint status = GL_FALSE;
    glGetProgramiv(program.id, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        char log[1024] = {0};
        glGetProgramInfoLog(program.id, sizeof(log) - 1, nullptr, log);
        SDL_Log("Program link failed: %s", log);
        glDeleteProgram(program.id);
        program.id = 0;
    } else {
        program.uMVP = glGetUniformLocation(program.id, "uMVP");
        program.uColor = glGetUniformLocation(program.id, "uColor");
        program.uTex0 = glGetUniformLocation(program.id, "uTex0");
        program.uTex1 = glGetUniformLocation(program.id, "uTex1");
        program.uRatio = glGetUniformLocation(program.id, "uRatio");
        program.uDither = glGetUniformLocation(program.id, "uDither");
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

bool RenderView::buildPrograms() {
    mSingleProgram = buildProgram(kVertexSingle, kFragmentSingle);
    mMixProgram = buildProgram(kVertexMix, kFragmentMix);
    if (mSingleProgram.id == 0 || mMixProgram.id == 0) {
        return false;
    }
    glUseProgram(mSingleProgram.id);
    glUniform1i(mSingleProgram.uTex0, 0);
    glUseProgram(mMixProgram.id);
    glUniform1i(mMixProgram.uTex0, 0);
    glUniform1i(mMixProgram.uTex1, 1);
    glUseProgram(0);
    return true;
}

void RenderView::useProgram(const Program &program) {
    if (mCurrentProgram != &program) {
        mCurrentProgram = &program;
        glUseProgram(program.id);
    }
}

void RenderView::applyUniforms() {
    const Program &program = mMixing ? mMixProgram : mSingleProgram;
    useProgram(program);

    Mat4 mvp = mProjectionMatrix;
    mvp.multiply(mModelView.top());
    glUniformMatrix4fv(program.uMVP, 1, GL_FALSE, mvp.m);
    glUniform4f(program.uColor, mColor[0], mColor[1], mColor[2], mColor[3]);
    glUniform1f(program.uDither, mDither ? 1.0f : 0.0f);
    if (mMixing) {
        glUniform1f(program.uRatio, mMixRatio);
    }
}

// Lifecycle

bool RenderView::init(SDL_Window *window) {
    mWindow = window;
    if (!buildPrograms()) {
        return false;
    }
    glGenBuffers(1, &mQuad2DVBO);

    // Build mipmaps only with anisotropic filtering to preserve thumbnail sharpness.
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    if (extensions != nullptr && SDL_strstr(extensions, "GL_EXT_texture_filter_anisotropic") != nullptr) {
        GLfloat maxAnisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
        // Cap anisotropy at four.
        mMaxAnisotropy = (maxAnisotropy < 4.0f) ? maxAnisotropy : 4.0f;
    }
    SDL_Log("Anisotropic filtering %s (max %.1f)", (mMaxAnisotropy > 1.0f) ? "on" : "unavailable",
            mMaxAnisotropy);
    // A BGRA bitmap, which is what WIC decodes to, goes to the GPU as it is
    // where the context takes that order: desktop GL always, GL ES with an
    // extension. Anywhere else it is swapped to RGBA on upload.
    const char *version = (const char *)glGetString(GL_VERSION);
    if (version != nullptr && SDL_strstr(version, "OpenGL ES") == nullptr) {
        mBgraInternalFormat = GL_RGBA;
    } else if (extensions != nullptr && SDL_strstr(extensions, "GL_EXT_texture_format_BGRA8888") != nullptr) {
        mBgraInternalFormat = GL_BGRA;
    }
    SDL_Log("BGRA textures %s", (mBgraInternalFormat != 0) ? "upload as they are" : "are swapped to RGBA first");

    mLoadThreadsRunning.store(true);
    for (int i = 0; i < NUM_TEXTURE_LOAD_THREADS; ++i) {
        mLoadThreads.emplace_back([this, i]() {
            textureLoadThread(i);
            // SDL frees its per-thread state only for threads it started itself.
            SDL_CleanupTLS();
        });
    }
    return true;
}

void RenderView::shutdown() {
    if (mLoadThreadsRunning.exchange(false)) {
        mQueueCondition.notify_all();
        for (std::thread &thread : mLoadThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        mLoadThreads.clear();
    }
    mRootLayer = nullptr;
    mLists.clear();
    mCacheScaled.clear();
    mCacheUnscaled.clear();
}

void RenderView::setRootLayer(RootLayer *layer) {
    if (mRootLayer != layer) {
        mRootLayer = layer;
        mListsDirty = true;
        if (layer) {
            layer->setSize((float)mViewWidth, (float)mViewHeight);
        }
    }
}

void RenderView::onSurfaceCreated() {
    clearCache();

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // Premultiplied alpha, as in the original.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    resetColor();

    if (mRootLayer) {
        mRootLayer->onSurfaceCreated(this);
    }
    for (int i = (int)mLists.systemList.size() - 1; i >= 0; --i) {
        mLists.systemList[(size_t)i]->onSurfaceCreated(this);
    }
}

void RenderView::onSurfaceChanged(int width, int height) {
    mViewWidth = width;
    mViewHeight = height;
    if (mRootLayer) {
        mRootLayer->setSize((float)width, (float)height);
    }

    glViewport(0, 0, width, height);
    setFov(mFov);
    // Top-left screen projection; draw2D z passes unchanged into the depth buffer,
    // matching glDrawTexOES and BackgroundLayer's far-plane placement.
    mOrtho = Mat4::ortho(0.0f, (float)width, (float)height, 0.0f, 0.0f, -1.0f);

    if (mRootLayer) {
        mRootLayer->onSurfaceChanged(this, width, height);
    }
}

void RenderView::setFov(float fov) {
    mFov = fov;
    float aspect = (mViewHeight == 0) ? 1.0f : (float)mViewWidth / (float)mViewHeight;
    mPerspective = Mat4::perspective(fov, aspect, 0.1f, 100.0f);
    mProjectionMatrix = mPerspective;
}

void RenderView::onDrawFrame() {
    if (mListsDirty) {
        updateLists();
    }

    bool wasLoadingExpensiveTextures = isLoadingExpensiveTextures();
    bool loadingExpensiveTextures = false;
    for (int i = 2; i < NUM_TEXTURE_LOAD_THREADS; ++i) {
        if (mThreadIsLoading[i].load()) {
            loadingExpensiveTextures = true;
            break;
        }
    }
    if (loadingExpensiveTextures != wasLoadingExpensiveTextures) {
        mLoadingExpensiveTexturesStartTime = loadingExpensiveTextures ? (int64_t)SDL_GetTicks() : 0;
    }

    ++mFrameCounter;
    processTextures(false);
    enforceTextureBudget();

    // Use nanosecond intervals: millisecond ticks can report zero on nonblocking swaps.
    uint64_t nowNs = SDL_GetTicksNS();
    uint64_t deltaNs = (mFrameTimeNs != 0 && nowNs > mFrameTimeNs) ? (nowNs - mFrameTimeNs) : 0;
    mFrameTimeNs = nowNs;
    // Still capped at 50ms, so a stall does not teleport the wall.
    const uint64_t maxDeltaNs = 50ull * 1000ull * 1000ull;
    if (deltaNs > maxDeltaNs) {
        deltaNs = maxDeltaNs;
    }
    mFrameInterval = (float)((double)deltaNs / 1000000000.0);
    // Kept in milliseconds: FloatAnim measures its durations against this.
    mFrameTime = SDL_GetTicks();

    processTouchEvents();

    // Update pass.
    bool isDirty = false;
    for (Layer *layer : mLists.updateList) {
        isDirty |= layer->update(this, mFrameInterval);
    }
    if (isDirty) {
        requestRender();
    }

    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, mViewWidth, mViewHeight);

    // Opaque pass.
    glDisable(GL_BLEND);
    for (int i = (int)mLists.opaqueList.size() - 1; i >= 0; --i) {
        Layer *layer = mLists.opaqueList[(size_t)i];
        if (!layer->mHidden) {
            layer->renderOpaque(this);
        }
    }

    // Blended pass.
    glEnable(GL_BLEND);
    for (Layer *layer : mLists.blendedList) {
        if (!layer->mHidden) {
            layer->renderBlended(this);
        }
    }
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
}

void RenderView::updateLists() {
    mListsDirty = false;
    if (mRootLayer) {
        mLists.clear();
        mRootLayer->generate(this, mLists);
    }
}

// Input

void RenderView::queueTouchEvent(const MotionEvent &event) {
    if (mTouchEventQueue.size() > 8 && event.action == MotionEvent::ACTION_MOVE) {
        return;
    }
    mTouchEventQueue.push_back(event);
    requestRender();
}

void RenderView::queuePointerMove(float x, float y) {
    if (mPointerMoved && mPointerX == x && mPointerY == y) {
        return;
    }
    mPointerX = x;
    mPointerY = y;
    mPointerMoved = true;
    requestRender();
}

void RenderView::queueAccelerometer(float x, float y, float z) {
    mAccelX = x;
    mAccelY = y;
    mAccelZ = z;
    mAccelChanged = true;
}

void RenderView::processTouchEvents() {
    if (mAccelChanged) {
        mAccelChanged = false;
        if (mRootLayer != nullptr) {
            mRootLayer->onAccelerometer(mAccelX, mAccelY, mAccelZ);
        }
    }
    if (mPointerMoved) {
        mPointerMoved = false;
        if (mRootLayer != nullptr) {
            mRootLayer->onPointerMoved(mPointerX, mPointerY);
        }
    }

    size_t numEvents = mTouchEventQueue.size();
    for (size_t i = 0; i < numEvents; ++i) {
        if (mTouchEventQueue.empty()) {
            return;
        }
        MotionEvent event = mTouchEventQueue.front();
        mTouchEventQueue.pop_front();

        Layer *target;
        if (event.action == MotionEvent::ACTION_DOWN) {
            target = hitTest(event.getX(), event.getY());
            mTouchEventTarget = target;
        } else {
            target = mTouchEventTarget;
        }
        if (target) {
            target->onTouchEvent(event);
        }
        if (event.action == MotionEvent::ACTION_UP || event.action == MotionEvent::ACTION_CANCEL) {
            mTouchEventTarget = nullptr;
        }
    }
}

bool RenderView::dispatchKeyDown(int keyCode, const KeyEvent &event) {
    if (!mRootLayer) {
        return false;
    }
    requestRender();
    return mRootLayer->onKeyDown(keyCode, event);
}

Layer *RenderView::hitTest(float x, float y) {
    for (int i = (int)mLists.hitTestList.size() - 1; i >= 0; --i) {
        Layer *layer = mLists.hitTestList[(size_t)i];
        if (layer && !layer->mHidden) {
            float layerX = layer->mX;
            float layerY = layer->mY;
            if (x >= layerX && y >= layerY && x < layerX + layer->mWidth && y < layerY + layer->mHeight &&
                layer->containsPoint(x, y)) {
                return layer;
            }
        }
    }
    return nullptr;
}

// Textures

TexturePtr RenderView::getResource(const std::string &name, bool scaled) {
    std::map<std::string, TexturePtr> &cache = scaled ? mCacheScaled : mCacheUnscaled;
    auto it = cache.find(name);
    if (it != cache.end()) {
        return it->second;
    }
    if (name.empty()) {
        return nullptr;
    }
    TexturePtr texture = std::make_shared<ResourceTexture>(name, scaled);
    texture->mOwner = this;
    cache[name] = texture;
    return texture;
}

void RenderView::clearCache() {
    mCacheScaled.clear();
    mCacheUnscaled.clear();
}

void RenderView::queueDeleteTexture(GLuint textureId) {
    std::lock_guard<std::mutex> lock(mDeleteMutex);
    mPendingTextureDeletes.push_back(textureId);
}

bool RenderView::isLoadingExpensiveTextures() const {
    return mLoadingExpensiveTexturesStartTime != 0;
}

int64_t RenderView::elapsedLoadingExpensiveTextures() const {
    if (mLoadingExpensiveTexturesStartTime != 0) {
        return (int64_t)SDL_GetTicks() - mLoadingExpensiveTexturesStartTime;
    }
    return -1;
}

void RenderView::handleLowMemory() {
    if (mRootLayer) {
        mRootLayer->handleLowMemory();
    }
}

void RenderView::prime(const TexturePtr &texture, bool highPriority) {
    if (texture && texture->mState == Texture::STATE_UNLOADED &&
        (highPriority || mLoadingCount < MAX_LOADING_COUNT)) {
        queueLoad(texture, highPriority);
    }
}

void RenderView::loadTexture(const TexturePtr &texture) {
    if (!texture) {
        return;
    }
    if (texture->mState == Texture::STATE_UNLOADED || texture->mState == Texture::STATE_QUEUED) {
        texture->mState = Texture::STATE_LOADING;
        loadTextureAsync(texture);
        uploadTexture(texture);
    }
}

void RenderView::queueLoad(const TexturePtr &texture, bool highPriority) {
    if (!texture->shouldQueue()) {
        return;
    }
    texture->mState = Texture::STATE_LOADING;
    texture->mOwner = this;

    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        std::deque<TexturePtr> &inputQueue = texture->isUncachedVideo() ? mLoadInputQueueVideo
                                             : texture->isCached()     ? mLoadInputQueueCached
                                                                       : mLoadInputQueue;
        if (highPriority) {
            inputQueue.push_front(texture);
            // Keep the loading count bounded by dropping the oldest request.
            if (mLoadingCount >= MAX_LOADING_COUNT && inputQueue.size() > 1) {
                TexturePtr unloadTexture = inputQueue.back();
                inputQueue.pop_back();
                unloadTexture->mState = Texture::STATE_UNLOADED;
                --mLoadingCount;
            }
        } else {
            inputQueue.push_back(texture);
        }
    }
    mQueueCondition.notify_one();
    ++mLoadingCount;
}

void RenderView::applyBitmap(const TexturePtr &texture, Bitmap bitmap) {
    texture->mHasAlpha = bitmap.valid() && bitmap.hasTransparency();
    if (bitmap.valid()) {
        int width = bitmap.width();
        int height = bitmap.height();
        texture->mWidth = width;
        texture->mHeight = height;
        // The texture is a power of two, so the normalized extents the meshes
        // use stay meaningful and wrap modes behave everywhere. uploadTexture
        // puts the bitmap in its corner and fills the rest.
        if (!Shared::isPowerOf2(width) || !Shared::isPowerOf2(height)) {
            int paddedWidth = Shared::nextPowerOf2(width);
            int paddedHeight = Shared::nextPowerOf2(height);
            texture->mNormalizedWidth = (float)width / (float)paddedWidth;
            texture->mNormalizedHeight = (float)height / (float)paddedHeight;
        } else {
            texture->mNormalizedWidth = 1.0f;
            texture->mNormalizedHeight = 1.0f;
        }
    }
    texture->mBitmap = std::move(bitmap);
}

void RenderView::loadTextureAsync(const TexturePtr &texture) {
    applyBitmap(texture, texture->load(this));
}

void RenderView::finishLoad(const TexturePtr &texture, Bitmap bitmap) {
    // Collect finished pixels from the decode threads. Only the render thread
    // drains uploads and touches GL.
    applyBitmap(texture, std::move(bitmap));
    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        mLoadOutputQueue.push_back(texture);
    }
    requestRender();
}

void RenderView::uploadTexture(const TexturePtr &texture) {
    const int width = texture->mBitmap.valid() ? texture->mBitmap.width() : 0;
    const int height = texture->mBitmap.valid() ? texture->mBitmap.height() : 0;
    if (!texture->mBitmap.valid()) {
        texture->mState = Texture::STATE_ERROR;
        texture->mBitmap = Bitmap();
        return;
    }

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    const bool repeat = texture->wantsRepeat();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    // Every texture is a power of two, which is also the only size ES 2.0
    // builds a mip chain for.
    const int paddedWidth = Shared::nextPowerOf2(width);
    const int paddedHeight = Shared::nextPowerOf2(height);
    const bool clampEdges = texture->wantsMipmaps();
    const bool mipmapped = clampEdges && mMaxAnisotropy > 1.0f;
    const GLint minFilter = mipmapped ? GL_LINEAR_MIPMAP_LINEAR : (repeat ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, repeat ? GL_NEAREST : GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    Bitmap &bitmap = texture->mBitmap;
    GLint internalFormat = GL_RGBA;
    GLenum format = GL_RGBA;
    if (bitmap.order() == PixelOrder::BGRA) {
        if (mBgraInternalFormat != 0) {
            internalFormat = mBgraInternalFormat;
            format = GL_BGRA;
        } else {
            bitmap.reorder(PixelOrder::RGBA);
        }
    }
    if (paddedWidth == width && paddedHeight == height) {
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, bitmap.pixels());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, paddedWidth, paddedHeight, 0, format, GL_UNSIGNED_BYTE,
                     nullptr);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, GL_UNSIGNED_BYTE, bitmap.pixels());
        uploadPadding(bitmap, paddedWidth, paddedHeight, clampEdges, format);
    }
    if (mipmapped) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, mMaxAnisotropy);
    }
    GLenum error = glGetError();

    texture->mBitmap = Bitmap();
    mBoundTexture = nullptr;

    if (error != GL_NO_ERROR) {
        if (error == GL_OUT_OF_MEMORY) {
            handleLowMemory();
        }
        SDL_Log("Texture creation failed, glError 0x%04x", error);
        glDeleteTextures(1, &textureId);
        texture->mId = 0;
        texture->mState = Texture::STATE_UNLOADED;
    } else {
        texture->mId = textureId;
        texture->mOwner = this;
        texture->mState = Texture::STATE_LOADED;
        // A full chain is a third again on top of the base level, and the
        // budget has to know or it will hold a third more than it thinks.
        texture->mBytes = (size_t)paddedWidth * (size_t)paddedHeight * 4;
        if (mipmapped) {
            texture->mBytes += texture->mBytes / 3;
        }
        texture->mLastUsedMs = SDL_GetTicks();
        mTextureBytes += texture->mBytes;
        mLiveTextures.push_back(texture);
        requestRender();
    }
}

void RenderView::processTextures(bool processAll) {
    {
        std::lock_guard<std::mutex> lock(mDeleteMutex);
        if (!mPendingTextureDeletes.empty()) {
            glDeleteTextures((GLsizei)mPendingTextureDeletes.size(), mPendingTextureDeletes.data());
            mPendingTextureDeletes.clear();
        }
    }

    do {
        TexturePtr texture;
        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            if (!mLoadOutputQueue.empty()) {
                texture = mLoadOutputQueue.front();
                mLoadOutputQueue.pop_front();
            }
        }
        if (!texture) {
            break;
        }
        uploadTexture(texture);
        --mLoadingCount;
    } while (processAll);
}

void RenderView::enforceTextureBudget() {
    // Evict least-recently-bound textures above the memory budget.
    const size_t kBudgetBytes = 192u * 1024u * 1024u;
    // Never drop something drawn in the last couple of seconds, or scrolling
    // would evict and reload the same thumbnails as it goes.
    const uint64_t kKeepMs = 2000;
    const uint64_t nowMs = SDL_GetTicks();

    // Prune the entries whose textures have gone and total up what is left.
    // The list holds weak references, so it never keeps a texture alive.
    std::vector<TexturePtr> alive;
    alive.reserve(mLiveTextures.size());
    size_t total = 0;
    for (size_t i = 0; i < mLiveTextures.size();) {
        TexturePtr texture = mLiveTextures[i].lock();
        if (!texture || texture->mId == 0 || texture->mState != Texture::STATE_LOADED) {
            mLiveTextures[i] = mLiveTextures.back();
            mLiveTextures.pop_back();
            continue;
        }
        total += texture->mBytes;
        alive.push_back(std::move(texture));
        ++i;
    }
    mTextureBytes = total;
    if (total <= kBudgetBytes) {
        return;
    }

    // Oldest first, and stop once there is comfortable headroom so this does
    // not run again on the very next frame.
    std::sort(alive.begin(), alive.end(), [](const TexturePtr &a, const TexturePtr &b) {
        return a->mLastUsedMs < b->mLastUsedMs;
    });
    const size_t targetBytes = kBudgetBytes - kBudgetBytes / 10;
    for (const TexturePtr &texture : alive) {
        if (mTextureBytes <= targetBytes) {
            break;
        }
        if (texture->mLastUsedMs + kKeepMs > nowMs) {
            // Everything from here on is hotter still.
            break;
        }
        queueDeleteTexture(texture->mId);
        mTextureBytes -= texture->mBytes;
        texture->mId = 0;
        texture->mBytes = 0;
        // Back to unloaded, so the normal path reloads it if it is needed
        // again. clear() would also throw away the size the meshes read.
        texture->mState = Texture::STATE_UNLOADED;
    }
}

void RenderView::processAllTextures() {
    processTextures(true);
}

void RenderView::textureLoadThread(int index) {
    // Below the render thread. Four of these decoding full size photos will
    // take every core they are given, and at equal priority they take them from
    // the thread drawing the wall: opening an album cost the render thread a
    // fifth of a second on the run queue, measured through schedstat on a six
    // core phone. A tile arriving a frame later is not something a viewer sees;
    // the wall stopping is.
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);

    // Thread 0 drains the cached queue and thread 1 the video queue, matching
    // the original assignment.
    while (mLoadThreadsRunning.load()) {
        TexturePtr texture;
        {
            std::unique_lock<std::mutex> lock(mQueueMutex);
            auto pick = [&]() -> std::deque<TexturePtr> * {
                if (index == 0 && !mLoadInputQueueCached.empty()) {
                    return &mLoadInputQueueCached;
                }
                if (index == 1 && !mLoadInputQueueVideo.empty()) {
                    return &mLoadInputQueueVideo;
                }
                if (index >= 2 && !mLoadInputQueue.empty()) {
                    return &mLoadInputQueue;
                }
                // Idle threads help out with whatever is waiting.
                if (!mLoadInputQueue.empty()) {
                    return &mLoadInputQueue;
                }
                if (!mLoadInputQueueCached.empty()) {
                    return &mLoadInputQueueCached;
                }
                if (!mLoadInputQueueVideo.empty()) {
                    return &mLoadInputQueueVideo;
                }
                return nullptr;
            };
            std::deque<TexturePtr> *queue = pick();
            while (queue == nullptr && mLoadThreadsRunning.load()) {
                mQueueCondition.wait(lock);
                queue = pick();
            }
            if (!mLoadThreadsRunning.load()) {
                return;
            }
            texture = queue->front();
            queue->pop_front();
            ++mLoadsRunning;
        }

        if (index != 0) {
            mThreadIsLoading[index].store(true);
        }
        texture->startLoad(this, texture);
        mThreadIsLoading[index].store(false);
        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            --mLoadsRunning;
        }
        mLoadsFinished.notify_all();
    }
}

void RenderView::cancelLoads() {
    std::unique_lock<std::mutex> lock(mQueueMutex);
    for (std::deque<TexturePtr> *queue : {&mLoadInputQueue, &mLoadInputQueueCached, &mLoadInputQueueVideo}) {
        for (const TexturePtr &texture : *queue) {
            texture->mState = Texture::STATE_UNLOADED;
            --mLoadingCount;
        }
        queue->clear();
    }
    mLoadsFinished.wait(lock, [this]() { return mLoadsRunning == 0; });
}

// Binding and colour

bool RenderView::bind(const TexturePtr &texture) {
    if (!texture) {
        return false;
    }
    if (texture.get() == mBoundTexture) {
        return true;
    }
    switch (texture->mState) {
    case Texture::STATE_UNLOADED:
        if (std::dynamic_pointer_cast<ResourceTexture>(texture)) {
            loadTexture(texture);
            return false;
        }
        if (mLoadingCount < MAX_LOADING_COUNT) {
            queueLoad(texture, false);
        }
        break;
    case Texture::STATE_LOADED:
        glBindTexture(GL_TEXTURE_2D, texture->mId);
        mBoundTexture = texture.get();
        texture->mLastUsedMs = SDL_GetTicks();
        return true;
    default:
        break;
    }
    return false;
}

bool RenderView::bindMixed(const TexturePtr &from, const TexturePtr &to, float ratio) {
    bool bound = bind(from);
    glActiveTexture(GL_TEXTURE1);
    mBoundTexture = nullptr;
    bound = bind(to) && bound;
    glActiveTexture(GL_TEXTURE0);
    mBoundTexture = nullptr;
    if (!bound) {
        return false;
    }
    // Re-bind unit 0 now that the active unit is back where it started.
    if (!bind(from)) {
        return false;
    }
    mMixing = true;
    mMixRatio = ratio;
    // Kept so draw2D can give attribute 2 the extents of the second texture.
    mBoundTextureMixed = to.get();
    glEnableVertexAttribArray(2);
    return true;
}

void RenderView::unbindMixed() {
    mMixing = false;
    mBoundTextureMixed = nullptr;
    glDisableVertexAttribArray(2);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    mBoundTexture = nullptr;
}

void RenderView::setAlpha(float alpha) {
    mColor[0] = alpha;
    mColor[1] = alpha;
    mColor[2] = alpha;
    mColor[3] = alpha;
    mAlpha = alpha;
}

void RenderView::setColor(float red, float green, float blue, float alpha) {
    mColor[0] = red;
    mColor[1] = green;
    mColor[2] = blue;
    mColor[3] = alpha;
    mAlpha = alpha;
}

void RenderView::resetColor() {
    mColor[0] = 1.0f;
    mColor[1] = 1.0f;
    mColor[2] = 1.0f;
    mColor[3] = 1.0f;
    mAlpha = 1.0f;
}

// Matrix stack

void RenderView::glLoadIdentity() {
    mModelView.glLoadIdentity();
}

void RenderView::glTranslatef(float x, float y, float z) {
    mModelView.glTranslatef(x, y, z);
}

void RenderView::glRotatef(float angle, float x, float y, float z) {
    mModelView.glRotatef(angle, x, y, z);
}

void RenderView::glScalef(float x, float y, float z) {
    mModelView.glScalef(x, y, z);
}

void RenderView::glPushMatrix() {
    mModelView.glPushMatrix();
}

void RenderView::glPopMatrix() {
    mModelView.glPopMatrix();
}

void RenderView::gluLookAt(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ, float upX,
                           float upY, float upZ) {
    mModelView.glMultMatrixf(Mat4::lookAt(eyeX, eyeY, eyeZ, centerX, centerY, centerZ, upX, upY, upZ));
}

// Raw state

void RenderView::enableBlend(bool enable) {
    if (enable) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
}

void RenderView::blendFunc(GLenum src, GLenum dst) {
    glBlendFunc(src, dst);
}

void RenderView::depthFunc(GLenum func) {
    glDepthFunc(func);
}

// Geometry

void RenderView::bindVertexBuffer(GLuint vbo) {
    mVertexVBO = vbo;
}

void RenderView::bindTexCoordBuffer(int unit, GLuint vbo) {
    if (unit == 0 || unit == 1) {
        mTexCoordVBO[unit] = vbo;
    }
}

void RenderView::bindIndexBuffer(GLuint ibo) {
    mIndexVBO = ibo;
}

void RenderView::unbindBuffers() {
    mVertexVBO = 0;
    mTexCoordVBO[0] = 0;
    mTexCoordVBO[1] = 0;
    mIndexVBO = 0;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void RenderView::drawElements(GLenum mode, GLsizei count, size_t byteOffset) {
    if (mVertexVBO == 0 || mIndexVBO == 0) {
        return;
    }
    applyUniforms();

    glBindBuffer(GL_ARRAY_BUFFER, mVertexVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, mTexCoordVBO[0]);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    if (mMixing) {
        glBindBuffer(GL_ARRAY_BUFFER, mTexCoordVBO[1]);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIndexVBO);
    glDrawElements(mode, count, GL_UNSIGNED_SHORT, (const void *)byteOffset);
}

// 2D drawing, replacing OES_draw_texture

void RenderView::draw2D(float x, float y, float z, float width, float height) {
    // Art drawn a whole number of pixels wide and tall is drawn at the size it
    // was rendered, so it goes on whole pixels too. Half a pixel off, linear
    // filtering blends every texel with its neighbour and the art goes soft.
    const float roundedWidth = (float)(long)(width + 0.5f);
    const float roundedHeight = (float)(long)(height + 0.5f);
    if (width - roundedWidth < 0.001f && roundedWidth - width < 0.001f && height - roundedHeight < 0.001f &&
        roundedHeight - height < 0.001f) {
        x = (float)(long)(x + (x < 0.0f ? -0.5f : 0.5f));
        y = (float)(long)(y + (y < 0.0f ? -0.5f : 0.5f));
    }
    // Six floats per vertex: position then texture coordinate, in two arrays
    // because the attributes read from separate buffers.
    float u = 1.0f;
    float v = 1.0f;
    if (mBoundTexture) {
        u = mBoundTexture->getNormalizedWidth();
        v = mBoundTexture->getNormalizedHeight();
    }
    const float positions[] = {
        x,         y,          z,  // top left
        x + width, y,          z,  // top right
        x,         y + height, z,  // bottom left
        x + width, y + height, z,  // bottom right
    };
    const float texCoords[] = {
        0.0f, 0.0f, u, 0.0f, 0.0f, v, u, v,
    };
    // The mix program samples a second texture, which needs its own extents.
    float u1 = 1.0f;
    float v1 = 1.0f;
    if (mMixing && mBoundTextureMixed) {
        u1 = mBoundTextureMixed->getNormalizedWidth();
        v1 = mBoundTextureMixed->getNormalizedHeight();
    }
    const float texCoords1[] = {
        0.0f, 0.0f, u1, 0.0f, 0.0f, v1, u1, v1,
    };

    Mat4 savedProjection = mProjectionMatrix;
    Mat4 savedModelView = mModelView.top();
    mProjectionMatrix = mOrtho;
    mModelView.glLoadIdentity();
    applyUniforms();

    glBindBuffer(GL_ARRAY_BUFFER, mQuad2DVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(positions) + sizeof(texCoords) + sizeof(texCoords1), nullptr,
                 GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(positions), positions);
    glBufferSubData(GL_ARRAY_BUFFER, sizeof(positions), sizeof(texCoords), texCoords);
    glBufferSubData(GL_ARRAY_BUFFER, sizeof(positions) + sizeof(texCoords), sizeof(texCoords1), texCoords1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (const void *)sizeof(positions));
    if (mMixing) {
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0,
                              (const void *)(sizeof(positions) + sizeof(texCoords)));
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    mProjectionMatrix = savedProjection;
    mModelView.glLoadMatrixf(savedModelView);
}

void RenderView::draw2D(const TexturePtr &texture, float x, float y) {
    if (bind(texture)) {
        draw2D(x, y, 0.0f, (float)texture->getWidth(), (float)texture->getHeight());
    }
}

void RenderView::draw2D(const TexturePtr &texture, float x, float y, float width, float height) {
    if (bind(texture)) {
        draw2D(x, y, 0.0f, width, height);
    }
}

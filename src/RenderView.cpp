#include "RenderView.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

#include "App.h"
#include "Layer.h"
#include "Shared.h"

namespace {

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

const char *const kFragmentSingle = R"(
uniform sampler2D uTex0;
uniform vec4 uColor;
varying vec2 vTexCoord0;
void main() {
    gl_FragColor = texture2D(uTex0, vTexCoord0) * uColor;
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
    gl_FragColor = mix(previous, current, uRatio);
}
)";

std::string shaderPrelude(bool fragment) {
    if (GLES2_IsRealES()) {
        return fragment ? "#version 100\nprecision mediump float;\n" : "#version 100\n";
    }
    return "#version 120\n";
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

// ---------------------------------------------------------------------------
// Programs
// ---------------------------------------------------------------------------

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
    GLuint fragment = compile(GL_FRAGMENT_SHADER, shaderPrelude(true) + fragmentSource);
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
    if (mMixing) {
        glUniform1f(program.uRatio, mMixRatio);
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool RenderView::init(SDL_Window *window) {
    mWindow = window;
    if (!buildPrograms()) {
        return false;
    }
    glGenBuffers(1, &mQuad2DVBO);

    // Mipmaps alone cost too much sharpness: a thumbnail drawn near its own
    // size still blends level 0 with level 1, and the fine detail in a photo
    // goes with it. Anisotropic filtering is what keeps both ends: sharp head
    // on, filtered where the wall tilts away and minifies. Without it the
    // trade is not worth making, so the mip chain is only built when it is
    // there to pair with.
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    if (extensions != nullptr && SDL_strstr(extensions, "GL_EXT_texture_filter_anisotropic") != nullptr) {
        GLfloat maxAnisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
        // Four is where the returns flatten out for this, and it is cheap.
        mMaxAnisotropy = (maxAnisotropy < 4.0f) ? maxAnisotropy : 4.0f;
    }
    SDL_Log("Anisotropic filtering %s (max %.1f)", (mMaxAnisotropy > 1.0f) ? "on" : "unavailable",
            mMaxAnisotropy);

    mLoadThreadsRunning.store(true);
    for (int i = 0; i < NUM_TEXTURE_LOAD_THREADS; ++i) {
        mLoadThreads.emplace_back([this, i]() { textureLoadThread(i); });
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
    // Screen space projection for draw2D, with the origin at the top left just
    // as the Android view coordinates had it. The near and far planes are set
    // so the z argument lands in the depth buffer unchanged, which is what
    // glDrawTexOES did and what BackgroundLayer relies on to sit at the back.
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

    // The interval comes from the nanosecond clock, not SDL_GetTicks. A
    // millisecond counter reports 0 for any frame shorter than that, which
    // happens whenever the swap does not block - an unmapped window, or vsync
    // off - and a zero step makes every animation jump straight to its target.
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

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void RenderView::queueTouchEvent(const MotionEvent &event) {
    if (mTouchEventQueue.size() > 8 && event.action == MotionEvent::ACTION_MOVE) {
        return;
    }
    mTouchEventQueue.push_back(event);
    requestRender();
}

void RenderView::processTouchEvents() {
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

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

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

void RenderView::loadTextureAsync(const TexturePtr &texture) {
    Bitmap bitmap = texture->load(this);
    if (bitmap.valid()) {
        int width = bitmap.width();
        int height = bitmap.height();
        texture->mWidth = width;
        texture->mHeight = height;
        // Pad to a power of two, so the normalized extents the meshes use stay
        // meaningful and wrap modes behave everywhere.
        if (!Shared::isPowerOf2(width) || !Shared::isPowerOf2(height)) {
            int paddedWidth = Shared::nextPowerOf2(width);
            int paddedHeight = Shared::nextPowerOf2(height);
            bitmap = bitmap.paddedTo(paddedWidth, paddedHeight, texture->wantsMipmaps());
            texture->mNormalizedWidth = (float)width / (float)paddedWidth;
            texture->mNormalizedHeight = (float)height / (float)paddedHeight;
        } else {
            texture->mNormalizedWidth = 1.0f;
            texture->mNormalizedHeight = 1.0f;
        }
    }
    texture->mBitmap = std::move(bitmap);
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // ES 2.0 only accepts a mip chain on a power of two texture, and every
    // texture that asks for one is padded to a power of two anyway.
    bool mipmapped = texture->wantsMipmaps() && mMaxAnisotropy > 1.0f &&
                     Shared::isPowerOf2(texture->mBitmap.width()) &&
                     Shared::isPowerOf2(texture->mBitmap.height());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->mBitmap.width(), texture->mBitmap.height(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, texture->mBitmap.pixels());
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
        texture->mBytes = (size_t)width * (size_t)height * 4;
        if (mipmapped) {
            texture->mBytes += texture->mBytes / 3;
        }
        texture->mLastUsedFrame = mFrameCounter;
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
    // A few hundred megabytes of thumbnails is comfortable; past that the
    // least recently bound ones go back to disk and reload if needed.
    const size_t kBudgetBytes = 192u * 1024u * 1024u;
    // Never drop something drawn in the last few frames, or scrolling would
    // evict and reload the same thumbnails every frame.
    const uint64_t kKeepFrames = 120;

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
        return a->mLastUsedFrame < b->mLastUsedFrame;
    });
    const size_t targetBytes = kBudgetBytes - kBudgetBytes / 10;
    for (const TexturePtr &texture : alive) {
        if (mTextureBytes <= targetBytes) {
            break;
        }
        if (texture->mLastUsedFrame + kKeepFrames > mFrameCounter) {
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
        }

        if (index != 0) {
            mThreadIsLoading[index].store(true);
        }
        loadTextureAsync(texture);
        mThreadIsLoading[index].store(false);

        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            mLoadOutputQueue.push_back(texture);
        }
        requestRender();
    }
}

// ---------------------------------------------------------------------------
// Binding and colour
// ---------------------------------------------------------------------------

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
        texture->mLastUsedFrame = mFrameCounter;
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

// ---------------------------------------------------------------------------
// Matrix stack
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Raw state
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// 2D drawing, replacing OES_draw_texture
// ---------------------------------------------------------------------------

void RenderView::draw2D(float x, float y, float z, float width, float height) {
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

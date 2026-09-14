// Port of com.cooliris.media.RenderView on ES 2.0. MatrixStack, colour/mix shaders,
// vertex attributes and orthographic quads replace the ES 1.1 fixed-function pipeline.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/Input.h"
#include "core/MatrixStack.h"
#include "graphics/Texture.h"
#include "graphics/gles2.h"

class Layer;
class RootLayer;

struct SDL_Window;

class RenderLists {
  public:
    std::vector<Layer *> updateList;
    std::vector<Layer *> opaqueList;
    std::vector<Layer *> blendedList;
    std::vector<Layer *> hitTestList;
    std::vector<Layer *> systemList;

    void clear() {
        updateList.clear();
        opaqueList.clear();
        blendedList.clear();
        hitTestList.clear();
        systemList.clear();
    }
};

class RenderView {
  public:
    static const int NUM_TEXTURE_LOAD_THREADS = 4;
    // Maximum concurrent network workers; the pool grows on demand.
    static const int MAX_NETWORK_LOAD_THREADS = 6;
    // Idle timeout before a network worker retires.
    static const int NETWORK_THREAD_IDLE_SECONDS = 45;
    static const int MAX_LOADING_COUNT = 8;

    RenderView();
    ~RenderView();

    // ---- lifecycle -------------------------------------------------------
    bool init(SDL_Window *window);
    void shutdown();
    void setRootLayer(RootLayer *layer);
    void onSurfaceCreated();
    void onSurfaceChanged(int width, int height);
    void onDrawFrame();

    int getWidth() const {
        return mViewWidth;
    }

    int getHeight() const {
        return mViewHeight;
    }

    // ---- render loop bookkeeping ----------------------------------------
    uint64_t getFrameTime() const {
        return mFrameTime;
    }

    float getFrameInterval() const {
        return mFrameInterval;
    }

    void requestRender() {
        mRenderRequested = true;
    }

    bool consumeRenderRequest() {
        bool requested = mRenderRequested;
        mRenderRequested = false;
        return requested;
    }

    RenderLists &getLists() {
        return mLists;
    }

    void markListsDirty() {
        mListsDirty = true;
    }

    // ---- input -----------------------------------------------------------
    void queueTouchEvent(const MotionEvent &event);
    // Coalesced to the last position, since only where the pointer ended up
    // matters and a mouse produces these faster than frames.
    void queuePointerMove(float x, float y);
    // Coalesced to the latest reading, and applied on the render thread. The
    // tilt ends up in the camera, which the render thread owns.
    void queueAccelerometer(float x, float y, float z);
    bool dispatchKeyDown(int keyCode, const KeyEvent &event);

    // ---- textures --------------------------------------------------------
    TexturePtr getResource(const std::string &name, bool scaled = true);
    void clearCache();
    void prime(const TexturePtr &texture, bool highPriority);
    void loadTexture(const TexturePtr &texture);
    bool bind(const TexturePtr &texture);
    bool bindMixed(const TexturePtr &from, const TexturePtr &to, float ratio);
    void unbindMixed();
    void processAllTextures();
    void queueDeleteTexture(GLuint textureId);
    bool isLoadingExpensiveTextures() const;
    int64_t elapsedLoadingExpensiveTextures() const;
    void handleLowMemory();

    // ---- colour ----------------------------------------------------------
    void setAlpha(float alpha);
    float getAlpha() const {
        return mAlpha;
    }
    void setColor(float red, float green, float blue, float alpha);
    void resetColor();

    // ---- matrix stack, standing in for the fixed function one ------------
    void glLoadIdentity();
    void glTranslatef(float x, float y, float z);
    void glRotatef(float angle, float x, float y, float z);
    void glScalef(float x, float y, float z);
    void glPushMatrix();
    void glPopMatrix();
    void gluLookAt(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ, float upX,
                   float upY, float upZ);
    void setFov(float fov);

    // ---- raw state the ported draw code toggles --------------------------
    void enableBlend(bool enable);
    void blendFunc(GLenum src, GLenum dst);
    void depthFunc(GLenum func);

    // ---- geometry, standing in for the client array calls ----------------
    void bindVertexBuffer(GLuint vbo);
    void bindTexCoordBuffer(int unit, GLuint vbo);
    void bindIndexBuffer(GLuint ibo);
    void unbindBuffers();
    void drawElements(GLenum mode, GLsizei count, size_t byteOffset);

    // ---- 2D, standing in for glDrawTexfOES -------------------------------
    void draw2D(const TexturePtr &texture, float x, float y);
    void draw2D(const TexturePtr &texture, float x, float y, float width, float height);
    void draw2D(float x, float y, float z, float width, float height);

  private:
    struct Program {
        GLuint id = 0;
        GLint uMVP = -1;
        GLint uColor = -1;
        GLint uTex0 = -1;
        GLint uTex1 = -1;
        GLint uRatio = -1;
    };

    bool buildPrograms();
    Program buildProgram(const char *vertexSource, const char *fragmentSource);
    void useProgram(const Program &program);
    void applyUniforms();
    // Loads on the calling thread and shapes the pixels, without queueing. The
    // chrome path uses this: it is already on the render thread and uploads
    // straight away.
    void loadTextureAsync(const TexturePtr &texture);
    void applyBitmap(const TexturePtr &texture, Bitmap bitmap);

  public:
    // Hands a finished load back from whichever thread produced it. Safe to
    // call from a decode thread, a network thread, or a browser callback.
    void finishLoad(const TexturePtr &texture, Bitmap bitmap);

  private:
    void uploadTexture(const TexturePtr &texture);
    void processTextures(bool processAll);
    // Drops the least recently bound textures once the total passes the
    // budget. Called once a frame.
    void enforceTextureBudget();
    void queueLoad(const TexturePtr &texture, bool highPriority);
    void processTouchEvents();
    void updateLists();
    Layer *hitTest(float x, float y);
    void textureLoadThread(int index);
    // One elastic worker. Returns, and so retires, when it has had nothing to
    // do for NETWORK_THREAD_IDLE_SECONDS.
    void networkLoadThread();
    // Starts a thread if every one of them is busy and there is room. Call with
    // mNetworkMutex held.
    void growNetworkPoolLocked();
    // Joins whatever has retired since last time. Call with mNetworkMutex held.
    void reapNetworkThreadsLocked();

    SDL_Window *mWindow = nullptr;
    int mViewWidth = 0;
    int mViewHeight = 0;

    RootLayer *mRootLayer = nullptr;
    bool mListsDirty = false;
    RenderLists mLists;
    Layer *mTouchEventTarget = nullptr;

    Mat4 mProjectionMatrix = Mat4::identity();
    MatrixStack mModelView;
    Mat4 mPerspective = Mat4::identity();
    Mat4 mOrtho = Mat4::identity();
    float mFov = 45.0f;

    Program mSingleProgram;
    Program mMixProgram;
    const Program *mCurrentProgram = nullptr;
    bool mMixing = false;
    float mMixRatio = 0.0f;

    float mColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float mAlpha = 1.0f;

    GLuint mVertexVBO = 0;
    GLuint mTexCoordVBO[2] = {0, 0};
    GLuint mIndexVBO = 0;
    GLuint mQuad2DVBO = 0;

    Texture *mBoundTexture = nullptr;
    // The texture on unit 1 while mixing, for the second set of coordinates.
    Texture *mBoundTextureMixed = nullptr;

    uint64_t mFrameTime = 0;
    // Separate from mFrameTime because that one has to stay in milliseconds
    // for FloatAnim, while the interval needs better resolution than that.
    uint64_t mFrameTimeNs = 0;
    float mFrameInterval = 0.0f;
    bool mRenderRequested = true;

    int mLoadingCount = 0;
    int64_t mLoadingExpensiveTexturesStartTime = 0;

    // Every uploaded texture, weakly held so this list never keeps one alive.
    std::vector<std::weak_ptr<Texture>> mLiveTextures;
    size_t mTextureBytes = 0;
    uint64_t mFrameCounter = 0;
    // 1 means the extension is missing, and then no mip chains are built.
    float mMaxAnisotropy = 1.0f;
    // The internal format a BGRA bitmap uploads with, or 0 where the context
    // takes no BGRA and the bitmap is swapped to RGBA first.
    GLint mBgraInternalFormat = 0;

    std::map<std::string, TexturePtr> mCacheScaled;
    std::map<std::string, TexturePtr> mCacheUnscaled;

    // Three input queues, matching the original split between cheap cached
    // textures, video thumbnails and full decodes.
    std::deque<TexturePtr> mLoadInputQueue;
    std::deque<TexturePtr> mLoadInputQueueCached;
    std::deque<TexturePtr> mLoadInputQueueVideo;
    std::deque<TexturePtr> mLoadOutputQueue;
    std::mutex mQueueMutex;
    std::condition_variable mQueueCondition;
    std::vector<std::thread> mLoadThreads;
    std::atomic<bool> mLoadThreadsRunning{false};
    std::atomic<bool> mThreadIsLoading[NUM_TEXTURE_LOAD_THREADS];

    // On-demand network pool, separate from CPU decoding so stalled downloads cannot block it.
    std::deque<TexturePtr> mNetworkQueue;
    std::mutex mNetworkMutex;
    std::condition_variable mNetworkCondition;
    // Threads that have retired and are waiting to be joined. A thread cannot
    // join itself, so it leaves its handle here for the next one through.
    std::vector<std::thread> mNetworkThreads;
    std::vector<std::thread::id> mNetworkFinished;
    int mNetworkThreadCount = 0;
    int mNetworkIdleCount = 0;

    std::deque<MotionEvent> mTouchEventQueue;
    // The pointer's last position, and whether it has moved since the render
    // thread last looked.
    float mPointerX = 0.0f;
    float mPointerY = 0.0f;
    bool mPointerMoved = false;
    float mAccelX = 0.0f;
    float mAccelY = 0.0f;
    float mAccelZ = 0.0f;
    bool mAccelChanged = false;
    std::vector<GLuint> mPendingTextureDeletes;
    std::mutex mDeleteMutex;
};

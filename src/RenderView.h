// Port of com.cooliris.media.RenderView.
//
// The original extended GLSurfaceView and drove the ES 1.1 fixed function
// pipeline. This class keeps the same public shape - layer lists, texture
// queues, bind/draw2D/setAlpha - but implements it on ES 2.0:
//
//   glMatrixMode / glTranslatef / gluLookAt -> MatrixStack members here
//   glTexEnv REPLACE and MODULATE           -> the uColor uniform
//   glTexEnv COMBINE / INTERPOLATE          -> the two texture mix program
//   glDrawTexfOES (OES_draw_texture)        -> an ortho projected quad
//   glVertexPointer / glTexCoordPointer     -> vertex attributes 0, 1 and 2
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

#include "Input.h"
#include "MatrixStack.h"
#include "Texture.h"
#include "gles2.h"

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
    void loadTextureAsync(const TexturePtr &texture);
    void uploadTexture(const TexturePtr &texture);
    void processTextures(bool processAll);
    void queueLoad(const TexturePtr &texture, bool highPriority);
    void processTouchEvents();
    void updateLists();
    Layer *hitTest(float x, float y);
    void textureLoadThread(int index);

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

    std::deque<MotionEvent> mTouchEventQueue;
    std::vector<GLuint> mPendingTextureDeletes;
    std::mutex mDeleteMutex;
};

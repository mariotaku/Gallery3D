// Port of com.cooliris.media.GridQuad: 2x2 buffered triangle strip.
// Oriented quads precompute texture coordinates for 360 whole-degree rotations.
#pragma once

#include <vector>

#include "MatrixStack.h"
#include "gles2.h"

class RenderView;

class GridQuad {
  public:
    static const int INDEX_COUNT = 4;
    static const int ORIENTATION_COUNT = 360;

    explicit GridQuad(bool generateOrientedQuads);

    static GridQuad *createGridQuad(float width, float height, float xOffset, float yOffset, float uExtents,
                                    float vExtents, bool generateOrientedQuads);

    void setDynamic(bool dynamic);

    float getWidth() const {
        return mWidth;
    }

    float getHeight() const {
        return mHeight;
    }

    // Maps a texture rectangle to a quad rectangle for fullscreen tiles.
    // Local +x points screen-left and +y screen-up.
    void setCorners(float xMin, float yMin, float xMax, float yMax, float uAtXMin, float vAtYMin, float uAtXMax,
                    float vAtYMax);

    void update(float timeElapsed);
    void commit();
    void recomputeQuad();
    // Fits the image inside a box of this aspect that is fitHeight tall, where
    // one is the whole viewport. Less than one keeps a picture inside the safe
    // area rather than under the system bars.
    void resizeQuad(float viewAspect, float u, float v, float imageWidth, float imageHeight, float fitHeight);

    // Moves the quad off the middle of the screen. The safe area is rarely
    // centred on it: a cutout takes more from the top than the gesture bar
    // takes from the bottom.
    void setCenterOffset(float x, float y);

    void bindArrays(RenderView *view);
    void unbindArrays(RenderView *view);
    static void draw(RenderView *view, float orientationDegrees);

    bool usingHardwareBuffers() const {
        return mVertBufferIndex != 0;
    }

    void forgetHardwareBuffers();
    void freeHardwareBuffers();
    void generateHardwareBuffers();

  private:
    void set(int i, int j, float x, float y, float z, float u, float v);
    void set(int i, int j, float x, float y, float z, float u, float v, bool modifyOverlay, int orientationId);

    std::vector<float> mVertexBuffer;
    std::vector<float> mOverlayTexCoordBuffer;
    std::vector<float> mBaseTexCoordBuffer;
    std::vector<unsigned short> mIndexBuffer;

    int mW = 2;
    int mH = 2;
    GLuint mVertBufferIndex = 0;
    GLuint mIndexBufferIndex = 0;
    GLuint mOverlayTextureCoordBufferIndex = 0;
    GLuint mBaseTextureCoordBufferIndex = 0;
    bool mDynamicVBO = false;
    float mOffsetX = 0.0f;
    float mOffsetY = 0.0f;
    float mU = 0.0f;
    float mV = 0.0f;
    float mAnimU = 0.0f;
    float mAnimV = 0.0f;
    float mWidth = 0.0f;
    float mHeight = 0.0f;
    float mAnimWidth = 0.0f;
    float mAnimHeight = 0.0f;
    bool mQuadChanged = false;
    float mDefaultAspectRatio = 1.0f;
    const bool mOrientedQuad;
    MatrixStack *mMatrix = nullptr;
    float mCoordsIn[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float mCoordsOut[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

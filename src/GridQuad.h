// Port of com.cooliris.media.GridQuad.
//
// A 2x2 textured mesh drawn as a triangle strip out of hardware buffers. When
// asked for oriented quads it bakes 360 copies of the texture coordinates, one
// per whole degree of image rotation, and picks one with the index offset at
// draw time - the same trick the original used to rotate thumbnails for free.
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

    void update(float timeElapsed);
    void commit();
    void recomputeQuad();
    void resizeQuad(float viewAspect, float u, float v, float imageWidth, float imageHeight);

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

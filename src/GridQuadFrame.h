// Port of com.cooliris.media.GridQuadFrame.
//
// A 4x4 grid of vertices drawn as one triangle strip, forming the nine patch
// border around a grid item. 25 indices, degenerate triangles included.
#pragma once

#include <vector>

#include "gles2.h"

class RenderView;

class GridQuadFrame {
  public:
    static const int INDEX_COUNT = 25;

    GridQuadFrame();

    static GridQuadFrame *createFrame(float width, float height, int itemWidth, int itemHeight);

    void bindArrays(RenderView *view);
    void unbindArrays(RenderView *view);
    static void draw(RenderView *view);

    bool usingHardwareBuffers() const {
        return mVertBufferIndex != 0;
    }

    void forgetHardwareBuffers();
    void freeHardwareBuffers();
    void generateHardwareBuffers();

  private:
    void set(int i, int j, float x, float y, float z, float u, float v);

    std::vector<float> mVertexBuffer;
    std::vector<float> mTexCoordBuffer;
    std::vector<float> mSecTexCoordBuffer;
    std::vector<unsigned short> mIndexBuffer;

    int mW = 4;
    int mH = 4;
    GLuint mVertBufferIndex = 0;
    GLuint mIndexBufferIndex = 0;
    GLuint mTextureCoordBufferIndex = 0;
    GLuint mSecTextureCoordBufferIndex = 0;
};

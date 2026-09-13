#include "grid/GridQuadFrame.h"

#include "graphics/RenderView.h"

GridQuadFrame::GridQuadFrame() {
    int size = mW * mH;
    mVertexBuffer.assign((size_t)(size * 3), 0.0f);
    mTexCoordBuffer.assign((size_t)(size * 2), 0.0f);
    mSecTexCoordBuffer.assign((size_t)(size * 2), 0.0f);

    /*
     * Triangle strip over a 4x4 vertex grid.
     *
     * [0]---[1]---------[2]---[3]
     * [4]---[5]---------[6]---[7]
     * [8]---[9]--------[10]--[11]
     * [12]-[13]--------[14]--[15]
     */
    static const unsigned short kIndices[INDEX_COUNT] = {0,  4,  1,  5,  2,  6,  3,  7,  11, 6, 10, 14, 11,
                                                         15, 15, 14, 14, 10, 13, 9,  12, 8,  4,  9, 5};
    mIndexBuffer.assign(kIndices, kIndices + INDEX_COUNT);
    mVertBufferIndex = 0;
}

GridQuadFrame *GridQuadFrame::createFrame(float width, float height, int itemWidth, int itemHeight) {
    (void)itemWidth;
    GridQuadFrame *frame = new GridQuadFrame();
    const float textureSize = 64.0f;
    const float numPixelsYOriginShift = 7.0f;
    const float inset = 6.0f;
    const float ratio = 1.0f / (float)itemHeight;
    const float frameXThickness = 0.5f * textureSize * ratio;
    const float frameYThickness = 0.5f * textureSize * ratio;
    const float frameX = width * 0.5f + frameXThickness * 0.5f - inset * ratio;
    float frameY = height * 0.5f + frameYThickness * 0.5f + (inset - 1.0f) * ratio;
    const float originX = 0.0f;
    const float originY = numPixelsYOriginShift * ratio;

    frame->set(0, 0, -frameX + originX, -frameY + originY, 0, 1.0f, 1.0f);
    frame->set(1, 0, -frameX + originX + frameXThickness, -frameY + originY, 0, 0.5f, 1.0f);
    frame->set(2, 0, frameX - frameXThickness + originX, -frameY + originY, 0, 0.5f, 1.0f);
    frame->set(3, 0, frameX + originX, -frameY + originY, 0, 0.0f, 1.0f);

    frameY -= frameYThickness;

    frame->set(0, 1, -frameX + originX, -frameY + originY, 0, 1.0f, 0.5f);
    frame->set(1, 1, -frameX + frameXThickness + originX, -frameY + originY, 0, 0.5f, 0.5f);
    frame->set(2, 1, frameX - frameXThickness + originX, -frameY + originY, 0, 0.5f, 0.5f);
    frame->set(3, 1, frameX + originX, -frameY + originY, 0, 0.0f, 0.5f);

    frameY = height * 0.5f - frameYThickness;

    frame->set(0, 2, -frameX + originX, frameY + originY, 0, 1.0f, 0.5f);
    frame->set(1, 2, -frameX + frameXThickness + originX, frameY + originY, 0, 0.5f, 0.5f);
    frame->set(2, 2, frameX - frameXThickness + originX, frameY + originY, 0, 0.5f, 0.5f);
    frame->set(3, 2, frameX + originX, frameY + originY, 0, 0.0f, 0.5f);

    frameY += frameYThickness;

    frame->set(0, 3, -frameX + originX, frameY + originY, 0, 1.0f, 0.0f);
    frame->set(1, 3, -frameX + frameXThickness + originX, frameY + originY, 0, 0.5f, 0.0f);
    frame->set(2, 3, frameX - frameXThickness + originX, frameY + originY, 0, 0.5f, 0.0f);
    frame->set(3, 3, frameX + originX, frameY + originY, 0, 0.0f, 0.0f);

    return frame;
}

void GridQuadFrame::set(int i, int j, float x, float y, float z, float u, float v) {
    if (i < 0 || i >= mW || j < 0 || j >= mH) {
        return;
    }
    int index = mW * j + i;
    int posIndex = index * 3;
    mVertexBuffer[(size_t)posIndex] = x;
    mVertexBuffer[(size_t)posIndex + 1] = y;
    mVertexBuffer[(size_t)posIndex + 2] = z;

    int texIndex = index * 2;
    mTexCoordBuffer[(size_t)texIndex] = u;
    mTexCoordBuffer[(size_t)texIndex + 1] = v;
    mSecTexCoordBuffer[(size_t)texIndex] = u;
    mSecTexCoordBuffer[(size_t)texIndex + 1] = v;
}

void GridQuadFrame::bindArrays(RenderView *view) {
    view->bindVertexBuffer(mVertBufferIndex);
    view->bindTexCoordBuffer(0, mTextureCoordBufferIndex);
    view->bindTexCoordBuffer(1, mSecTextureCoordBufferIndex);
    view->bindIndexBuffer(mIndexBufferIndex);
}

void GridQuadFrame::draw(RenderView *view) {
    // Only valid after bindArrays.
    view->drawElements(GL_TRIANGLE_STRIP, INDEX_COUNT, 0);
}

void GridQuadFrame::unbindArrays(RenderView *view) {
    view->unbindBuffers();
}

void GridQuadFrame::forgetHardwareBuffers() {
    mVertBufferIndex = 0;
    mIndexBufferIndex = 0;
    mTextureCoordBufferIndex = 0;
    mSecTextureCoordBufferIndex = 0;
}

void GridQuadFrame::freeHardwareBuffers() {
    if (mVertBufferIndex != 0) {
        GLuint buffers[4] = {mVertBufferIndex, mTextureCoordBufferIndex, mSecTextureCoordBufferIndex,
                             mIndexBufferIndex};
        glDeleteBuffers(4, buffers);
        forgetHardwareBuffers();
    }
}

void GridQuadFrame::generateHardwareBuffers() {
    if (mVertBufferIndex != 0) {
        return;
    }
    glGenBuffers(1, &mVertBufferIndex);
    glBindBuffer(GL_ARRAY_BUFFER, mVertBufferIndex);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mVertexBuffer.size() * sizeof(float)), mVertexBuffer.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &mTextureCoordBufferIndex);
    glBindBuffer(GL_ARRAY_BUFFER, mTextureCoordBufferIndex);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mTexCoordBuffer.size() * sizeof(float)), mTexCoordBuffer.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &mSecTextureCoordBufferIndex);
    glBindBuffer(GL_ARRAY_BUFFER, mSecTextureCoordBufferIndex);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mSecTexCoordBuffer.size() * sizeof(float)), mSecTexCoordBuffer.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenBuffers(1, &mIndexBufferIndex);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIndexBufferIndex);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(mIndexBuffer.size() * sizeof(unsigned short)),
                 mIndexBuffer.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

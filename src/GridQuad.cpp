#include "GridQuad.h"

#include <cmath>

#include "FloatUtils.h"
#include "RenderView.h"
#include "Shared.h"

static const float kPi = 3.14159265358979323846f;

GridQuad::GridQuad(bool generateOrientedQuads) : mOrientedQuad(generateOrientedQuads) {
    if (mOrientedQuad) {
        mMatrix = new MatrixStack();
        mMatrix->glLoadIdentity();
    }
    int vertsAcross = 2;
    int vertsDown = 2;
    mW = vertsAcross;
    mH = vertsDown;
    int size = vertsAcross * vertsDown;
    int orientationCount = generateOrientedQuads ? ORIENTATION_COUNT : 1;

    mVertexBuffer.assign((size_t)(size * 3 * orientationCount), 0.0f);
    mOverlayTexCoordBuffer.assign((size_t)(size * 2 * orientationCount), 0.0f);
    mBaseTexCoordBuffer.assign((size_t)(size * 2 * orientationCount), 0.0f);

    mIndexBuffer.resize((size_t)(INDEX_COUNT * orientationCount));
    for (size_t i = 0; i < mIndexBuffer.size(); ++i) {
        mIndexBuffer[i] = (unsigned short)i;
    }
    mVertBufferIndex = 0;
}

GridQuad *GridQuad::createGridQuad(float width, float height, float xOffset, float yOffset, float uExtents,
                                   float vExtents, bool generateOrientedQuads) {
    GridQuad *grid = new GridQuad(generateOrientedQuads);
    grid->mWidth = width;
    grid->mHeight = height;
    grid->mAnimWidth = width;
    grid->mAnimHeight = height;
    grid->mDefaultAspectRatio = width / height;
    float widthBy2 = width * 0.5f;
    float heightBy2 = height * 0.5f;
    const float v = vExtents;
    const float u = uExtents;
    if (!generateOrientedQuads) {
        grid->set(0, 0, -widthBy2 + xOffset, -heightBy2 + yOffset, 0.0f, u, v);
        grid->set(1, 0, widthBy2 + xOffset, -heightBy2 + yOffset, 0.0f, 0.0f, v);
        grid->set(0, 1, -widthBy2 + xOffset, heightBy2 + yOffset, 0.0f, u, 0.0f);
        grid->set(1, 1, widthBy2 + xOffset, heightBy2 + yOffset, 0.0f, 0.0f, 0.0f);
    } else {
        for (int i = 0; i < ORIENTATION_COUNT; ++i) {
            grid->set(0, 0, -widthBy2 + xOffset, -heightBy2 + yOffset, 0.0f, u, v, true, i);
            grid->set(1, 0, widthBy2 + xOffset, -heightBy2 + yOffset, 0.0f, 0.0f, v, true, i);
            grid->set(0, 1, -widthBy2 + xOffset, heightBy2 + yOffset, 0.0f, u, 0.0f, true, i);
            grid->set(1, 1, widthBy2 + xOffset, heightBy2 + yOffset, 0.0f, 0.0f, 0.0f, true, i);
        }
    }
    grid->mU = uExtents;

    grid->mV = vExtents;
    return grid;
}

void GridQuad::setDynamic(bool dynamic) {
    mDynamicVBO = dynamic;
}

void GridQuad::update(float timeElapsed) {
    mAnimWidth = FloatUtils::animate(mAnimWidth, mWidth, timeElapsed);
    mAnimHeight = FloatUtils::animate(mAnimHeight, mHeight, timeElapsed);
    // recomputeQuad reads mU and mV directly, so extents snap despite these animated values.
    mAnimU = FloatUtils::animate(mAnimU, mU, timeElapsed);
    mAnimV = FloatUtils::animate(mAnimV, mV, timeElapsed);
    recomputeQuad();
}

void GridQuad::commit() {
    mAnimWidth = mWidth;
    mAnimHeight = mHeight;
    mAnimU = mU;
    mAnimV = mV;
}

void GridQuad::setCenterOffset(float x, float y) {
    mOffsetX = x;
    mOffsetY = y;
}

void GridQuad::recomputeQuad() {
    float widthBy2 = mAnimWidth * 0.5f;
    float heightBy2 = mAnimHeight * 0.5f;
    float xOffset = mOffsetX;
    float yOffset = mOffsetY;
    float u = mU;
    float v = mV;
    // Update both coordinate sets: the single-texture shader uses overlay attribute 1.
    // Fullscreen quads have no overlay art and use identical coordinates for both.
    set(0, 0, -widthBy2 + xOffset, -heightBy2 + yOffset, 0.0f, u, v, true, 0);
    set(1, 0, widthBy2 + xOffset, -heightBy2 + yOffset, 0.0f, 0.0f, v, true, 0);
    set(0, 1, -widthBy2 + xOffset, heightBy2 + yOffset, 0.0f, u, 0.0f, true, 0);
    set(1, 1, widthBy2 + xOffset, heightBy2 + yOffset, 0.0f, 0.0f, 0.0f, true, 0);
    mQuadChanged = true;
}

void GridQuad::setCorners(float xMin, float yMin, float xMax, float yMax, float uAtXMin, float vAtYMin,
                          float uAtXMax, float vAtYMax) {
    set(0, 0, xMin, yMin, 0.0f, uAtXMin, vAtYMin, true, 0);
    set(1, 0, xMax, yMin, 0.0f, uAtXMax, vAtYMin, true, 0);
    set(0, 1, xMin, yMax, 0.0f, uAtXMin, vAtYMax, true, 0);
    set(1, 1, xMax, yMax, 0.0f, uAtXMax, vAtYMax, true, 0);
    mQuadChanged = true;
}

void GridQuad::resizeQuad(float viewAspect, float u, float v, float imageWidth, float imageHeight,
                          float fitHeight, bool ease) {
    // Given u and v we know the aspect ratio of the image, so one axis has to
    // move depending on the image and viewport aspect ratios.
    mU = u;
    mV = v;
    float imageAspect = imageWidth / imageHeight;
    float width = mDefaultAspectRatio;
    float height = 1.0f;
    if (viewAspect < 1.0f) {
        height = height * (mDefaultAspectRatio / imageAspect);
        float maxHeight = width / viewAspect;
        if (height > maxHeight) {
            float ratio = height / maxHeight;
            height /= ratio;
            width /= ratio;
        }
    } else {
        width = width * (imageAspect / mDefaultAspectRatio);
        float maxWidth = height * viewAspect;
        if (width > maxWidth) {
            float ratio = width / maxWidth;
            width /= ratio;
            height /= ratio;
        }
    }
    // Shrink the whole thing to the box asked for, after the fit: the aspect
    // clamp above works the same at any size.
    mWidth = width * fitHeight;
    mHeight = height * fitHeight;
    if (ease) {
        // Leave the drawn size where it is and let update() walk it over. The
        // texture extents still snap: they belong to whichever image is being
        // drawn this frame, not to the shape it is drawn at.
        mAnimU = mU;
        mAnimV = mV;
    } else {
        commit();
    }
    recomputeQuad();
}

void GridQuad::set(int i, int j, float x, float y, float z, float u, float v) {
    set(i, j, x, y, z, u, v, true, 0);
}

void GridQuad::set(int i, int j, float x, float y, float z, float u, float v, bool modifyOverlay, int orientationId) {
    if (i < 0 || i >= mW || j < 0 || j >= mH) {
        return;
    }
    int index = orientationId * INDEX_COUNT + mW * j + i;
    int posIndex = index * 3;
    mVertexBuffer[(size_t)posIndex] = x;
    mVertexBuffer[(size_t)posIndex + 1] = y;
    mVertexBuffer[(size_t)posIndex + 2] = z;

    int baseTexIndex = index * 2;
    MatrixStack *matrix = mMatrix;
    if (matrix != nullptr) {
        // Rotates the texture coordinates about the quad centre and shrinks
        // them just enough that a rotated thumbnail still fills the frame.
        int orientation = orientationId * 2;
        matrix->glLoadIdentity();
        matrix->glTranslatef(0.5f, 0.5f, 0.0f);
        float itheta = (float)orientation * (kPi / 180.0f);
        float sini = std::sin(itheta);
        float scale = 1.0f + (sini * sini) * 0.33333333f;
        scale = 1.0f / scale;
        matrix->glRotatef(-(float)orientation, 0.0f, 0.0f, 1.0f);
        matrix->glScalef(scale, scale, 1.0f);
        matrix->glTranslatef(-0.5f + (sini * 0.125f / scale),
                             -0.5f + std::fabs(std::sin(itheta * 0.5f) * 0.25f), 0.0f);
        mCoordsIn[0] = u;
        mCoordsIn[1] = v;
        mCoordsIn[2] = 0.0f;
        mCoordsIn[3] = 1.0f;
        matrix->apply(mCoordsIn, mCoordsOut);
        u = mCoordsOut[0] / mCoordsOut[3];
        v = mCoordsOut[1] / mCoordsOut[3];
    }
    mBaseTexCoordBuffer[(size_t)baseTexIndex] = u;
    mBaseTexCoordBuffer[(size_t)baseTexIndex + 1] = v;
    if (modifyOverlay) {
        mOverlayTexCoordBuffer[(size_t)baseTexIndex] = u;
        mOverlayTexCoordBuffer[(size_t)baseTexIndex + 1] = v;
    }
}

void GridQuad::bindArrays(RenderView *view) {
    view->bindVertexBuffer(mVertBufferIndex);
    view->bindTexCoordBuffer(0, mOverlayTextureCoordBufferIndex);
    view->bindTexCoordBuffer(1, mBaseTextureCoordBufferIndex);
    view->bindIndexBuffer(mIndexBufferIndex);

    if (mDynamicVBO && mQuadChanged) {
        mQuadChanged = false;
        glBindBuffer(GL_ARRAY_BUFFER, mOverlayTextureCoordBufferIndex);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mOverlayTexCoordBuffer.size() * sizeof(float)),
                     mOverlayTexCoordBuffer.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, mBaseTextureCoordBufferIndex);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mBaseTexCoordBuffer.size() * sizeof(float)),
                     mBaseTexCoordBuffer.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, mVertBufferIndex);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mVertexBuffer.size() * sizeof(float)), mVertexBuffer.data(),
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

void GridQuad::draw(RenderView *view, float orientationDegrees) {
    // Only valid after bindArrays.
    int orientation = (int)Shared::normalizePositive(orientationDegrees);
    if (orientation < 0 || orientation >= ORIENTATION_COUNT) {
        orientation = 0;
    }
    view->drawElements(GL_TRIANGLE_STRIP, INDEX_COUNT, (size_t)orientation * INDEX_COUNT * sizeof(unsigned short));
}

void GridQuad::unbindArrays(RenderView *view) {
    view->unbindBuffers();
}

void GridQuad::forgetHardwareBuffers() {
    mVertBufferIndex = 0;
    mIndexBufferIndex = 0;
    mOverlayTextureCoordBufferIndex = 0;
    mBaseTextureCoordBufferIndex = 0;
}

void GridQuad::freeHardwareBuffers() {
    if (mVertBufferIndex != 0) {
        GLuint buffers[4] = {mVertBufferIndex, mOverlayTextureCoordBufferIndex, mBaseTextureCoordBufferIndex,
                             mIndexBufferIndex};
        glDeleteBuffers(4, buffers);
        forgetHardwareBuffers();
    }
}

void GridQuad::generateHardwareBuffers() {
    if (mVertBufferIndex != 0) {
        return;
    }
    GLenum bufferType = mDynamicVBO ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

    glGenBuffers(1, &mVertBufferIndex);
    glBindBuffer(GL_ARRAY_BUFFER, mVertBufferIndex);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mVertexBuffer.size() * sizeof(float)), mVertexBuffer.data(), bufferType);

    glGenBuffers(1, &mOverlayTextureCoordBufferIndex);
    glBindBuffer(GL_ARRAY_BUFFER, mOverlayTextureCoordBufferIndex);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mOverlayTexCoordBuffer.size() * sizeof(float)),
                 mOverlayTexCoordBuffer.data(), bufferType);

    glGenBuffers(1, &mBaseTextureCoordBufferIndex);
    glBindBuffer(GL_ARRAY_BUFFER, mBaseTextureCoordBufferIndex);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mBaseTexCoordBuffer.size() * sizeof(float)), mBaseTexCoordBuffer.data(),
                 bufferType);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenBuffers(1, &mIndexBufferIndex);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIndexBufferIndex);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(mIndexBuffer.size() * sizeof(unsigned short)),
                 mIndexBuffer.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// Port of com.cooliris.media.MatrixStack, plus the pieces of android.opengl.Matrix
// and android.opengl.GLU that the original leaned on. ES 2.0 dropped the
// fixed-function matrix stack, so this class now backs the whole port.
//
// Storage is column major, the same layout OpenGL and android.opengl.Matrix use:
// m[column * 4 + row].
#pragma once

#include <vector>

struct Mat4 {
    float m[16];

    void setIdentity();
    // this = this * other
    void multiply(const Mat4 &other);
    // out = this * in, both 4 component vectors.
    void apply(const float in[4], float out[4]) const;

    void translate(float x, float y, float z);
    void scale(float x, float y, float z);
    void rotate(float angleDegrees, float x, float y, float z);

    static Mat4 identity();
    static Mat4 rotation(float angleDegrees, float x, float y, float z);
    static Mat4 frustum(float left, float right, float bottom, float top, float near, float far);
    static Mat4 ortho(float left, float right, float bottom, float top, float near, float far);
    static Mat4 perspective(float fovyDegrees, float aspect, float near, float far);
    static Mat4 lookAt(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ, float upX,
                       float upY, float upZ);
};

class MatrixStack {
  public:
    explicit MatrixStack(int maxDepth = 32);

    void glLoadIdentity();
    void glLoadMatrixf(const Mat4 &matrix);
    void glMultMatrixf(const Mat4 &matrix);
    void glPushMatrix();
    void glPopMatrix();
    void glTranslatef(float x, float y, float z);
    void glScalef(float x, float y, float z);
    void glRotatef(float angleDegrees, float x, float y, float z);
    void glFrustumf(float left, float right, float bottom, float top, float near, float far);
    void glOrthof(float left, float right, float bottom, float top, float near, float far);

    void apply(const float in[4], float out[4]) const;

    const Mat4 &top() const {
        return mStack[(size_t)mTop];
    }

    Mat4 &top() {
        return mStack[(size_t)mTop];
    }

  private:
    std::vector<Mat4> mStack;
    int mTop = 0;
};

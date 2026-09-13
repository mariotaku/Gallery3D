#include "core/MatrixStack.h"

#include <cmath>
#include <cstring>

static const float kPi = 3.14159265358979323846f;

void Mat4::setIdentity() {
    std::memset(m, 0, sizeof(m));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

Mat4 Mat4::identity() {
    Mat4 result;
    result.setIdentity();
    return result;
}

void Mat4::multiply(const Mat4 &other) {
    float out[16];
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += m[k * 4 + r] * other.m[c * 4 + k];
            }
            out[c * 4 + r] = sum;
        }
    }
    std::memcpy(m, out, sizeof(out));
}

void Mat4::apply(const float in[4], float out[4]) const {
    for (int r = 0; r < 4; ++r) {
        out[r] = m[0 * 4 + r] * in[0] + m[1 * 4 + r] * in[1] + m[2 * 4 + r] * in[2] + m[3 * 4 + r] * in[3];
    }
}

void Mat4::translate(float x, float y, float z) {
    for (int i = 0; i < 4; ++i) {
        m[12 + i] += m[0 + i] * x + m[4 + i] * y + m[8 + i] * z;
    }
}

void Mat4::scale(float x, float y, float z) {
    for (int i = 0; i < 4; ++i) {
        m[0 + i] *= x;
        m[4 + i] *= y;
        m[8 + i] *= z;
    }
}

Mat4 Mat4::rotation(float angleDegrees, float x, float y, float z) {
    Mat4 result;
    result.setIdentity();
    float a = angleDegrees * (kPi / 180.0f);
    float s = std::sin(a);
    float c = std::cos(a);

    if (x == 1.0f && y == 0.0f && z == 0.0f) {
        result.m[5] = c;
        result.m[10] = c;
        result.m[6] = s;
        result.m[9] = -s;
    } else if (x == 0.0f && y == 1.0f && z == 0.0f) {
        result.m[0] = c;
        result.m[10] = c;
        result.m[8] = s;
        result.m[2] = -s;
    } else if (x == 0.0f && y == 0.0f && z == 1.0f) {
        result.m[0] = c;
        result.m[5] = c;
        result.m[1] = s;
        result.m[4] = -s;
    } else {
        float len = std::sqrt(x * x + y * y + z * z);
        if (len != 1.0f && len != 0.0f) {
            float recip = 1.0f / len;
            x *= recip;
            y *= recip;
            z *= recip;
        }
        float nc = 1.0f - c;
        float xy = x * y;
        float yz = y * z;
        float zx = z * x;
        float xs = x * s;
        float ys = y * s;
        float zs = z * s;
        result.m[0] = x * x * nc + c;
        result.m[4] = xy * nc - zs;
        result.m[8] = zx * nc + ys;
        result.m[1] = xy * nc + zs;
        result.m[5] = y * y * nc + c;
        result.m[9] = yz * nc - xs;
        result.m[2] = zx * nc - ys;
        result.m[6] = yz * nc + xs;
        result.m[10] = z * z * nc + c;
    }
    return result;
}

void Mat4::rotate(float angleDegrees, float x, float y, float z) {
    multiply(Mat4::rotation(angleDegrees, x, y, z));
}

Mat4 Mat4::frustum(float left, float right, float bottom, float top, float near, float far) {
    Mat4 result;
    std::memset(result.m, 0, sizeof(result.m));
    float rWidth = 1.0f / (right - left);
    float rHeight = 1.0f / (top - bottom);
    float rDepth = 1.0f / (near - far);
    float x = 2.0f * (near * rWidth);
    float y = 2.0f * (near * rHeight);
    float A = (right + left) * rWidth;
    float B = (top + bottom) * rHeight;
    float C = (far + near) * rDepth;
    float D = 2.0f * (far * near * rDepth);
    result.m[0] = x;
    result.m[5] = y;
    result.m[8] = A;
    result.m[9] = B;
    result.m[10] = C;
    result.m[14] = D;
    result.m[11] = -1.0f;
    return result;
}

Mat4 Mat4::ortho(float left, float right, float bottom, float top, float near, float far) {
    Mat4 result;
    result.setIdentity();
    float rWidth = 1.0f / (right - left);
    float rHeight = 1.0f / (top - bottom);
    float rDepth = 1.0f / (far - near);
    result.m[0] = 2.0f * rWidth;
    result.m[5] = 2.0f * rHeight;
    result.m[10] = -2.0f * rDepth;
    result.m[12] = -(right + left) * rWidth;
    result.m[13] = -(top + bottom) * rHeight;
    result.m[14] = -(far + near) * rDepth;
    result.m[15] = 1.0f;
    return result;
}

Mat4 Mat4::perspective(float fovyDegrees, float aspect, float near, float far) {
    float top = near * std::tan(fovyDegrees * (kPi / 360.0f));
    float right = top * aspect;
    return Mat4::frustum(-right, right, -top, top, near, far);
}

Mat4 Mat4::lookAt(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ, float upX, float upY,
                  float upZ) {
    float fx = centerX - eyeX;
    float fy = centerY - eyeY;
    float fz = centerZ - eyeZ;

    float rlf = 1.0f / std::sqrt(fx * fx + fy * fy + fz * fz);
    fx *= rlf;
    fy *= rlf;
    fz *= rlf;

    float sx = fy * upZ - fz * upY;
    float sy = fz * upX - fx * upZ;
    float sz = fx * upY - fy * upX;

    float rls = 1.0f / std::sqrt(sx * sx + sy * sy + sz * sz);
    sx *= rls;
    sy *= rls;
    sz *= rls;

    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;

    Mat4 result;
    result.m[0] = sx;
    result.m[1] = ux;
    result.m[2] = -fx;
    result.m[3] = 0.0f;
    result.m[4] = sy;
    result.m[5] = uy;
    result.m[6] = -fy;
    result.m[7] = 0.0f;
    result.m[8] = sz;
    result.m[9] = uz;
    result.m[10] = -fz;
    result.m[11] = 0.0f;
    result.m[12] = 0.0f;
    result.m[13] = 0.0f;
    result.m[14] = 0.0f;
    result.m[15] = 1.0f;
    result.translate(-eyeX, -eyeY, -eyeZ);
    return result;
}

// The stack always holds the top matrix, so a depth below one has no meaning.
MatrixStack::MatrixStack(int maxDepth) : mStack(maxDepth > 1 ? (size_t)maxDepth : 1) {
    glLoadIdentity();
}

void MatrixStack::glLoadIdentity() {
    mStack[(size_t)mTop].setIdentity();
}

void MatrixStack::glLoadMatrixf(const Mat4 &matrix) {
    mStack[(size_t)mTop] = matrix;
}

void MatrixStack::glMultMatrixf(const Mat4 &matrix) {
    mStack[(size_t)mTop].multiply(matrix);
}

void MatrixStack::glPushMatrix() {
    if (mTop + 1 < (int)mStack.size()) {
        mStack[(size_t)(mTop + 1)] = mStack[(size_t)mTop];
        ++mTop;
    }
}

void MatrixStack::glPopMatrix() {
    if (mTop > 0) {
        --mTop;
    }
}

void MatrixStack::glTranslatef(float x, float y, float z) {
    mStack[(size_t)mTop].translate(x, y, z);
}

void MatrixStack::glScalef(float x, float y, float z) {
    mStack[(size_t)mTop].scale(x, y, z);
}

void MatrixStack::glRotatef(float angleDegrees, float x, float y, float z) {
    mStack[(size_t)mTop].rotate(angleDegrees, x, y, z);
}

void MatrixStack::glFrustumf(float left, float right, float bottom, float top, float near, float far) {
    mStack[(size_t)mTop] = Mat4::frustum(left, right, bottom, top, near, far);
}

void MatrixStack::glOrthof(float left, float right, float bottom, float top, float near, float far) {
    mStack[(size_t)mTop] = Mat4::ortho(left, right, bottom, top, near, far);
}

void MatrixStack::apply(const float in[4], float out[4]) const {
    mStack[(size_t)mTop].apply(in, out);
}

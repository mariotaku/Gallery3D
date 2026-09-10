// Port of com.cooliris.media.Vector3f.
#pragma once

struct Vector3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vector3f() = default;

    Vector3f(float xx, float yy, float zz) : x(xx), y(yy), z(zz) {}

    void set(const Vector3f &vector) {
        x = vector.x;
        y = vector.y;
        z = vector.z;
    }

    void set(float xx, float yy, float zz) {
        x = xx;
        y = yy;
        z = zz;
    }

    void add(const Vector3f &vector) {
        x += vector.x;
        y += vector.y;
        z += vector.z;
    }

    void add(float xx, float yy, float zz) {
        x += xx;
        y += yy;
        z += zz;
    }

    void subtract(const Vector3f &vector) {
        x -= vector.x;
        y -= vector.y;
        z -= vector.z;
    }

    void scale(float sx, float sy, float sz) {
        x *= sx;
        y *= sy;
        z *= sz;
    }

    bool equals(const Vector3f &vector) const {
        return x == vector.x && y == vector.y && z == vector.z;
    }
};

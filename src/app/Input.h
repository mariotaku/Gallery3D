// Stands in for android.view.MotionEvent and android.view.KeyEvent.
// Carries two pointers; the second is populated only for multitouch.
#pragma once

#include <cstdint>

struct MotionEvent {
    enum Action {
        ACTION_DOWN = 0,
        ACTION_UP = 1,
        ACTION_MOVE = 2,
        ACTION_CANCEL = 3,
        ACTION_POINTER_DOWN = 5,
        ACTION_POINTER_UP = 6,
    };

    int action = ACTION_DOWN;
    int pointerCount = 1;
    // Index of the pointer that went down or up, for the POINTER_ actions.
    int actionIndex = 0;
    float xs[2] = {0.0f, 0.0f};
    float ys[2] = {0.0f, 0.0f};
    uint64_t eventTime = 0;

    int getAction() const {
        return action;
    }

    int getPointerCount() const {
        return pointerCount;
    }

    float getX() const {
        return xs[0];
    }

    float getY() const {
        return ys[0];
    }

    float getX(int index) const {
        return xs[index & 1];
    }

    float getY(int index) const {
        return ys[index & 1];
    }

    uint64_t getEventTime() const {
        return eventTime;
    }
};

struct KeyEvent {
    enum Action {
        ACTION_DOWN = 0,
        ACTION_UP = 1,
    };

    // Only the codes the grid actually reacts to.
    enum Code {
        KEYCODE_UNKNOWN = 0,
        KEYCODE_BACK,
        KEYCODE_MENU,
        KEYCODE_DPAD_UP,
        KEYCODE_DPAD_DOWN,
        KEYCODE_DPAD_LEFT,
        KEYCODE_DPAD_RIGHT,
        KEYCODE_DPAD_CENTER,
    };

    int action = ACTION_DOWN;
    int keyCode = KEYCODE_UNKNOWN;

    int getAction() const {
        return action;
    }

    int getKeyCode() const {
        return keyCode;
    }
};

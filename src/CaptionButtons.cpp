#include "CaptionButtons.h"

#include <algorithm>
#include <cmath>

#include "App.h"
#include "Canvas.h"

namespace {

// Windows caption-button dimensions at 100% display scale.
const float BUTTON_WIDTH = 46.0f;
const float BUTTON_HEIGHT = 32.0f;
const int BUTTON_COUNT = 3;

// The glyph box inside a button. Windows draws these at 10 by 10.
const float GLYPH_SIZE = 10.0f;

// Close goes red, the other two take a plain wash. These are the system's own
// colours for a dark window.
const float CLOSE_R = 0.769f;
const float CLOSE_G = 0.169f;
const float CLOSE_B = 0.110f;

enum { BUTTON_MINIMIZE = 0, BUTTON_MAXIMIZE = 1, BUTTON_CLOSE = 2 };

float scaled(float value) {
    return value * App::UI_DENSITY;
}

}  // namespace

CaptionButtons::CaptionButtons() {
    mTexture = std::make_shared<ButtonsTexture>(this);
}

CaptionButtons::~CaptionButtons() = default;

float CaptionButtons::preferredWidth() {
    return scaled(BUTTON_WIDTH) * (float)BUTTON_COUNT;
}

float CaptionButtons::preferredHeight() {
    return scaled(BUTTON_HEIGHT);
}

void CaptionButtons::setActions(std::function<void()> minimize, std::function<void()> toggleMaximize,
                                std::function<void()> close) {
    mMinimize = std::move(minimize);
    mToggleMaximize = std::move(toggleMaximize);
    mClose = std::move(close);
}

void CaptionButtons::setMaximized(bool maximized) {
    if (mMaximized == maximized) {
        return;
    }
    mMaximized = maximized;
    invalidate();
}

void CaptionButtons::invalidate() {
    mNeedsDraw = true;
    mTexture->setNeedsDraw();
}

void CaptionButtons::onSizeChanged() {
    mTexture->setSize((int)(preferredWidth() + 0.5f), (int)(preferredHeight() + 0.5f));
    invalidate();
}

int CaptionButtons::buttonAt(float x, float y) const {
    if (y < mY || y >= mY + preferredHeight()) {
        return -1;
    }
    const float width = scaled(BUTTON_WIDTH);
    const float offset = x - mX;
    if (offset < 0.0f || offset >= width * (float)BUTTON_COUNT) {
        return -1;
    }
    return (int)(offset / width);
}

bool CaptionButtons::containsPoint(float x, float y) {
    return buttonAt(x, y) >= 0;
}

void CaptionButtons::onPointerMoved(float x, float y) {
    const int hovered = buttonAt(x, y);
    if (hovered == mHovered) {
        return;
    }
    mHovered = hovered;
    invalidate();
}

bool CaptionButtons::onTouchEvent(const MotionEvent &event) {
    const float x = event.getX();
    const float y = event.getY();
    const int index = buttonAt(x, y);

    switch (event.getAction()) {
    case MotionEvent::ACTION_DOWN:
        if (index < 0) {
            return false;
        }
        mPressed = index;
        mHovered = index;
        invalidate();
        return true;
    case MotionEvent::ACTION_MOVE:
        if (mPressed < 0) {
            return false;
        }
        // A press that slides off the button lets go of it, and sliding back
        // takes it again. That is how the system's own buttons behave.
        if (mHovered != index) {
            mHovered = index;
            invalidate();
        }
        return true;
    case MotionEvent::ACTION_UP: {
        const int pressed = mPressed;
        mPressed = -1;
        invalidate();
        if (pressed < 0 || pressed != index) {
            return pressed >= 0;
        }
        // The action runs last. Close destroys the window this was drawn on,
        // so nothing may touch the layer afterwards.
        switch (pressed) {
        case BUTTON_MINIMIZE:
            if (mMinimize) {
                mMinimize();
            }
            break;
        case BUTTON_MAXIMIZE:
            if (mToggleMaximize) {
                mToggleMaximize();
            }
            break;
        case BUTTON_CLOSE:
            if (mClose) {
                mClose();
            }
            break;
        default:
            break;
        }
        return true;
    }
    default:
        break;
    }
    return false;
}

void CaptionButtons::generate(RenderView *view, RenderLists &lists) {
    (void)view;
    lists.blendedList.push_back(this);
    lists.hitTestList.push_back(this);
}

void CaptionButtons::renderBlended(RenderView *view) {
    if (mTexture->getCanvasWidth() <= 0) {
        onSizeChanged();
    }
    view->loadTexture(mTexture);
    if (!mTexture->isLoaded()) {
        return;
    }
    // Caption buttons remain fully opaque when the HUD fades.
    view->setAlpha(1.0f);
    view->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    view->draw2D(mTexture, mX, mY, (float)mTexture->getCanvasWidth(), (float)mTexture->getCanvasHeight());
}

Bitmap CaptionButtons::compose() {
    if (mTexture->getCanvasWidth() <= 0) {
        onSizeChanged();
    }
    return mTexture->load(nullptr);
}

void CaptionButtons::ButtonsTexture::renderCanvas(Bitmap &canvas, int width, int height) {
    (void)width;
    const float buttonWidth = scaled(BUTTON_WIDTH);
    const float glyph = scaled(GLYPH_SIZE);
    // One device pixel at 100%, thickening with the display rather than staying
    // hairline on a dense screen.
    const float stroke = std::max(1.0f, std::floor(App::UI_DENSITY + 0.5f));

    for (int i = 0; i < BUTTON_COUNT; ++i) {
        const float left = buttonWidth * (float)i;
        const bool hovered = (mOwner->mHovered == i);
        const bool pressed = (mOwner->mPressed == i && hovered);

        if (hovered) {
            if (i == BUTTON_CLOSE) {
                // Close uses a distinct hover colour.
                const float alpha = pressed ? 0.7f : 1.0f;
                Canvas::fillRect(canvas, (int)left, 0, (int)buttonWidth, height, CLOSE_R, CLOSE_G, CLOSE_B, alpha);
            } else {
                const float alpha = pressed ? 0.16f : 0.09f;
                Canvas::fillRect(canvas, (int)left, 0, (int)buttonWidth, height, 1.0f, 1.0f, 1.0f, alpha);
            }
        }

        // The glyph box, centred.
        const float glyphLeft = left + (buttonWidth - glyph) * 0.5f;
        const float glyphTop = ((float)height - glyph) * 0.5f;
        const float glyphRight = glyphLeft + glyph;
        const float glyphBottom = glyphTop + glyph;

        switch (i) {
        case BUTTON_MINIMIZE:
            Canvas::drawLine(canvas, glyphLeft, (float)height * 0.5f, glyphRight, (float)height * 0.5f, stroke, 1.0f,
                             1.0f, 1.0f, 1.0f);
            break;
        case BUTTON_MAXIMIZE:
            if (mOwner->mMaximized) {
                // Restore: the front pane, and the corner of the one behind it.
                const float step = std::max(1.0f, glyph * 0.25f);
                Canvas::drawLine(canvas, glyphLeft, glyphTop + step, glyphRight - step, glyphTop + step, stroke, 1.0f,
                                 1.0f, 1.0f, 1.0f);
                Canvas::drawLine(canvas, glyphLeft, glyphBottom, glyphRight - step, glyphBottom, stroke, 1.0f, 1.0f,
                                 1.0f, 1.0f);
                Canvas::drawLine(canvas, glyphLeft, glyphTop + step, glyphLeft, glyphBottom, stroke, 1.0f, 1.0f, 1.0f,
                                 1.0f);
                Canvas::drawLine(canvas, glyphRight - step, glyphTop + step, glyphRight - step, glyphBottom, stroke,
                                 1.0f, 1.0f, 1.0f, 1.0f);
                Canvas::drawLine(canvas, glyphLeft + step, glyphTop, glyphRight, glyphTop, stroke, 1.0f, 1.0f, 1.0f,
                                 1.0f);
                Canvas::drawLine(canvas, glyphRight, glyphTop, glyphRight, glyphBottom - step, stroke, 1.0f, 1.0f,
                                 1.0f, 1.0f);
            } else {
                Canvas::drawLine(canvas, glyphLeft, glyphTop, glyphRight, glyphTop, stroke, 1.0f, 1.0f, 1.0f, 1.0f);
                Canvas::drawLine(canvas, glyphLeft, glyphBottom, glyphRight, glyphBottom, stroke, 1.0f, 1.0f, 1.0f,
                                 1.0f);
                Canvas::drawLine(canvas, glyphLeft, glyphTop, glyphLeft, glyphBottom, stroke, 1.0f, 1.0f, 1.0f, 1.0f);
                Canvas::drawLine(canvas, glyphRight, glyphTop, glyphRight, glyphBottom, stroke, 1.0f, 1.0f, 1.0f,
                                 1.0f);
            }
            break;
        case BUTTON_CLOSE:
            Canvas::drawLine(canvas, glyphLeft, glyphTop, glyphRight, glyphBottom, stroke, 1.0f, 1.0f, 1.0f, 1.0f);
            Canvas::drawLine(canvas, glyphRight, glyphTop, glyphLeft, glyphBottom, stroke, 1.0f, 1.0f, 1.0f, 1.0f);
            break;
        default:
            break;
        }
    }
    mOwner->mNeedsDraw = false;
}

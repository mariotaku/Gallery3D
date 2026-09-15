#include "hud/PathBarLayer.h"

#include <algorithm>

#include "app/App.h"
#include "graphics/Canvas.h"
#include "graphics/DrawableLoad.h"

namespace {

// The shipped art is 39 pixels tall and the fill is a single column, stretched
// horizontally. Everything below is in those units, scaled by the density.
const float ART_HEIGHT = 39.0f;
const float ICON_SIZE = 39.0f;
// pathbar_join is 21 wide and pathbar_cap 22. Layout needs both before the art
// is loaded, so they are named here alongside the rest.
const float JOIN_WIDTH = 21.0f;
const float CAP_WIDTH = 22.0f;
const float FONT_SIZE = 18.0f;
// The black halo behind the label, tighter than StringTexture.Config's default
// in the original. It keeps the label readable where the bar shows a bright
// photo through it. The opacity stays under 1, since blendOver clamps coverage
// at 1 and a stronger halo turns into a flat dark box around the text.
const float SHADOW_RADIUS = 2.8f;
const float SHADOW_OPACITY = 0.91f;
// Gap between the icon and the label, and the padding after the label.
const float TEXT_GAP = 4.0f;
const float TRAILING_PAD = 10.0f;

// In whole pixels, rounded as the art's sizes are, so the crumb icons, the join
// and the cap are drawn at exactly the size they were rendered at.
float scaled(float value) {
    return (float)App::uiPixels(value);
}

}  // namespace

PathBarLayer::PathBarLayer() {
    mTexture = std::make_shared<BarTexture>(this);
}

PathBarLayer::~PathBarLayer() = default;

float PathBarLayer::preferredHeight() {
    return scaled(ART_HEIGHT);
}

void PathBarLayer::invalidate() {
    mNeedsLayout = true;
}

void PathBarLayer::pushLabel(const char *icon, const std::string &label, Action action) {
    Component component;
    component.icon = (icon != nullptr) ? icon : "";
    component.label = label;
    component.action = std::move(action);
    mComponents.push_back(std::move(component));
    invalidate();
}

void PathBarLayer::popLabel() {
    if (!mComponents.empty()) {
        mComponents.pop_back();
        invalidate();
    }
}

void PathBarLayer::changeLabel(const std::string &label) {
    if (label.empty() || mComponents.empty()) {
        return;
    }
    if (mComponents.back().label == label) {
        return;
    }
    mComponents.back().label = label;
    invalidate();
}

void PathBarLayer::changeLabelAt(size_t index, const std::string &label) {
    if (label.empty() || index >= mComponents.size() || mComponents[index].label == label) {
        return;
    }
    mComponents[index].label = label;
    invalidate();
}

float PathBarLayer::crumbCenterX(size_t index) const {
    if (index >= mComponents.size()) {
        return mX;
    }
    return mX + mComponents[index].x + mComponents[index].width * 0.5f;
}

std::string PathBarLayer::getCurrentLabel() const {
    return mComponents.empty() ? std::string() : mComponents.back().label;
}

int PathBarLayer::getNumLevels() const {
    return (int)mComponents.size();
}

void PathBarLayer::clear() {
    mComponents.clear();
    invalidate();
}

void PathBarLayer::onSizeChanged() {
    invalidate();
}

void PathBarLayer::layout() {
    mNeedsLayout = false;

    // Only the last crumb shows its label, as in the original: the ones behind
    // it collapse to their icon.
    float x = 0.0f;
    for (size_t i = 0; i < mComponents.size(); ++i) {
        Component &component = mComponents[i];
        if (i > 0) {
            // Leave the seam for the chevron that separates this crumb from
            // the one before it.
            x += scaled(JOIN_WIDTH);
        }
        float width = scaled(ICON_SIZE);
        if (i + 1 == mComponents.size() && !component.label.empty()) {
            int textWidth = 0;
            Canvas::measureText(component.label, scaled(FONT_SIZE), false, &textWidth, nullptr);
            width += scaled(TEXT_GAP) + (float)textWidth + scaled(TRAILING_PAD);
        }
        component.x = x;
        component.width = width;
        x += width;
    }
    // The rounded cap sits past the last crumb, not on top of it.
    mBarWidth = x + scaled(CAP_WIDTH);

    // Nothing to draw with no crumbs, and a zero sized texture would only be
    // rejected by the loader.
    int barWidth = (int)(mBarWidth + 0.5f);
    int barHeight = (int)(scaled(ART_HEIGHT) + 0.5f);
    if (barWidth <= 0 || barHeight <= 0) {
        mTexture->setSize(0, 0);
        return;
    }
    mTexture->setSize(barWidth, barHeight);
    mTexture->setNeedsDraw();
}

void PathBarLayer::BarTexture::renderCanvas(Bitmap &canvas, int width, int height) {
    Bitmap fill = DrawableLoad::load("pathbar_bg").bitmap;
    Bitmap cap = DrawableLoad::load("pathbar_cap").bitmap;
    Bitmap join = DrawableLoad::load("pathbar_join").bitmap;

    // Fill only crumb spans; joins and caps occupy gaps to avoid double blending.
    // Start each span at the previous endpoint to prevent fractional-density rounding gaps.
    auto paintSpan = [&](const Bitmap &art, int x, int spanWidth) {
        if (spanWidth <= 0) {
            return;
        }
        if (art.valid()) {
            Canvas::blitScaled(canvas, art, x, 0, spanWidth, height);
        } else {
            // Use a flat fill when art is unavailable.
            Canvas::fillRect(canvas, x, 0, spanWidth, height, 0.0f, 0.0f, 0.0f, 0.18f);
        }
    };

    const std::vector<Component> &components = mOwner->mComponents;
    int spanStart = 0;

    for (size_t i = 0; i < components.size(); ++i) {
        const Component &component = components[i];
        int x = (int)(component.x + 0.5f);
        int componentEnd = (int)(component.x + component.width + 0.5f);

        if (i == 0) {
            // The first crumb takes the fill all the way back to the left edge.
            paintSpan(fill, 0, componentEnd);
        } else {
            // The join fills everything the layout left between the crumbs.
            paintSpan(join, spanStart, x - spanStart);
            paintSpan(fill, x, componentEnd - x);
        }
        spanStart = componentEnd;

        if (!component.icon.empty()) {
            Bitmap icon = DrawableLoad::load(component.icon).bitmap;
            if (icon.valid()) {
                int iconSize = (int)scaled(ICON_SIZE);
                Canvas::blitScaled(canvas, icon, x, (height - iconSize) / 2, iconSize, iconSize);
            }
        }

        if (i + 1 == components.size() && !component.label.empty()) {
            int textWidth = 0;
            int textHeight = 0;
            Canvas::measureText(component.label, scaled(FONT_SIZE), false, &textWidth, &textHeight);
            int textX = x + (int)scaled(ICON_SIZE) + (int)scaled(TEXT_GAP);
            int textY = (height - textHeight) / 2;
            Canvas::drawText(canvas, component.label, textX, textY, scaled(FONT_SIZE), false, 1.0f, 1.0f, 1.0f,
                             1.0f, (int)scaled(SHADOW_RADIUS), SHADOW_OPACITY);
        }
    }

    // And the cap takes the rest, so it reaches the edge of the texture however
    // the widths rounded.
    paintSpan(cap, spanStart, width - spanStart);
}

void PathBarLayer::generate(RenderView *view, RenderLists &lists) {
    (void)view;
    lists.blendedList.push_back(this);
    lists.hitTestList.push_back(this);
}

Bitmap PathBarLayer::compose() {
    if (mNeedsLayout) {
        layout();
    }
    // load() ignores the render view, so there is nothing to pass it.
    return mTexture->load(nullptr);
}

void PathBarLayer::renderBlended(RenderView *view) {
    if (mNeedsLayout) {
        layout();
    }
    if (mComponents.empty() || mTexture->getCanvasWidth() <= 0) {
        return;
    }
    view->loadTexture(mTexture);
    if (!mTexture->isLoaded()) {
        return;
    }
    // draw2D z = 0 puts chrome in front. Retain HudLayer's colour to fade the
    // premultiplied bar with the rest of the HUD.
    view->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    view->draw2D(mTexture, mX, mY, (float)mTexture->getCanvasWidth(), (float)mTexture->getCanvasHeight());
}

bool PathBarLayer::containsPoint(float x, float y) {
    return x >= mX && x < mX + mBarWidth && y >= mY && y < mY + preferredHeight();
}

bool PathBarLayer::onTouchEvent(const MotionEvent &event) {
    float localX = event.getX() - mX;
    float localY = event.getY() - mY;

    switch (event.getAction()) {
    case MotionEvent::ACTION_DOWN:
        mTouchIndex = -1;
        if (localY >= 0.0f && localY < preferredHeight()) {
            for (size_t i = 0; i < mComponents.size(); ++i) {
                const Component &component = mComponents[i];
                if (localX >= component.x && localX < component.x + component.width) {
                    mTouchIndex = (int)i;
                    break;
                }
            }
        }
        break;
    case MotionEvent::ACTION_UP:
        if (mTouchIndex >= 0 && mTouchIndex < (int)mComponents.size()) {
            // Copy the action before running it: it usually changes state, and
            // that can rewrite the component list underneath us.
            Action action = mComponents[(size_t)mTouchIndex].action;
            mTouchIndex = -1;
            if (action) {
                action();
            }
        }
        mTouchIndex = -1;
        break;
    case MotionEvent::ACTION_CANCEL:
        mTouchIndex = -1;
        break;
    default:
        break;
    }
    return true;
}

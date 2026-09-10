#include "MenuBar.h"

#include <algorithm>

#include "App.h"
#include "Canvas.h"

namespace {

// selection_menu_bg ships as a single 58 tall column, stretched across.
const float ART_HEIGHT = 58.0f;
const float ICON_SIZE = 34.0f;

float scaled(float value) {
    return value * App::PIXEL_DENSITY;
}

}  // namespace

MenuBar::MenuBar() {
    mTexture = std::make_shared<BarTexture>(this);
}

MenuBar::~MenuBar() = default;

float MenuBar::preferredHeight() {
    return scaled(ART_HEIGHT);
}

void MenuBar::setButtons(const std::vector<std::pair<std::string, Action>> &buttons) {
    mButtons.clear();
    for (const auto &entry : buttons) {
        Button button;
        button.icon = entry.first;
        button.action = entry.second;
        mButtons.push_back(std::move(button));
    }
    mNeedsLayout = true;
    setHidden(mButtons.empty());
}

void MenuBar::clearButtons() {
    mButtons.clear();
    mNeedsLayout = true;
    setHidden(true);
}

void MenuBar::onSizeChanged() {
    mNeedsLayout = true;
}

void MenuBar::layout() {
    mNeedsLayout = false;
    int barWidth = (int)(mWidth + 0.5f);
    int barHeight = (int)(preferredHeight() + 0.5f);
    if (barWidth <= 0 || barHeight <= 0 || mButtons.empty()) {
        mTexture->setSize(0, 0);
        return;
    }
    // Buttons share the bar evenly, which is what the original did once the
    // selection menu had decided how many there were.
    float share = mWidth / (float)mButtons.size();
    for (size_t i = 0; i < mButtons.size(); ++i) {
        mButtons[i].x = share * (float)i;
        mButtons[i].width = share;
    }
    mTexture->setSize(barWidth, barHeight);
    mTexture->setNeedsDraw();
}

void MenuBar::BarTexture::renderCanvas(Bitmap &canvas, int width, int height) {
    Bitmap fill = Bitmap::load(App::drawablePath("selection_menu_bg"), 0);
    Bitmap divider = Bitmap::load(App::drawablePath("selection_menu_divider"), 0);

    if (fill.valid()) {
        Canvas::blitScaled(canvas, fill, 0, 0, width, height);
    } else {
        Canvas::fillRect(canvas, 0, 0, width, height, 0.0f, 0.0f, 0.0f, 0.7f);
    }

    const std::vector<Button> &buttons = mOwner->mButtons;
    int iconSize = (int)scaled(ICON_SIZE);
    for (size_t i = 0; i < buttons.size(); ++i) {
        const Button &button = buttons[i];
        if (i > 0 && divider.valid()) {
            int dividerWidth = std::max(1, (int)scaled(1.0f));
            Canvas::blitScaled(canvas, divider, (int)button.x, 0, dividerWidth, height);
        }
        Bitmap icon = Bitmap::load(App::drawablePath(button.icon), 0);
        if (icon.valid()) {
            int x = (int)(button.x + (button.width - (float)iconSize) * 0.5f);
            Canvas::blitScaled(canvas, icon, x, (height - iconSize) / 2, iconSize, iconSize);
        }
    }
}

void MenuBar::generate(RenderView *view, RenderLists &lists) {
    (void)view;
    lists.blendedList.push_back(this);
    lists.hitTestList.push_back(this);
}

void MenuBar::renderBlended(RenderView *view) {
    if (mNeedsLayout) {
        layout();
    }
    if (mButtons.empty() || mTexture->getCanvasWidth() <= 0) {
        return;
    }
    view->loadTexture(mTexture);
    if (!mTexture->isLoaded()) {
        return;
    }
    // The colour is left as HudLayer set it, so the bar fades with the rest.
    view->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    view->draw2D(mTexture, mX, mY, (float)mTexture->getCanvasWidth(), (float)mTexture->getCanvasHeight());
}

bool MenuBar::containsPoint(float x, float y) {
    return !mButtons.empty() && x >= mX && x < mX + mWidth && y >= mY && y < mY + preferredHeight();
}

bool MenuBar::onTouchEvent(const MotionEvent &event) {
    float localX = event.getX() - mX;
    float localY = event.getY() - mY;

    switch (event.getAction()) {
    case MotionEvent::ACTION_DOWN:
        mTouchIndex = -1;
        if (localY >= 0.0f && localY < preferredHeight()) {
            for (size_t i = 0; i < mButtons.size(); ++i) {
                if (localX >= mButtons[i].x && localX < mButtons[i].x + mButtons[i].width) {
                    mTouchIndex = (int)i;
                    break;
                }
            }
        }
        break;
    case MotionEvent::ACTION_UP:
        if (mTouchIndex >= 0 && mTouchIndex < (int)mButtons.size()) {
            // Copy before running: delete rewrites the button list underneath
            // us when the selection empties.
            Action action = mButtons[(size_t)mTouchIndex].action;
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

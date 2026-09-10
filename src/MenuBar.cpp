#include "MenuBar.h"

#include <algorithm>

#include "App.h"
#include "Canvas.h"

namespace {

// selection_menu_bg ships as a single 58 tall column, stretched across.
const float ART_HEIGHT = 58.0f;
const float ICON_SIZE = 34.0f;
const float FONT_SIZE = 17.0f;
// The highlight is inset from the button edges, and its own caps sit outside
// that inset. Both from the original.
const float HIGHLIGHT_INSET = 9.0f;
const float HIGHLIGHT_EDGE_WIDTH = 21.0f;

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

void MenuBar::setButtons(const std::vector<ButtonSpec> &buttons) {
    mButtons.clear();
    for (const ButtonSpec &spec : buttons) {
        Button button;
        button.icon = spec.icon;
        button.label = spec.label;
        button.action = spec.action;
        mButtons.push_back(std::move(button));
    }
    mPressedIndex = -1;
    mNeedsLayout = true;
    setHidden(mButtons.empty());
}

void MenuBar::clearButtons() {
    mButtons.clear();
    mPressedIndex = -1;
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
    if (mOwner->mPressedIndex >= 0 && mOwner->mPressedIndex < (int)buttons.size()) {
        drawHighlight(canvas, buttons[(size_t)mOwner->mPressedIndex], height);
    }

    int iconSize = (int)scaled(ICON_SIZE);
    float fontSize = scaled(FONT_SIZE);
    for (size_t i = 0; i < buttons.size(); ++i) {
        const Button &button = buttons[i];
        if (i > 0 && divider.valid()) {
            int dividerWidth = std::max(1, (int)scaled(1.0f));
            Canvas::blitScaled(canvas, divider, (int)button.x, 0, dividerWidth, height);
        }
        Bitmap icon = button.icon.empty() ? Bitmap() : Bitmap::load(App::drawablePath(button.icon), 0);
        int iconWidth = icon.valid() ? iconSize : 0;
        int labelWidth = 0;
        int labelHeight = 0;
        if (!button.label.empty()) {
            Canvas::measureText(button.label, fontSize, false, &labelWidth, &labelHeight);
        }
        // Icon and label sit side by side, and the pair is centred in the
        // button rather than each half being centred on its own.
        int contentWidth = iconWidth + labelWidth;
        int x = (int)(button.x + (button.width - (float)contentWidth) * 0.5f);
        if (icon.valid()) {
            Canvas::blitScaled(canvas, icon, x, (height - iconSize) / 2, iconSize, iconSize);
            x += iconWidth;
        }
        if (labelWidth > 0) {
            Canvas::drawText(canvas, button.label, x, (height - labelHeight) / 2, fontSize, false, 1.0f, 1.0f,
                             1.0f, 1.0f, 0);
        }
    }
}

void MenuBar::BarTexture::drawHighlight(Bitmap &canvas, const Button &button, int height) {
    Bitmap left = Bitmap::load(App::drawablePath("selection_menu_bg_pressed_left"), 0);
    Bitmap middle = Bitmap::load(App::drawablePath("selection_menu_bg_pressed"), 0);
    Bitmap right = Bitmap::load(App::drawablePath("selection_menu_bg_pressed_right"), 0);
    if (!middle.valid()) {
        return;
    }
    int inset = (int)scaled(HIGHLIGHT_INSET);
    int edge = (int)scaled(HIGHLIGHT_EDGE_WIDTH);
    int x = (int)button.x + inset;
    int width = (int)button.width - inset * 2;
    if (width <= 0) {
        return;
    }
    if (left.valid()) {
        Canvas::blitScaled(canvas, left, x - edge, 0, edge, height);
    }
    Canvas::blitScaled(canvas, middle, x, 0, width, height);
    if (right.valid()) {
        Canvas::blitScaled(canvas, right, x + width, 0, edge, height);
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
    int hit = -1;
    if (localY >= 0.0f && localY < preferredHeight()) {
        for (size_t i = 0; i < mButtons.size(); ++i) {
            if (localX >= mButtons[i].x && localX < mButtons[i].x + mButtons[i].width) {
                hit = (int)i;
                break;
            }
        }
    }

    int wasPressed = mPressedIndex;
    switch (event.getAction()) {
    case MotionEvent::ACTION_DOWN:
    case MotionEvent::ACTION_MOVE:
        mPressedIndex = hit;
        break;
    case MotionEvent::ACTION_UP:
        mPressedIndex = -1;
        if (wasPressed >= 0 && wasPressed == hit && wasPressed < (int)mButtons.size()) {
            // Copied before it runs: delete rewrites the button list underneath
            // us when the selection empties.
            Action action = mButtons[(size_t)wasPressed].action;
            if (action) {
                // The highlight has to come off before the action fires, or a
                // rebuilt bar keeps it.
                mTexture->setNeedsDraw();
                action();
                return true;
            }
        }
        break;
    case MotionEvent::ACTION_CANCEL:
        mPressedIndex = -1;
        break;
    default:
        break;
    }
    if (mPressedIndex != wasPressed) {
        mTexture->setNeedsDraw();
    }
    return true;
}

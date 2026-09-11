#include "PopupMenu.h"

#include <algorithm>

#include <SDL3/SDL.h>

#include "App.h"

namespace {

// All from the original, in the units its layout constants are written in.
const float POPUP_TRIANGLE_X_MARGIN = 16.0f;
const float POPUP_Y_OFFSET = 20.0f;
const float PADDING_LEFT = 15.0f;
const float PADDING_TOP = 13.0f;
const float PADDING_RIGHT = 15.0f;
const float PADDING_BOTTOM = 40.0f;
const float ICON_TITLE_MIN_WIDTH = 100.0f;
const float ROW_HEIGHT = 36.0f;
// The icon sits centred in a column this wide, and the title starts after it.
const float ICON_SPAN = 45.0f;
const float ICON_SIZE = 34.0f;
const float FONT_SIZE = 17.0f;
// IconTitleDrawable adds this past the title before the padding.
const float TITLE_TRAIL = 15.0f;
// The triangle art, in the same units. It ships at one density like everything
// else, so it is resized rather than drawn as it loads.
const float TRIANGLE_WIDTH = 43.0f;
const float TRIANGLE_HEIGHT = 28.0f;
// How far short of the bottom the panel stops. Its nine-patch carries a soft
// drop shadow below its border, so the border itself ends up one triangle
// height above the bottom, which is where the triangle's own top two rows
// continue it into the V.
const float POPUP_TRIANGLE_EXTRA_HEIGHT = 14.0f;

const float OPEN_SECONDS = 0.4f;
const float CLOSE_SECONDS = 0.3f;

float scaled(float value) {
    return value * App::UI_DENSITY;
}

}  // namespace

PopupMenu::PopupMenu() {
    mTexture = std::make_shared<PopupTexture>(this);
    setHidden(true);
}

PopupMenu::~PopupMenu() = default;

void PopupMenu::ensureArt() {
    if (mArtLoaded) {
        return;
    }
    mArtLoaded = true;
    mBackground = Canvas::loadNinePatch("popup.9");
    mHighlight = Canvas::loadNinePatch("popup_option_selected.9");
    // Resized on load, because it is stamped rather than stretched: its top row
    // is opaque and full width so that it merges with the bottom of the panel,
    // and showAtPoint needs its width before anything has been drawn.
    Bitmap triangle = Bitmap::load(App::drawablePath("popup_triangle_bottom"), 0);
    if (triangle.valid()) {
        mTriangle = triangle.scaled((int)scaled(TRIANGLE_WIDTH), (int)scaled(TRIANGLE_HEIGHT));
    }
}

void PopupMenu::setOptions(const std::vector<Option> &options) {
    close(false);
    mRows.clear();
    for (const Option &option : options) {
        Row row;
        row.option = option;
        mRows.push_back(std::move(row));
    }
    mNeedsLayout = true;
}

void PopupMenu::layout() {
    mNeedsLayout = false;
    if (mRows.empty()) {
        mPopupWidth = 0.0f;
        mPopupHeight = 0.0f;
        mTexture->setSize(0, 0);
        return;
    }

    // The widest title decides the width, with a floor so a popup of short
    // words is not a sliver.
    float contentWidth = scaled(ICON_TITLE_MIN_WIDTH);
    for (const Row &row : mRows) {
        int titleWidth = 0;
        Canvas::measureText(row.option.title, scaled(FONT_SIZE), false, &titleWidth, nullptr);
        float width = scaled(ICON_SPAN) + (float)titleWidth + scaled(TITLE_TRAIL);
        contentWidth = std::max(contentWidth, width);
    }

    float rowHeight = scaled(ROW_HEIGHT);
    float top = scaled(PADDING_TOP);
    for (Row &row : mRows) {
        row.top = top;
        row.bottom = top + rowHeight;
        top = row.bottom;
    }

    mPopupWidth = scaled(PADDING_LEFT) + contentWidth + scaled(PADDING_RIGHT);
    // The bottom padding is where the triangle lives, which is why it is so
    // much deeper than the top.
    mPopupHeight = top + scaled(PADDING_BOTTOM);
    mTexture->setSize((int)(mPopupWidth + 0.5f), (int)(mPopupHeight + 0.5f));
    mTexture->setNeedsDraw();
}

void PopupMenu::showAtPoint(float pointX, float pointY, float outerWidth, float outerHeight) {
    (void)outerHeight;
    ensureArt();
    if (mNeedsLayout) {
        layout();
    }
    if (mRows.empty()) {
        return;
    }

    // Centred over the point, then pushed back inside the window.
    float halfWidth = mPopupWidth * 0.5f;
    float x = pointX - halfWidth;
    float clampedX = std::min(std::max(x, 0.0f), std::max(0.0f, outerWidth - mPopupWidth));
    mPopupX = clampedX;
    mPopupY = pointY + scaled(POPUP_Y_OFFSET) - mPopupHeight;

    // The triangle stays under the point even after the popup has been pushed
    // back inside, which is what keeps it pointing at the button.
    float triangleHalf = mTriangle.valid() ? ((float)mTriangle.width() * 0.5f) : 0.0f;
    float margin = scaled(POPUP_TRIANGLE_X_MARGIN);
    float triangleX = halfWidth + (x - clampedX) - triangleHalf;
    mTriangleX = std::min(std::max(triangleX, margin), std::max(margin, mPopupWidth - margin * 2.0f));
    mTexture->setNeedsDraw();

    mShow = true;
    setHidden(false);
    mShowAnim.setValue(0.0f);
    mShowAnim.animateValue(1.0f, OPEN_SECONDS, SDL_GetTicks());
}

void PopupMenu::close(bool fadeOut) {
    if (!mShow) {
        return;
    }
    if (fadeOut) {
        mShowAnim.animateValue(0.0f, CLOSE_SECONDS, SDL_GetTicks());
    } else {
        mShowAnim.setValue(0.0f);
    }
    mShow = false;
    setSelectedItem(-1);
}

void PopupMenu::generate(RenderView *view, RenderLists &lists) {
    (void)view;
    lists.updateList.push_back(this);
    lists.blendedList.push_back(this);
    lists.hitTestList.push_back(this);
}

bool PopupMenu::update(RenderView *view, float frameInterval) {
    (void)frameInterval;
    // While it is open the layer rect is the whole window, so a press anywhere
    // reaches onTouchEvent and can close it.
    if (!mHidden) {
        setPosition(0.0f, 0.0f);
        setSize((float)view->getWidth(), (float)view->getHeight());
    }
    return mShowAnim.getTimeRemaining(SDL_GetTicks()) > 0.0f;
}

Bitmap PopupMenu::compose() {
    if (mNeedsLayout) {
        layout();
    }
    // load() ignores the render view, so there is nothing to pass it.
    return mTexture->load(nullptr);
}

void PopupMenu::renderBlended(RenderView *view) {
    float showRatio = mShowAnim.getValue(SDL_GetTicks());
    if (showRatio < 0.003f && !mShow) {
        setHidden(true);
        return;
    }
    if (mNeedsLayout) {
        layout();
    }
    if (mRows.empty() || mTexture->getCanvasWidth() <= 0) {
        return;
    }
    view->loadTexture(mTexture);
    if (!mTexture->isLoaded()) {
        return;
    }

    // The original ran the open through a wipe shader. This does the part that
    // reads: it grows past its size and settles back, and fades as it goes.
    float scale = 1.0f;
    const float split = 0.7f;
    if (mShow && showRatio < 1.0f) {
        scale = (showRatio < split) ? (0.8f + 0.3f * showRatio / split)
                                    : (1.0f + ((1.0f - showRatio) / (1.0f - split)) * 0.1f);
    }
    float width = mPopupWidth * scale;
    float height = mPopupHeight * scale;
    // Grows about the point it is anchored to, which is the bottom middle.
    float x = mPopupX + (mPopupWidth - width) * 0.5f;
    float y = mPopupY + (mPopupHeight - height);

    float previousAlpha = view->getAlpha();
    if (showRatio < 1.0f) {
        view->setAlpha(previousAlpha * showRatio);
    }
    view->blendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    view->draw2D(mTexture, x, y, width, height);
    if (showRatio < 1.0f) {
        view->setAlpha(previousAlpha);
    }
}

void PopupMenu::PopupTexture::renderCanvas(Bitmap &canvas, int width, int height) {
    mOwner->ensureArt();

    int bodyHeight = height - (int)scaled(POPUP_TRIANGLE_EXTRA_HEIGHT);
    if (mOwner->mBackground.valid()) {
        Canvas::blitNinePatch(canvas, mOwner->mBackground, 0, 0, width, bodyHeight);
    } else {
        Canvas::fillRect(canvas, 0, 0, width, bodyHeight, 0.0f, 0.0f, 0.0f, 0.85f);
    }
    if (mOwner->mTriangle.valid()) {
        // Stamped, not blended. Its opaque top rows have to replace the panel's
        // bottom border so the outline runs down into the point instead of
        // straight across behind it.
        Canvas::stamp(canvas, mOwner->mTriangle, (int)mOwner->mTriangleX,
                      height - mOwner->mTriangle.height() - 1);
    }

    int left = (int)scaled(PADDING_LEFT);
    int contentRight = width - (int)scaled(PADDING_RIGHT);
    int iconSize = (int)scaled(ICON_SIZE);
    int iconLeft = left + (int)((scaled(ICON_SPAN) - (float)iconSize) * 0.5f);
    int titleLeft = left + (int)scaled(ICON_SPAN);
    float fontSize = scaled(FONT_SIZE);

    for (size_t i = 0; i < mOwner->mRows.size(); ++i) {
        const Row &row = mOwner->mRows[i];
        int top = (int)row.top;
        int rowHeight = (int)(row.bottom - row.top);

        if ((int)i == mOwner->mSelectedItem && mOwner->mHighlight.valid()) {
            Canvas::blitNinePatch(canvas, mOwner->mHighlight, left, top, contentRight - left, rowHeight);
        }

        if (!row.option.icon.empty()) {
            Bitmap icon = Bitmap::load(App::drawablePath(row.option.icon), 0);
            if (icon.valid()) {
                Canvas::blitScaled(canvas, icon, iconLeft, top + (rowHeight - iconSize) / 2, iconSize, iconSize);
            }
        }

        int titleWidth = 0;
        int titleHeight = 0;
        Canvas::measureText(row.option.title, fontSize, false, &titleWidth, &titleHeight);
        Canvas::drawText(canvas, row.option.title, titleLeft, top + (rowHeight - titleHeight) / 2, fontSize, false,
                         1.0f, 1.0f, 1.0f, 1.0f, 0);
    }
}

int PopupMenu::hitTestOptions(float x, float y) const {
    float localX = x - mPopupX;
    float localY = y - mPopupY;
    if (mRows.empty() || localX < 0.0f || localX >= mPopupWidth || localY < 0.0f) {
        return -1;
    }
    for (size_t i = 0; i < mRows.size(); ++i) {
        if (localY < mRows[i].bottom) {
            // Above the first row's top is still the first row, as in the
            // original: the top padding belongs to it.
            return (int)i;
        }
    }
    return -1;
}

void PopupMenu::setSelectedItem(int index) {
    if (mSelectedItem == index) {
        return;
    }
    mSelectedItem = index;
    mTexture->setNeedsDraw();
}

bool PopupMenu::onTouchEvent(const MotionEvent &event) {
    if (!mShow) {
        return false;
    }
    int hit = hitTestOptions(event.getX(), event.getY());

    switch (event.getAction()) {
    case MotionEvent::ACTION_DOWN:
    case MotionEvent::ACTION_MOVE:
        setSelectedItem(hit);
        break;
    case MotionEvent::ACTION_UP:
        if (hit >= 0 && hit == mSelectedItem && hit < (int)mRows.size()) {
            // Copied and the popup closed before it runs: an action can rebuild
            // the bar this popup hangs off, and with it the options.
            Action action = mRows[(size_t)hit].option.action;
            close(true);
            if (action) {
                action();
            }
            return true;
        }
        // A press that ends outside every row dismisses it, which is what makes
        // the whole window the layer rect worth doing.
        close(true);
        break;
    case MotionEvent::ACTION_CANCEL:
        setSelectedItem(-1);
        break;
    default:
        break;
    }
    return true;
}

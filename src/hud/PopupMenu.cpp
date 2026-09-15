#include "hud/PopupMenu.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

#include "app/App.h"
#include "graphics/DrawableLoad.h"

namespace {

// Java popup layout units.
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
// The span the point is centred in, in layout units.
const float TRIANGLE_WIDTH = 43.0f;
// How far the texture reaches below the panel body, for the point.
const float POPUP_TRIANGLE_EXTRA_HEIGHT = 14.0f;

// The panel and its point, in layout units, as art/popup.9.svg and
// art/popup_triangle_bottom.svg draw them: smoked glass in a white border,
// with a shadow cast downward. They are drawn here as one shape, so the border
// runs into the point and the shadow falls under both without a seam at any
// density.
const float BOX_LEFT = 8.3f;
const float BOX_TOP = 4.45f;
const float BOX_RIGHT = 8.1f;
// Up from the bottom of the panel body.
const float BOX_BOTTOM = 12.9f;
const float BOX_RADIUS = 5.5f;
const float BORDER_WIDTH = 2.0f;
// The point's half width where its sides leave the border's middle, and how
// far below that its tip is.
const float POINT_HALF_WIDTH = 15.64f;
const float POINT_DEPTH = 14.55f;
const float GLASS_GREY = 38.0f / 255.0f;
const float GLASS_ALPHA = 0.9f;
const float SHADOW_OFFSET = 4.0f;
const float SHADOW_SIGMA = 3.4f;
const float SHADOW_ALPHA = 0.78f;

const float OPEN_SECONDS = 0.4f;
const float CLOSE_SECONDS = 0.3f;

// In whole pixels, rounded as the art's sizes are, so the icons and the
// triangle are drawn at exactly the size they were rendered at, and the panel
// is a whole number of pixels wide.
float scaled(float value) {
    return (float)App::uiPixels(value);
}

// The middle of the border, as a signed distance in pixels: negative inside.
// A rounded rectangle joined to a triangle whose top is sunk into it, so the
// border has no line across the point's opening.
struct Outline {
    float left, top, right, bottom, radius;
    float pointX[3];
    float pointY[3];

    float distance(float x, float y) const {
        // The rounded rectangle.
        const float halfWidth = (right - left) * 0.5f;
        const float halfHeight = (bottom - top) * 0.5f;
        const float qx = std::fabs(x - (left + halfWidth)) - halfWidth + radius;
        const float qy = std::fabs(y - (top + halfHeight)) - halfHeight + radius;
        const float ox = std::max(qx, 0.0f);
        const float oy = std::max(qy, 0.0f);
        const float box = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;

        // The triangle, as the furthest of its three edges. That keeps its
        // corners sharp, as the art's mitred strokes are.
        float point = -1e9f;
        for (int i = 0; i < 3; ++i) {
            const int j = (i + 1) % 3;
            const int k = (i + 2) % 3;
            float nx = pointY[j] - pointY[i];
            float ny = pointX[i] - pointX[j];
            const float length = std::sqrt(nx * nx + ny * ny);
            nx /= length;
            ny /= length;
            // Outward: away from the corner the edge does not touch.
            if (nx * (pointX[k] - pointX[i]) + ny * (pointY[k] - pointY[i]) > 0.0f) {
                nx = -nx;
                ny = -ny;
            }
            point = std::max(point, nx * (x - pointX[i]) + ny * (y - pointY[i]));
        }
        return std::min(box, point);
    }
};

// The radius of two box passes whose blur is closest to a gaussian of sigma.
int boxRadiusFor(float sigma) {
    // Two passes of width 2r + 1 have a variance of 2(r^2 + r) / 3.
    const float radius = (-1.0f + std::sqrt(1.0f + 6.0f * sigma * sigma)) * 0.5f;
    return std::max(1, (int)(radius + 0.5f));
}

// Draws the panel, its point and their shadow over canvas. pointCenter is the
// x of the tip, and bodyHeight the bottom of the panel body.
void drawPanel(Bitmap &canvas, int bodyHeight, float pointCenter) {
    const float unit = App::UI_DENSITY;
    const float halfBorder = BORDER_WIDTH * unit * 0.5f;
    const int width = canvas.width();
    const int height = canvas.height();

    Outline outline;
    outline.left = BOX_LEFT * unit + halfBorder;
    outline.top = BOX_TOP * unit + halfBorder;
    outline.right = (float)width - BOX_RIGHT * unit - halfBorder;
    outline.bottom = (float)bodyHeight - BOX_BOTTOM * unit - halfBorder;
    outline.radius = BOX_RADIUS * unit - halfBorder;
    // The sides carry on up into the box, past the border and its edge
    // blending, so the two shapes share no edge.
    const float sink = BORDER_WIDTH * unit * 2.0f;
    const float depth = POINT_DEPTH * unit;
    const float topHalfWidth = POINT_HALF_WIDTH * unit * (depth + sink) / depth;
    outline.pointX[0] = pointCenter - topHalfWidth;
    outline.pointY[0] = outline.bottom - sink;
    outline.pointX[1] = pointCenter + topHalfWidth;
    outline.pointY[1] = outline.bottom - sink;
    outline.pointX[2] = pointCenter;
    outline.pointY[2] = outline.bottom + depth;

    // Pixel coverage of the shape out to the border's outer edge, measured at
    // the pixel's centre.
    auto coverage = [&outline, halfBorder](float x, float y) {
        return std::min(1.0f, std::max(0.0f, halfBorder - outline.distance(x, y) + 0.5f));
    };

    const float shadowOffset = SHADOW_OFFSET * unit;
    Bitmap cast(width, height);
    uint8_t *castPixels = cast.pixels();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            castPixels[((size_t)y * (size_t)width + (size_t)x) * 4 + 3] =
                (uint8_t)(coverage((float)x + 0.5f, (float)y + 0.5f - shadowOffset) * 255.0f + 0.5f);
        }
    }
    const int radius = boxRadiusFor(SHADOW_SIGMA * unit);
    const Bitmap shadow = Canvas::blurredCoverage(cast, radius);
    const int pad = Canvas::blurPadding(radius);

    uint8_t *pixels = canvas.pixels();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float px = (float)x + 0.5f;
            const float py = (float)y + 0.5f;
            const float distance = outline.distance(px, py);
            const float inside = std::min(1.0f, std::max(0.0f, halfBorder - distance + 0.5f));
            const float border = std::min(1.0f, std::max(0.0f, halfBorder - std::fabs(distance) + 0.5f));
            const float blurred =
                shadow.valid() ? shadow.pixels()[((size_t)(y + pad) * (size_t)shadow.width() + (size_t)(x + pad)) * 4 + 3] / 255.0f
                               : 0.0f;
            // The shadow only shows outside the shape, as the art masks it.
            const float shadowAlpha = blurred * SHADOW_ALPHA * (1.0f - inside);
            const float glassAlpha = inside * GLASS_ALPHA;

            // Premultiplied grey, layered shadow, glass, border. Every layer is
            // grey, so the channel order does not matter.
            uint8_t *pixel = pixels + ((size_t)y * (size_t)width + (size_t)x) * 4;
            float value = pixel[0] / 255.0f;
            float alpha = pixel[3] / 255.0f;
            value = value * (1.0f - shadowAlpha);
            alpha = shadowAlpha + alpha * (1.0f - shadowAlpha);
            value = GLASS_GREY * glassAlpha + value * (1.0f - glassAlpha);
            alpha = glassAlpha + alpha * (1.0f - glassAlpha);
            value = border + value * (1.0f - border);
            alpha = border + alpha * (1.0f - border);
            const uint8_t grey = (uint8_t)(std::min(1.0f, value) * 255.0f + 0.5f);
            pixel[0] = grey;
            pixel[1] = grey;
            pixel[2] = grey;
            pixel[3] = (uint8_t)(std::min(1.0f, alpha) * 255.0f + 0.5f);
        }
    }
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
    mHighlight = Canvas::loadNinePatch("popup_option_selected.9");
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

void PopupMenu::showAtPoint(float pointX, float pointY, float boundsLeft, float boundsWidth) {
    ensureArt();
    if (mNeedsLayout) {
        layout();
    }
    if (mRows.empty()) {
        return;
    }

    // Centred over the point, then pushed back inside the band.
    float halfWidth = mPopupWidth * 0.5f;
    float x = pointX - halfWidth;
    float clampedX = std::min(std::max(x, boundsLeft), std::max(boundsLeft, boundsLeft + boundsWidth - mPopupWidth));
    mPopupX = clampedX;
    mPopupY = pointY + scaled(POPUP_Y_OFFSET) - mPopupHeight;

    // The triangle stays under the point even after the popup has been pushed
    // back inside, which is what keeps it pointing at the button.
    float triangleHalf = scaled(TRIANGLE_WIDTH) * 0.5f;
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

    // Opening animation overshoots, settles, and fades in.
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
    drawPanel(canvas, bodyHeight, mOwner->mTriangleX + scaled(TRIANGLE_WIDTH) * 0.5f);

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
            Bitmap icon = DrawableLoad::load(row.option.icon).bitmap;
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

bool PopupMenu::rowCenter(size_t index, float *x, float *y) const {
    if (!mShow || index >= mRows.size()) {
        return false;
    }
    const float top = (index == 0) ? 0.0f : mRows[index - 1].bottom;
    *x = mPopupX + mPopupWidth * 0.5f;
    *y = mPopupY + (top + mRows[index].bottom) * 0.5f;
    return true;
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
        // Dismiss when a press ends outside all rows.
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

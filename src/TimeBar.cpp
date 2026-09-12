#include "TimeBar.h"

#include "Dates.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "App.h"
#include "FloatUtils.h"
#include "GridLayer.h"
#include "MediaFeed.h"
#include "MediaItem.h"
#include "MediaSet.h"

namespace {

const float MARKER_SPACING_PIXELS = 50.0f;
const float AUTO_SCROLL_MARGIN = 100.0f;
const float FONT_SIZE = 17.0f;
// The horizontal and vertical room the popup adds around the date.
const float POPUP_PAD_X = 70.0f;
const float POPUP_PAD_Y = 20.0f;

const char *const MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

float scaled(float value) {
    return value * App::UI_DENSITY;
}

}  // namespace

TimeBar::TimeBar() {
    mPopup = std::make_shared<PopupTexture>(this);
}

TimeBar::~TimeBar() = default;

void TimeBar::onSizeChanged() {
    mScroll = getScrollForPosition(mPosition);
}

void TimeBar::setFeed(MediaFeed *feed, int state, bool needsLayout) {
    mFeed = feed;
    mState = state;
    layout();
    if (needsLayout) {
        mPosition = 0.0f;
        mScroll = getScrollForPosition(mPosition);
    }
}

MediaItem *TimeBar::getItem() const {
    size_t numMarkers = mMarkers.size();
    if (numMarkers == 0) {
        return nullptr;
    }
    size_t index = (size_t)(mPosition * (float)numMarkers);
    if (index >= numMarkers) {
        index = numMarkers - 1;
    }
    const Marker &marker = mMarkers[index];
    size_t numItems = marker.items.size();
    if (numItems == 0) {
        return nullptr;
    }
    // Where inside this marker's own span the position falls, which is what
    // picks one of the items it covers.
    float deltaBetweenMarkers = 1.0f / (float)numMarkers;
    float increment = mPosition - (float)index * deltaBetweenMarkers;
    size_t itemIndex = (size_t)((float)numItems * increment / deltaBetweenMarkers);
    if (itemIndex >= numItems) {
        itemIndex = numItems - 1;
    }
    return marker.items[itemIndex];
}

const TimeBar::Marker *TimeBar::getAnchorMarker() const {
    size_t numMarkers = mMarkers.size();
    if (numMarkers == 0) {
        return nullptr;
    }
    size_t index = (size_t)(mPosition * (float)numMarkers);
    if (index >= numMarkers) {
        index = numMarkers - 1;
    }
    return &mMarkers[index];
}

void TimeBar::setItem(MediaItem *item) {
    auto found = mTracker.find(item);
    if (found == mTracker.end()) {
        return;
    }
    float markerX = (mTotalWidth == 0.0f) ? 0.0f : mMarkers[found->second].x / mTotalWidth;
    mPosition = std::max(0.0f, std::min(1.0f, markerX));
    mScroll = getScrollForPosition(mPosition);
}

float TimeBar::addMarker(const Marker &marker) {
    mMarkers.push_back(marker);
    return marker.x + scaled(MARKER_SPACING_PIXELS);
}

void TimeBar::layout() {
    if (mFeed == nullptr) {
        return;
    }
    mTracker.clear();
    mMarkers.clear();

    float scrollX = mScroll;
    int lastYear = -1;
    int lastMonth = -1;
    int lastDayBlock = -1;
    float dx = 0.0f;
    int increment = 12;
    mShowTime = true;

    // Inside an album every item gets a look in. Over the stacks there is one
    // item per set, so the markers are much coarser.
    MediaSet *set = nullptr;
    MediaSet slotCover;
    if (mState == GridLayer::STATE_GRID_VIEW) {
        set = mFeed->getFilteredSet();
        if (set == nullptr) {
            set = mFeed->getCurrentSet();
        }
    } else {
        increment = 2;
        if (!mFeed->hasExpandedMediaSet()) {
            mShowTime = false;
        }
        int numSlots = mFeed->getNumSlots();
        for (int i = 0; i < numSlots; ++i) {
            MediaSet *slotSet = mFeed->getSetForSlot(i);
            if (slotSet != nullptr && slotSet->getNumItems() > 0) {
                // A reference, not a copy: the album still owns the item.
                slotCover.addItemRef(slotSet->getItems()[0]);
            }
        }
        set = &slotCover;
    }

    if (set != nullptr) {
        // Copied, because building markers walks it while the loader thread may
        // still be adding to the live one.
        std::vector<MediaItem *> items = set->getItems();
        size_t j = 0;
        while (j < items.size()) {
            MediaItem *item = items[j];
            if (item == nullptr) {
                break;
            }
            // Use date arithmetic for pre-1970 values that Windows localtime rejects.
            const Dates::Civil date = Dates::civilFromMs(item->mDateTakenInMs);
            int year = date.year;
            int month = date.month;
            int dayBlock = date.day;

            if (year != lastYear) {
                lastYear = year;
                lastMonth = -1;
                lastDayBlock = -1;
            }

            Marker marker;
            marker.x = dx;
            marker.year = year;
            marker.month = month;
            marker.day = dayBlock;
            if (month != lastMonth) {
                lastMonth = month;
                lastDayBlock = -1;
                marker.type = Marker::TYPE_MONTH;
            } else if (dayBlock != lastDayBlock) {
                lastDayBlock = dayBlock;
                marker.type = Marker::TYPE_DAY;
            } else {
                marker.type = Marker::TYPE_DOT;
            }
            dx = addMarker(marker);

            // The marker covers the next `increment` items, and each of them
            // points back at it so setItem can find the knob position.
            size_t markerIndex = mMarkers.size() - 1;
            for (int k = 0; k < increment; ++k) {
                size_t index = j + (size_t)k;
                if (index >= items.size()) {
                    break;
                }
                if (index == items.size() - 1 && k != 0) {
                    break;
                }
                mMarkers[markerIndex].items.push_back(items[index]);
                mTracker[items[index]] = markerIndex;
            }
            if (j == items.size() - 1) {
                break;
            }
            j += (size_t)increment;
            if (j >= items.size() - 1) {
                j = items.size() - 1;
            }
        }
        mTotalWidth = dx - scaled(MARKER_SPACING_PIXELS);
    }

    mPosition = getPositionForScroll(scrollX);
    mPositionAnim = mPosition;
}

float TimeBar::getScrollForPosition(float position) const {
    // Maps position onto a scroll that keeps the knob in the middle of the bar.
    float halfWidth = mWidth * 0.5f;
    float positionInv = 1.0f - position;
    return positionInv * -halfWidth + position * (mTotalWidth - halfWidth);
}

float TimeBar::getPositionForScroll(float scroll) const {
    if (mTotalWidth == 0.0f) {
        return 0.0f;
    }
    return (scroll + mWidth * 0.5f) / mTotalWidth;
}

float TimeBar::getKnobXForPosition(float position) const {
    return position * mTotalWidth;
}

float TimeBar::getPositionForKnobX(float knobX) const {
    float normKnobX = (mTotalWidth == 0.0f) ? 0.0f : knobX / mTotalWidth;
    return std::max(0.0f, std::min(1.0f, normKnobX));
}

bool TimeBar::update(RenderView *view, float frameInterval) {
    (void)view;
    float ratio = std::min(1.0f, 10.0f * frameInterval);
    float invRatio = 1.0f - ratio;
    mPositionAnim = ratio * mPosition + invRatio * mPositionAnim;
    mScrollAnim = ratio * mScroll + invRatio * mScrollAnim;

    if (mInDrag) {
        // Drag towards either end and the bar keeps scrolling on its own, so a
        // long album is reachable without letting go.
        float x = getKnobXForPosition(mPosition) - mScrollAnim;
        float margin = scaled(AUTO_SCROLL_MARGIN);
        float velocity = 0.0f;
        if (x < margin) {
            velocity = -std::pow(1.0f - x / margin, 2.0f);
        } else if (x > mWidth - margin) {
            velocity = std::pow(1.0f - (mWidth - x) / margin, 2.0f);
        }
        mScroll += velocity * 400.0f * frameInterval;
        mPosition = getPositionForKnobX(mDragX + mScroll);
        mTextAlpha = 1.0f;
    } else {
        mTextAlpha = 0.0f;
    }
    mAnimTextAlpha = FloatUtils::animate(mAnimTextAlpha, mTextAlpha, frameInterval);
    return mAnimTextAlpha != mTextAlpha;
}

void TimeBar::updatePopup() {
    const Marker *anchor = getAnchorMarker();
    std::string text;
    if (anchor == nullptr || anchor->year <= 1970) {
        // Before the epoch means no date was ever read off the file.
        text = "Date unknown";
    } else {
        text = std::string(MONTHS[anchor->month]) + " " + std::to_string(anchor->day) + " " +
               std::to_string(anchor->year);
    }
    if (text == mPopupText && mPopup->getCanvasWidth() > 0) {
        return;
    }
    mPopupText = text;
    int textWidth = 0;
    int textHeight = 0;
    if (!Canvas::measureText(mPopupText, scaled(FONT_SIZE), true, &textWidth, &textHeight)) {
        return;
    }
    mPopupTextWidth = textWidth;
    mPopupTextHeight = textHeight;
    mPopup->setSize(textWidth + (int)scaled(POPUP_PAD_X), textHeight + (int)scaled(POPUP_PAD_Y));
    mPopup->setNeedsDraw();
}

void TimeBar::PopupTexture::renderCanvas(Bitmap &canvas, int width, int height) {
    if (!mOwner->mPopupBackgroundLoaded) {
        mOwner->mPopupBackground = Canvas::loadNinePatch("popup.9");
        mOwner->mPopupBackgroundLoaded = true;
    }
    if (mOwner->mPopupBackground.valid()) {
        Canvas::blitNinePatch(canvas, mOwner->mPopupBackground, 0, 0, width, height);
    } else {
        Canvas::fillRect(canvas, 0, 0, width, height, 0.0f, 0.0f, 0.0f, 0.75f);
    }
    // Align labels to the art's padding: half horizontally, one quarter vertically.
    int x = (int)(scaled(POPUP_PAD_X) * 0.5f);
    int y = (int)(scaled(POPUP_PAD_Y) * 0.25f);
    Canvas::drawText(canvas, mOwner->mPopupText, x, y, scaled(FONT_SIZE), true, 1.0f, 1.0f, 1.0f, 1.0f, 0);
}

void TimeBar::generate(RenderView *view, RenderLists &lists) {
    (void)view;
    lists.updateList.push_back(this);
    lists.blendedList.push_back(this);
    lists.hitTestList.push_back(this);
}

void TimeBar::renderBlended(RenderView *view) {
    TexturePtr knob = view->getResource(mInDrag ? "scroller_pressed_new" : "scroller_new");
    if (!view->bind(knob)) {
        return;
    }
    float knobWidth = (float)knob->getWidth();
    float knobHeight = (float)knob->getHeight();
    float knobX = mX - mScrollAnim + getKnobXForPosition(mPositionAnim) - knobWidth * 0.5f;
    // Without a date the knob rides the bar; with a date it drops to the bottom
    // edge. That edge is the safe area's, not the window's: on a phone the
    // window runs under the navigation bar, and a knob at the window's own
    // bottom sits behind it where it cannot be dragged.
    float knobY = mShowTime ? ((float)view->getHeight() - App::SAFE_AREA.bottom - knobHeight) : mY;
    view->draw2D(knobX, knobY, 0.0f, knobWidth, knobHeight);

    if (!mShowTime || (!mInDrag && mAnimTextAlpha == 0.0f)) {
        return;
    }
    updatePopup();
    view->loadTexture(mPopup);
    if (!mPopup->isLoaded()) {
        return;
    }
    float popupWidth = (float)mPopup->getCanvasWidth();
    float popupHeight = (float)mPopup->getCanvasHeight();
    // Composed with whatever alpha the HUD is at, so the popup fades with the
    // rest of the chrome as well as with the drag.
    float hudAlpha = view->getAlpha();
    view->setAlpha(hudAlpha * mAnimTextAlpha);
    view->draw2D(mPopup, ((float)view->getWidth() - popupWidth) * 0.5f,
                 ((float)view->getHeight() - scaled(10.0f)) * 0.5f, popupWidth, popupHeight);
    view->setAlpha(hudAlpha);
}

bool TimeBar::onTouchEvent(const MotionEvent &event) {
    mDragX = event.getX();
    mPosition = getPositionForKnobX(mDragX + mScroll);

    if (mListener != nullptr) {
        mListener->onTimeChanged(this);
    }

    switch (event.getAction()) {
    case MotionEvent::ACTION_DOWN:
        mInDrag = true;
        break;
    case MotionEvent::ACTION_UP:
    case MotionEvent::ACTION_CANCEL:
        mInDrag = false;
        // Snap to the nearest marker, which also recentres the scroll.
        setItem(getItem());
        break;
    default:
        break;
    }
    return true;
}

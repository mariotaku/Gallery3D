// Port of com.cooliris.media.TimeBar: album date scrubber with a drag popup.
// Invisible markers map drag positions to groups of items; only knob and popup are drawn.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Canvas.h"
#include "CanvasTexture.h"
#include "Layer.h"
#include "RenderView.h"

class MediaFeed;
class MediaItem;
class MediaSet;

class TimeBar : public Layer {
  public:
    static const int HEIGHT = 48;

    // The date the popup shows over the scroll knob. Static and given the
    // parts, so what it reads for any date can be checked on its own.
    static std::string popupTextFor(int year, int month, int day);

    class Listener {
      public:
        virtual ~Listener() = default;
        virtual void onTimeChanged(TimeBar *timebar) = 0;
    };

    TimeBar();
    ~TimeBar() override;

    void setListener(Listener *listener) {
        mListener = listener;
    }

    void setFeed(MediaFeed *feed, int state, bool needsLayout);

    // The item under the knob.
    MediaItem *getItem() const;

    // Moves the knob to the item. The grid calls this every frame with whatever
    // is under the camera, which is what keeps the two in step.
    void setItem(MediaItem *item);

    bool isDragged() const {
        return mInDrag;
    }

    void generate(RenderView *view, RenderLists &lists) override;
    bool update(RenderView *view, float frameInterval) override;
    void renderBlended(RenderView *view) override;
    bool onTouchEvent(const MotionEvent &event) override;

  protected:
    void onSizeChanged() override;

  private:
    struct Marker {
        static const int TYPE_MONTH = 1;
        static const int TYPE_DAY = 2;
        static const int TYPE_DOT = 3;

        float x = 0.0f;
        int year = 0;
        int month = 0;
        int day = 0;
        int type = TYPE_DOT;
        std::vector<MediaItem *> items;
    };

    // Background and date composed into one texture.
    class PopupTexture : public CanvasTexture {
      public:
        explicit PopupTexture(TimeBar *owner) : mOwner(owner) {}

      protected:
        void renderCanvas(Bitmap &canvas, int width, int height) override;

      private:
        TimeBar *mOwner;
    };

    void layout();
    float addMarker(const Marker &marker);
    const Marker *getAnchorMarker() const;
    float getScrollForPosition(float position) const;
    float getPositionForScroll(float scroll) const;
    float getKnobXForPosition(float position) const;
    float getPositionForKnobX(float knobX) const;
    // Rebuilds the popup when the date under the knob changes.
    void updatePopup();

    Listener *mListener = nullptr;
    MediaFeed *mFeed = nullptr;
    int mState = 0;
    float mTotalWidth = 0.0f;
    float mPosition = 0.0f;
    float mPositionAnim = 0.0f;
    float mScroll = 0.0f;
    float mScrollAnim = 0.0f;
    bool mInDrag = false;
    float mDragX = 0.0f;
    bool mShowTime = true;
    float mTextAlpha = 0.0f;
    float mAnimTextAlpha = 0.0f;

    // Markers and item-to-marker index are render-thread-only.
    // GridLayer polls the feed's change flag before updating them.
    std::vector<Marker> mMarkers;
    std::unordered_map<const MediaItem *, size_t> mTracker;

    std::string mPopupText;
    int mPopupTextWidth = 0;
    int mPopupTextHeight = 0;
    std::shared_ptr<PopupTexture> mPopup;
    Canvas::NinePatch mPopupBackground;
    bool mPopupBackgroundLoaded = false;
};

// App-drawn minimise, maximise and close buttons using Windows metrics and colours.
// Glyphs are rasterised at the display scale.
#pragma once

#include <functional>

#include "graphics/Bitmap.h"
#include "graphics/CanvasTexture.h"
#include "graphics/Layer.h"
#include "graphics/RenderView.h"

class CaptionButtons : public Layer {
  public:
    CaptionButtons();
    ~CaptionButtons() override;

    // The texture holds a pointer to this instance, so copying would leave an invalid owner.
    CaptionButtons(const CaptionButtons &) = delete;
    CaptionButtons &operator=(const CaptionButtons &) = delete;

    // The owner supplies maximised state to select the restore glyph.
    void setMaximized(bool maximized);

    // Window actions supplied by the owner.
    void setActions(std::function<void()> minimize, std::function<void()> toggleMaximize,
                    std::function<void()> close);

    static float preferredWidth();
    static float preferredHeight();

    // Pointer position for hover feedback.
    void onPointerMoved(float x, float y);

    void generate(RenderView *view, RenderLists &lists) override;
    void renderBlended(RenderView *view) override;
    bool onTouchEvent(const MotionEvent &event) override;
    bool containsPoint(float x, float y) override;

    // Lays out and composes without GL.
    Bitmap compose();

  protected:
    void onSizeChanged() override;

  private:
    class ButtonsTexture : public CanvasTexture {
      public:
        explicit ButtonsTexture(CaptionButtons *owner) : mOwner(owner) {}

      protected:
        void renderCanvas(Bitmap &canvas, int width, int height) override;

      private:
        CaptionButtons *mOwner;
    };

    // Which button a point falls in, or -1.
    int buttonAt(float x, float y) const;
    void invalidate();

    std::shared_ptr<ButtonsTexture> mTexture;
    std::function<void()> mMinimize;
    std::function<void()> mToggleMaximize;
    std::function<void()> mClose;

    bool mMaximized = false;
    int mHovered = -1;
    int mPressed = -1;
    bool mNeedsDraw = true;
};

// The minimise, maximise and close buttons, drawn by the app.
//
// Taking the caption into the client area takes these with it, so they have to
// be drawn here. They follow the system's metrics rather than the app's own
// look: 46 by 32 at 100%, the glyphs Windows uses, and close going red under
// the pointer. A window button that sits where the eye expects it and lights up
// the way the others on screen do is worth more than one that matches the
// gallery's styling.
//
// The glyphs are drawn rather than loaded. Segoe Fluent Icons has them, but it
// is a Windows font and the shapes are a dash, a rectangle and a cross, which
// come out crisper drawn straight at whatever size the display asks for.
#pragma once

#include <functional>

#include "Bitmap.h"
#include "CanvasTexture.h"
#include "Layer.h"
#include "RenderView.h"

class CaptionButtons : public Layer {
  public:
    CaptionButtons();
    ~CaptionButtons() override;

    // Not copyable. The texture holds a pointer back here to read the hover and
    // press state while it draws, and a copy would leave that pointing at
    // whichever instance it was made from.
    CaptionButtons(const CaptionButtons &) = delete;
    CaptionButtons &operator=(const CaptionButtons &) = delete;

    // Told from outside, because the window is not this layer's to ask. The
    // maximise glyph becomes the restore one when the window is maximised.
    void setMaximized(bool maximized);

    // What a click runs. The layer has no window to act on, so the owner
    // supplies these.
    void setActions(std::function<void()> minimize, std::function<void()> toggleMaximize,
                    std::function<void()> close);

    static float preferredWidth();
    static float preferredHeight();

    // Where the pointer is, so a button can light before it is pressed. The
    // caption buttons are the only chrome here that answers to a bare hover,
    // which is what they do on every other window.
    void onPointerMoved(float x, float y);

    void generate(RenderView *view, RenderLists &lists) override;
    void renderBlended(RenderView *view) override;
    bool onTouchEvent(const MotionEvent &event) override;
    bool containsPoint(float x, float y) override;

    // Lays out and composes without touching GL, so a test can read the pixels
    // the screen gets.
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

// Entry point, replacing com.cooliris.media.Gallery.
//
// Usage: gallery3d [photo directory] [--also directory] [--scale N] [--bordered]
//        [--safe-area L,T,R,B]
// Defaults to the user's Pictures folder. --also shows a second directory
// alongside the first, through ConcatenatedDataSource. --bordered puts the
// system title bar back.
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "App.h"
#include "Canvas.h"
#include "ConcatenatedDataSource.h"
#include "GridLayer.h"
#include "GridLayoutInterface.h"
#include "Input.h"
#include "LocalDataSource.h"
#include "RenderView.h"
#include "Texture.h"
#include "gles2.h"

namespace {

std::string defaultPhotoDirectory() {
    const char *home = SDL_getenv("USERPROFILE");
    if (home == nullptr) {
        home = SDL_getenv("HOME");
    }
    if (home == nullptr) {
        return ".";
    }
    return std::string(home) + "/Pictures";
}

std::string assetRoot() {
    // Assets are copied next to the binary at build time.
    const char *base = SDL_GetBasePath();
    if (base == nullptr) {
        return "assets";
    }
    return std::string(base) + "assets";
}

int keyCodeFromSDL(SDL_Keycode key) {
    switch (key) {
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
        return KeyEvent::KEYCODE_BACK;
    case SDLK_LEFT:
        return KeyEvent::KEYCODE_DPAD_LEFT;
    case SDLK_RIGHT:
        return KeyEvent::KEYCODE_DPAD_RIGHT;
    case SDLK_UP:
        return KeyEvent::KEYCODE_DPAD_UP;
    case SDLK_DOWN:
        return KeyEvent::KEYCODE_DPAD_DOWN;
    case SDLK_RETURN:
    case SDLK_SPACE:
        return KeyEvent::KEYCODE_DPAD_CENTER;
    case SDLK_TAB:
        return KeyEvent::KEYCODE_MENU;
    default:
        return KeyEvent::KEYCODE_UNKNOWN;
    }
}

// Reads the framebuffer back and writes it out. Used by --screenshot so a
// build can be checked without a human at the keyboard.
// Returns false when the framebuffer has nothing in it yet, which happens if
// the readback beats the first composite. The caller then waits and retries
// rather than writing a blank png that looks like a rendering bug.
bool saveFramebuffer(int width, int height, const std::string &path) {
    std::vector<unsigned char> pixels((size_t)width * (size_t)height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    bool anyOpaque = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            anyOpaque = true;
            break;
        }
    }
    if (!anyOpaque) {
        return false;
    }

    // GL returns bottom up rows.
    std::vector<unsigned char> flipped(pixels.size());
    size_t stride = (size_t)width * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(&flipped[(size_t)y * stride], &pixels[(size_t)(height - 1 - y) * stride], stride);
    }
    SDL_Surface *surface =
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, flipped.data(), (int)stride);
    if (surface == nullptr) {
        return false;
    }
    IMG_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    SDL_Log("Wrote %s", path.c_str());
    return true;
}

// Where the window can be grabbed once it has no frame of its own.
//
// The blurred backdrop is the whole point of the wall, and a title bar sitting
// on top of it cuts the picture off. Without a frame the backdrop runs to the
// edge of the window, and this gives back the two things the frame was doing:
// a strip to drag by, and borders to resize from.
//
// Windows will not draw caption buttons for a frameless window, so there are
// none. Alt+F4 closes, and Escape still does from the album wall.
SDL_HitTestResult windowHitTest(SDL_Window *window, const SDL_Point *area, void *data) {
    (void)data;
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);

    // Wide enough to hit comfortably, and it follows the display scale so it
    // is the same physical size everywhere.
    const int border = (int)(6.0f * App::UI_DENSITY + 0.5f);
    const bool left = area->x < border;
    const bool right = area->x >= width - border;
    const bool top = area->y < border;
    const bool bottom = area->y >= height - border;

    if (top && left) {
        return SDL_HITTEST_RESIZE_TOPLEFT;
    }
    if (top && right) {
        return SDL_HITTEST_RESIZE_TOPRIGHT;
    }
    if (bottom && left) {
        return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    }
    if (bottom && right) {
        return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    }
    if (top) {
        return SDL_HITTEST_RESIZE_TOP;
    }
    if (bottom) {
        return SDL_HITTEST_RESIZE_BOTTOM;
    }
    if (left) {
        return SDL_HITTEST_RESIZE_LEFT;
    }
    if (right) {
        return SDL_HITTEST_RESIZE_RIGHT;
    }

    // A strip along the top to drag by, between the two things that already
    // live up there. A draggable region swallows the click, so it has to stop
    // short of the path bar on the left and the mode button on the right or
    // neither would be usable.
    const int safeLeft = (int)App::SAFE_AREA.left;
    const int safeTop = (int)App::SAFE_AREA.top;
    const int safeRight = width - (int)App::SAFE_AREA.right;
    const int captionHeight = safeTop + (int)(44.0f * App::UI_DENSITY + 0.5f);
    const int pathBarWidth = safeLeft + (int)(560.0f * App::UI_DENSITY + 0.5f);
    const int topRightWidth = (int)(100.0f * App::UI_DENSITY + 0.5f);
    if (area->y >= safeTop && area->y < captionHeight && area->x > pathBarWidth &&
        area->x < safeRight - topRightWidth) {
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

}  // namespace

int main(int argc, char **argv) {
    std::string photoDirectory;
    // A second library, shown after the first. Two sources behind one feed.
    std::string alsoDirectory;
    // The system title bar, off by default so the backdrop reaches the top.
    bool bordered = false;
    // Stands in for a notch and a home indicator. Desktops report no insets, so
    // without this the safe area layout is never exercised here.
    bool safeAreaOverridden = false;
    App::SafeAreaInsets safeAreaOverride;
    std::string screenshotPath;
    int screenshotFrames = 240;
    // Opens the given album part way through, so the grid view can be captured
    // without a hand on the mouse.
    int openSlot = -1;
    // The timeline is entered from the HUD menu, which is not ported yet, so
    // this is the only way to see it.
    bool timeline = false;
    // Fullscreen is reached by tapping a photo, so this is the headless way in.
    bool fullscreen = false;
    // Select mode is entered by long pressing a stack, so this is the headless
    // way in. It is also the only thing that exercises the checkmark drawing.
    bool select = false;
    // Both act on the selection, so both need --select. They exist because
    // there is no menu to invoke them from yet.
    bool rotate = false;
    bool deleteSelection = false;
    // Holds a drag on the time bar, which is the only thing that raises the
    // date popup. Needs --open, because the bar belongs to the album view. The
    // value is where along the bar to press, from 0 to 1; the middle is where
    // the knob already sits, so anything else also scrolls the wall.
    bool scrub = false;
    float scrubAt = 0.5f;
    // Zooms the fullscreen photo, which is the only thing that reaches for the
    // hi-res texture. Needs --fullscreen, and fires after it.
    bool zoom = false;
    // Taps a button on the bottom selection bar, so the popup it opens can be
    // captured. Needs --select. -1 for off.
    int popupButton = -1;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            screenshotFrames = std::atoi(argv[++i]);
        } else if (arg == "--open" && i + 1 < argc) {
            openSlot = std::atoi(argv[++i]);
        } else if (arg == "--timeline") {
            timeline = true;
        } else if (arg == "--fullscreen") {
            fullscreen = true;
        } else if (arg == "--select") {
            select = true;
        } else if (arg == "--rotate") {
            rotate = true;
        } else if (arg == "--delete") {
            deleteSelection = true;
        } else if (arg == "--popup" && i + 1 < argc) {
            popupButton = std::atoi(argv[++i]);
        } else if (arg == "--zoom") {
            zoom = true;
        } else if (arg == "--scrub") {
            scrub = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                scrubAt = (float)std::atof(argv[++i]);
            }
        } else if (arg == "--bordered") {
            bordered = true;
        } else if (arg == "--safe-area" && i + 1 < argc) {
            float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (SDL_sscanf(argv[++i], "%f,%f,%f,%f", &values[0], &values[1], &values[2], &values[3]) == 4) {
                safeAreaOverride.left = values[0];
                safeAreaOverride.top = values[1];
                safeAreaOverride.right = values[2];
                safeAreaOverride.bottom = values[3];
                safeAreaOverridden = true;
            }
        } else if (arg == "--also" && i + 1 < argc) {
            alsoDirectory = argv[++i];
        } else if (arg == "--scale" && i + 1 < argc) {
            float scale = (float)std::atof(argv[++i]);
            if (scale > 0.0f) {
                App::CONTENT_SCALE = scale;
            }
        } else if (photoDirectory.empty()) {
            photoDirectory = arg;
        }
    }
    if (photoDirectory.empty()) {
        photoDirectory = defaultPhotoDirectory();
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Ask for ES 2.0 first. Desktop drivers that refuse it still hand back a
    // context whose GL 2.0 core covers every call this port makes.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!bordered) {
        windowFlags |= SDL_WINDOW_BORDERLESS;
    }
    SDL_Window *window = SDL_CreateWindow("Gallery3D", 1280, 800, windowFlags);
    if (window == nullptr) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    bool realES = true;
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        SDL_Log("ES 2.0 context unavailable (%s), falling back to desktop GL", SDL_GetError());
        realES = false;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        context = SDL_GL_CreateContext(window);
    }
    if (context == nullptr) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return 1;
    }
    GLES2_SetRealES(realES);
    if (!GLES2_Load()) {
        SDL_Log("Failed to load GL entry points");
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    SDL_Log("GL_VERSION  : %s", (const char *)glGetString(GL_VERSION));
    SDL_Log("GL_RENDERER : %s", (const char *)glGetString(GL_RENDERER));

    App::ASSET_ROOT = assetRoot();
    // Two separate things, multiplied into the one knob the ported code reads.
    // The display scale is what SDL reports for the monitor: how many physical
    // pixels a logical pixel is worth, so text and assets stay crisp on HiDPI.
    // The content scale says how big the wall should be, because the ported
    // constants were picked for a 320x480 phone. Everything downstream keys off
    // App::PIXEL_DENSITY - grid item size below, slot spacing in
    // GridLayoutInterface, labels in DisplaySlot, quads in GridDrawables,
    // thumbnail resolution in Texture - so scaling it here scales the whole
    // wall coherently, and nothing else has to know.
    float displayScale = SDL_GetWindowDisplayScale(window);
    if (displayScale <= 0.0f) {
        displayScale = 1.0f;
    }
    App::PIXEL_DENSITY = displayScale * App::CONTENT_SCALE;
    // The chrome follows the display and not the wall, so a button is the size
    // the screen asks for rather than that times the wall's enlargement.
    App::UI_DENSITY = displayScale;

    // What the platform says is safe to put controls in. SDL reports the whole
    // client area on a desktop, so these come out zero and nothing moves; on a
    // phone it is the rect left over once the cutouts are taken off.
    {
        SDL_Rect safeRect;
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        if (SDL_GetWindowSafeArea(window, &safeRect)) {
            App::SAFE_AREA.left = (float)safeRect.x;
            App::SAFE_AREA.top = (float)safeRect.y;
            App::SAFE_AREA.right = (float)(windowWidth - (safeRect.x + safeRect.w));
            App::SAFE_AREA.bottom = (float)(windowHeight - (safeRect.y + safeRect.h));
        }
        if (safeAreaOverridden) {
            App::SAFE_AREA = safeAreaOverride;
        }
        if (App::SAFE_AREA.left != 0.0f || App::SAFE_AREA.top != 0.0f || App::SAFE_AREA.right != 0.0f ||
            App::SAFE_AREA.bottom != 0.0f) {
            SDL_Log("Safe area insets: left %.0f top %.0f right %.0f bottom %.0f", App::SAFE_AREA.left,
                    App::SAFE_AREA.top, App::SAFE_AREA.right, App::SAFE_AREA.bottom);
        }
    }

    if (!bordered) {
        // After the density is known, since the hit test regions follow it.
        SDL_SetWindowHitTest(window, windowHitTest, nullptr);
    }
    SDL_Log("PIXEL_DENSITY %.3f (display %.3f x content %.3f), UI_DENSITY %.3f, drawables from the %.1fx bucket",
            App::PIXEL_DENSITY, displayScale, App::CONTENT_SCALE, App::UI_DENSITY, App::drawableBucketDensity());
    Canvas::initFonts();

    RenderView renderView;
    if (!renderView.init(window)) {
        SDL_Log("RenderView init failed");
        return 1;
    }

    GridLayoutInterface layoutInterface(4);
    GridLayer gridLayer((int)(96.0f * App::PIXEL_DENSITY), (int)(72.0f * App::PIXEL_DENSITY), &layoutInterface,
                        &renderView);
    LocalDataSource dataSource(photoDirectory);
    // Held out here so they outlive the feed. Only used when --also was given.
    std::unique_ptr<LocalDataSource> alsoSource;
    std::unique_ptr<ConcatenatedDataSource> combinedSource;
    DataSource *feedSource = &dataSource;
    if (!alsoDirectory.empty()) {
        alsoSource = std::make_unique<LocalDataSource>(alsoDirectory);
        combinedSource = std::make_unique<ConcatenatedDataSource>(&dataSource, alsoSource.get());
        feedSource = combinedSource.get();
        SDL_Log("Also showing %s", alsoDirectory.c_str());
    }

    renderView.setRootLayer(&gridLayer);
    renderView.onSurfaceCreated();

    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    renderView.onSurfaceChanged(pixelWidth, pixelHeight);

    // A fullscreen photo is drawn about as wide as the window, so decode it to
    // at least that. Below the old 1024 there is nothing to gain, and the cap
    // keeps a very large window from turning every photo into a 4096 texture.
    {
        int longEdge = (pixelWidth > pixelHeight) ? pixelWidth : pixelHeight;
        App::SCREEN_NAIL_MAX_EDGE = std::min(2048, std::max(1024, longEdge));
        // Twice that when zoomed, which covers the fill-screen zoom without
        // trying to hold a whole 24 megapixel photo on the card.
        App::HI_RES_MAX_EDGE = std::min(4096, App::SCREEN_NAIL_MAX_EDGE * 2);
        SDL_Log("Screennail max edge %d, hi-res max edge %d", App::SCREEN_NAIL_MAX_EDGE, App::HI_RES_MAX_EDGE);
    }

    gridLayer.setDataSource(feedSource);
    SDL_Log("Scanning %s", photoDirectory.c_str());

    // Pointer state, turned into the MotionEvents the ported gesture code wants.
    // SDL reports pointer positions in window units; the renderer works in
    // pixels, so scale on the way in.
    bool mouseDown = false;
    MotionEvent event;

    // Live fingers, in the order they went down. The ported gesture code wants
    // Android's shape: one event carrying every pointer, with the action
    // saying which one changed. SDL reports each finger separately, so they
    // are tracked here and folded into one event.
    struct Finger {
        SDL_FingerID id;
        float x;
        float y;
    };
    std::vector<Finger> fingers;
    auto fingerIndex = [&fingers](SDL_FingerID id) -> int {
        for (size_t i = 0; i < fingers.size(); ++i) {
            if (fingers[i].id == id) {
                return (int)i;
            }
        }
        return -1;
    };
    // SDL gives finger positions normalised to the window, so scale to pixels.
    auto buildTouchEvent = [&fingers, &pixelWidth, &pixelHeight](int action, int actionIndex) {
        MotionEvent touch;
        touch.action = action;
        touch.actionIndex = actionIndex;
        touch.pointerCount = (int)std::min<size_t>(fingers.size(), 2);
        for (int i = 0; i < touch.pointerCount; ++i) {
            touch.xs[i] = fingers[(size_t)i].x * (float)pixelWidth;
            touch.ys[i] = fingers[(size_t)i].y * (float)pixelHeight;
        }
        touch.eventTime = SDL_GetTicks();
        return touch;
    };
    auto pointerScale = [&]() {
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        return (windowWidth > 0) ? ((float)pixelWidth / (float)windowWidth) : 1.0f;
    };

    int frameNumber = 0;
    // --frames is a count, but a swap that does not block turns that count into
    // no time at all. Requiring the wall clock to have passed as well keeps a
    // capture from outrunning the compositor and the texture loaders.
    const uint64_t startTicks = SDL_GetTicks();
    const uint64_t screenshotAfterMs = (uint64_t)screenshotFrames * 1000ull / 60ull;
    bool running = true;
    while (running) {
        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent)) {
            switch (sdlEvent.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
                renderView.onSurfaceChanged(pixelWidth, pixelHeight);
                renderView.requestRender();
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (sdlEvent.button.button == SDL_BUTTON_LEFT) {
                    mouseDown = true;
                    event = MotionEvent();
                    event.action = MotionEvent::ACTION_DOWN;
                    event.xs[0] = sdlEvent.button.x * pointerScale();
                    event.ys[0] = sdlEvent.button.y * pointerScale();
                    event.eventTime = SDL_GetTicks();
                    renderView.queueTouchEvent(event);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (sdlEvent.button.button == SDL_BUTTON_LEFT && mouseDown) {
                    mouseDown = false;
                    event.action = MotionEvent::ACTION_UP;
                    event.xs[0] = sdlEvent.button.x * pointerScale();
                    event.ys[0] = sdlEvent.button.y * pointerScale();
                    event.eventTime = SDL_GetTicks();
                    renderView.queueTouchEvent(event);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (mouseDown) {
                    event.action = MotionEvent::ACTION_MOVE;
                    event.xs[0] = sdlEvent.motion.x * pointerScale();
                    event.ys[0] = sdlEvent.motion.y * pointerScale();
                    event.eventTime = SDL_GetTicks();
                    renderView.queueTouchEvent(event);
                }
                break;
            case SDL_EVENT_FINGER_DOWN: {
                if (fingerIndex(sdlEvent.tfinger.fingerID) >= 0) {
                    break;
                }
                fingers.push_back({sdlEvent.tfinger.fingerID, sdlEvent.tfinger.x, sdlEvent.tfinger.y});
                int index = (int)fingers.size() - 1;
                // The first finger opens the gesture; later ones join it.
                renderView.queueTouchEvent(buildTouchEvent(
                    index == 0 ? MotionEvent::ACTION_DOWN : MotionEvent::ACTION_POINTER_DOWN, index));
                break;
            }
            case SDL_EVENT_FINGER_MOTION: {
                int index = fingerIndex(sdlEvent.tfinger.fingerID);
                if (index < 0) {
                    break;
                }
                fingers[(size_t)index].x = sdlEvent.tfinger.x;
                fingers[(size_t)index].y = sdlEvent.tfinger.y;
                renderView.queueTouchEvent(buildTouchEvent(MotionEvent::ACTION_MOVE, index));
                break;
            }
            case SDL_EVENT_FINGER_UP: {
                int index = fingerIndex(sdlEvent.tfinger.fingerID);
                if (index < 0) {
                    break;
                }
                // Build the event before dropping the finger, so the pointer
                // that lifted is still in it - that is what Android does and
                // what ScaleGestureDetector reads to end a pinch.
                bool last = fingers.size() == 1;
                MotionEvent touch =
                    buildTouchEvent(last ? MotionEvent::ACTION_UP : MotionEvent::ACTION_POINTER_UP, index);
                fingers.erase(fingers.begin() + index);
                renderView.queueTouchEvent(touch);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                // The wheel drives the pinch: it spreads a stack in the album
                // view and zooms a photo in fullscreen.
                float mouseX = 0.0f;
                float mouseY = 0.0f;
                SDL_GetMouseState(&mouseX, &mouseY);
                float scale = pointerScale();
                gridLayer.getInputProcessor()->onWheel(mouseX * scale, mouseY * scale, sdlEvent.wheel.y);
                renderView.requestRender();
                break;
            }
            case SDL_EVENT_KEY_DOWN: {
                int keyCode = keyCodeFromSDL(sdlEvent.key.key);
                if (keyCode == KeyEvent::KEYCODE_BACK && sdlEvent.key.key == SDLK_ESCAPE &&
                    gridLayer.getState() == GridLayer::STATE_MEDIA_SETS) {
                    running = false;
                    break;
                }
                if (keyCode != KeyEvent::KEYCODE_UNKNOWN) {
                    KeyEvent keyEvent;
                    keyEvent.action = KeyEvent::ACTION_DOWN;
                    keyEvent.keyCode = keyCode;
                    renderView.dispatchKeyDown(keyCode, keyEvent);
                }
                break;
            }
            default:
                break;
            }
        }

        renderView.onDrawFrame();
        SDL_GL_SwapWindow(window);

        ++frameNumber;
        if (openSlot >= 0 && frameNumber == screenshotFrames / 2) {
            gridLayer.tapGesture(openSlot, false);
        }
        if (timeline && frameNumber == (screenshotFrames * 3) / 4) {
            gridLayer.setState(GridLayer::STATE_TIMELINE);
        }
        if (fullscreen && frameNumber == (screenshotFrames * 3) / 4) {
            gridLayer.getInputProcessor()->setCurrentSelectedSlot(0);
        }
        if (select && frameNumber == (screenshotFrames * 3) / 4) {
            // Not GridLayer::enterSelectionMode: that selects the focused slot,
            // and with no pointer there is no focus, so the empty selection
            // cancels the mode again straight away. Set the mode and pick a
            // slot explicitly instead.
            gridLayer.getHud()->enterSelectionMode();
            gridLayer.addSlotToSelectedItems(0, false, true);
        }
        if (popupButton >= 0 && frameNumber == (screenshotFrames * 7) / 8) {
            // A press and a release on the button, since a popup opens on the
            // release, and there is no pointer here to do it.
            MenuBar *bar = gridLayer.getHud()->getMenuBar();
            MotionEvent press;
            press.xs[0] = bar->buttonCenterX((size_t)popupButton);
            press.ys[0] = bar->getY() + bar->getHeight() * 0.5f;
            press.action = MotionEvent::ACTION_DOWN;
            bar->onTouchEvent(press);
            press.action = MotionEvent::ACTION_UP;
            bar->onTouchEvent(press);
        }
        if (zoom && frameNumber == (screenshotFrames * 7) / 8) {
            gridLayer.zoomInToSelectedItem();
        }
        if (scrub && frameNumber == (screenshotFrames * 7) / 8) {
            // Straight at the bar rather than through the hit test list: a
            // press has to land on it and then stay down, and there is no
            // pointer here to hold it there.
            TimeBar *timeBar = gridLayer.getHud()->getTimeBar();
            MotionEvent down;
            down.action = MotionEvent::ACTION_DOWN;
            down.xs[0] = timeBar->getX() + timeBar->getWidth() * scrubAt;
            down.ys[0] = timeBar->getY() + timeBar->getHeight() * 0.5f;
            timeBar->onTouchEvent(down);
        }
        if ((rotate || deleteSelection) && frameNumber == (screenshotFrames * 7) / 8) {
            if (rotate) {
                gridLayer.rotateSelectedItems(90.0f);
            }
            if (deleteSelection) {
                gridLayer.deleteSelection();
            }
        }
        if (!screenshotPath.empty() && frameNumber >= screenshotFrames &&
            SDL_GetTicks() - startTicks >= screenshotAfterMs) {
            renderView.onDrawFrame();
            if (saveFramebuffer(pixelWidth, pixelHeight, screenshotPath)) {
                running = false;
            } else if (SDL_GetTicks() - startTicks > screenshotAfterMs + 5000) {
                SDL_Log("Gave up waiting for a composited frame");
                running = false;
            }
        }
    }

    gridLayer.shutdown();
    renderView.shutdown();
    Canvas::shutdownFonts();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

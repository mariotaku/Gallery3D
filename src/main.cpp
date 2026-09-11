// Entry point, replacing com.cooliris.media.Gallery.
//
// Usage: gallery3d [photo directory] [--also directory] [--scale N]
//        [--safe-area L,T,R,B]
// Defaults to the user's Pictures folder. --also shows a second directory
// alongside the first, through ConcatenatedDataSource. --help lists the rest.
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#include <intrin.h>
#endif
#include <string>
#include <vector>

#include "App.h"
#include "ArticDataSource.h"
#include "Backtrace.h"
#include "Canvas.h"
#include "ConcatenatedDataSource.h"
#include "GridLayer.h"
#include "GridLayoutInterface.h"
#include "Input.h"
#include "LocalDataSource.h"
#include "RenderView.h"
#include "CaptionButtons.h"
#include "HudLayer.h"
#include "Texture.h"
#include "WindowFrame.h"
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
#if defined(__EMSCRIPTEN__)
    // Preloaded into the runtime's filesystem at this path, by the
    // --preload-file in CMakeLists. There is no binary to sit next to.
    return "/assets";
#else
    // Assets are copied next to the binary at build time.
    const char *base = SDL_GetBasePath();
    if (base == nullptr) {
        return "assets";
    }
    return std::string(base) + "assets";
#endif
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

    // The caption strip, which is the app's to drag by now that it is inside
    // the client area. A draggable region swallows the click before the app
    // ever sees it, so it has to stop short of everything up there that is
    // meant to be pressed: the crumbs on the left, the mode button and the
    // window buttons on the right.
    //
    // The HUD is asked where those actually are. Reserving a fixed width for
    // the path bar instead left an eighty pixel strip to grab on a window this
    // wide, because the bar is allowed far more room than one crumb uses.
    HudLayer *hud = (HudLayer *)data;
    if (hud == nullptr) {
        return SDL_HITTEST_NORMAL;
    }
    const int safeTop = (int)App::SAFE_AREA.top;
    const int captionBottom = safeTop + (int)(44.0f * App::UI_DENSITY + 0.5f);
    if (area->y >= safeTop && area->y < captionBottom && area->x > (int)hud->draggableLeft() &&
        area->x < (int)hud->draggableRight()) {
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

// The flag list lives here and only here. A copy of it in the readme would be
// wrong within a release or two; this one cannot drift from the parser below
// without somebody noticing on the next run.
// Everything that follows the display rather than the window's contents. Called
// at startup and again whenever the window lands on a display with a different
// scale, because a laptop plugged into an external monitor does exactly that.
//
// Returns true when the scale actually moved, so the caller knows whether the
// wall has to be rebuilt or only resized.
// Set by --dpi-change, so a scripted run can go through the whole reflow
// without a second monitor to drag the window onto.
float sForcedDisplayScale = 0.0f;

bool applyDisplayScale(SDL_Window *window) {
    float displayScale = (sForcedDisplayScale > 0.0f) ? sForcedDisplayScale : SDL_GetWindowDisplayScale(window);
    if (displayScale <= 0.0f) {
        displayScale = 1.0f;
    }
    // Two separate things, multiplied into the one knob the ported code reads.
    // The display scale is what SDL reports for the monitor: how many physical
    // pixels a logical pixel is worth, so text and assets stay crisp on HiDPI.
    // The content scale says how big the wall should be, because the ported
    // constants were picked for a 320x480 phone. Everything downstream keys off
    // App::PIXEL_DENSITY - grid item size, slot spacing in GridLayoutInterface,
    // labels in DisplaySlot, quads in GridDrawables, thumbnail resolution in
    // Texture - so scaling it here scales the whole wall coherently.
    const float wanted = displayScale * App::CONTENT_SCALE;
    if (SDL_fabsf(wanted - App::PIXEL_DENSITY) < 0.001f) {
        return false;
    }
    App::PIXEL_DENSITY = wanted;
    // The chrome follows the display and not the wall, so a button is the size
    // the screen asks for rather than that times the wall's enlargement.
    App::UI_DENSITY = displayScale;
    SDL_Log("PIXEL_DENSITY %.3f (display %.3f x content %.3f), UI_DENSITY %.3f, drawables from the %.1fx bucket",
            App::PIXEL_DENSITY, displayScale, App::CONTENT_SCALE, App::UI_DENSITY, App::drawableBucketDensity());
    return true;
}

// What the platform says is safe to put controls in. SDL reports the whole
// client area on a desktop, so these come out zero and nothing moves; on a
// phone it is the rect left over once the cutouts are taken off. The insets are
// in window coordinates, so they move when the window does.
void applySafeArea(SDL_Window *window, bool overridden, const App::SafeAreaInsets &overrideInsets) {
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
    if (overridden) {
        App::SAFE_AREA = overrideInsets;
    }
    if (App::SAFE_AREA.left != 0.0f || App::SAFE_AREA.top != 0.0f || App::SAFE_AREA.right != 0.0f ||
        App::SAFE_AREA.bottom != 0.0f) {
        SDL_Log("Safe area insets: left %.0f top %.0f right %.0f bottom %.0f", App::SAFE_AREA.left, App::SAFE_AREA.top,
                App::SAFE_AREA.right, App::SAFE_AREA.bottom);
    }
}

// A fullscreen photo is drawn about as wide as the window, so decode it to at
// least that. Below the old 1024 there is nothing to gain, and the cap keeps a
// very large window from turning every photo into a 4096 texture.
void applyPhotoResolution(int pixelWidth, int pixelHeight) {
    int longEdge = (pixelWidth > pixelHeight) ? pixelWidth : pixelHeight;
    int screenNail = std::min(2048, std::max(1024, longEdge));
    // Twice that when zoomed, which covers the fill-screen zoom without trying
    // to hold a whole 24 megapixel photo on the card.
    int hiRes = std::min(4096, screenNail * 2);
    if (screenNail == App::SCREEN_NAIL_MAX_EDGE && hiRes == App::HI_RES_MAX_EDGE) {
        return;
    }
    App::SCREEN_NAIL_MAX_EDGE = screenNail;
    App::HI_RES_MAX_EDGE = hiRes;
    SDL_Log("Screennail max edge %d, hi-res max edge %d", App::SCREEN_NAIL_MAX_EDGE, App::HI_RES_MAX_EDGE);
}

// The accelerometer reading, turned from the device's own axes into the
// display's.
//
// SDL reports the axes of the hardware, which are fixed to the case, and the
// display orientation separately. The original did this switch on
// Display.getRotation() inside onSensorChanged; it belongs here instead,
// because it is the platform's business rather than the wall's.
//
// Only the first axis is used downstream - the wall leans along the screen and
// never up it - but all three are passed through so the seam does not have to
// change if that stops being true.
void queueAccelerometer(RenderView &renderView, SDL_Window *window, const float values[3]) {
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    float alongScreen;
    switch (SDL_GetCurrentDisplayOrientation(display)) {
    case SDL_ORIENTATION_LANDSCAPE:
        alongScreen = -values[1];
        break;
    case SDL_ORIENTATION_PORTRAIT_FLIPPED:
        alongScreen = -values[0];
        break;
    case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
        alongScreen = values[1];
        break;
    case SDL_ORIENTATION_PORTRAIT:
    default:
        // Also the answer where the orientation is unknown, which is what a
        // desktop reports. A desktop with an accelerometer is a laptop lid
        // sensor, and portrait is the right reading of it.
        alongScreen = values[0];
        break;
    }
    renderView.queueAccelerometer(alongScreen, values[1], values[2]);
}

void printUsage() {
    SDL_Log("Usage: gallery3d [photo directory] [options]");
    SDL_Log("");
    SDL_Log("  Browses a directory of photos as a 3D wall, one stack per folder.");
    SDL_Log("  Defaults to your Pictures folder.");
    SDL_Log("");
    SDL_Log("Options");
    SDL_Log("  --artic              browse the Art Institute of Chicago instead of a");
    SDL_Log("                       directory, over its public api. Read only, so the");
    SDL_Log("                       delete and rotate buttons do not appear");
    SDL_Log("  --also DIR           show a second directory on the same wall");
    SDL_Log("  --scale N            how much bigger the wall is than the phone it was");
    SDL_Log("                       laid out for; raise for bigger stacks, fewer on screen");
    SDL_Log("  --backdrop-blur KIND how the wash behind the wall is blurred: gaussian");
    SDL_Log("                       (the default) or box, which is what the original did");
    SDL_Log("  --backdrop-sigma N   how strong the gaussian is, in pixels of the cropped");
    SDL_Log("                       photo. 2.58 matches the box it replaces; higher is");
    SDL_Log("                       a softer wash");
    SDL_Log("  --safe-area L,T,R,B  pretend the window has cutouts, so the layout that");
    SDL_Log("                       keeps controls clear of a notch can be seen here");
    SDL_Log("  --help               this");
    SDL_Log("");
    SDL_Log("Checking a build without a hand on the mouse. These drive the app to a");
    SDL_Log("state and render a fixed number of frames, so a screenshot is repeatable.");
    SDL_Log("  --screenshot PATH    save the framebuffer and exit");
    SDL_Log("  --frames N           how many frames to render first");
    SDL_Log("  --open N             open album N on the way");
    SDL_Log("  --timeline           switch to the timeline");
    SDL_Log("  --fullscreen [N]     open photo N of the album fullscreen");
    SDL_Log("  --zoom [N]           zoom that photo N times, which is what reaches for");
    SDL_Log("                       the tiles or the full resolution texture");
    SDL_Log("                       (needs --fullscreen)");
    SDL_Log("  --select             enter selection mode and pick one item");
    SDL_Log("  --rotate             rotate the selection (needs --select)");
    SDL_Log("  --delete             delete the selection (needs --select)");
    SDL_Log("  --popup N            tap button N on the selection bar (needs --select)");
    SDL_Log("  --scrub [0..1]       hold a drag on the time bar (needs --open)");
    SDL_Log("  --tilt N             lean the wall as though the accelerometer read N along");
    SDL_Log("                       the screen, for a machine that has no sensor");
    SDL_Log("  --dpi-change N       part way through, act as though the window moved to a");
    SDL_Log("                       display of scale N, and reflow for it");
    SDL_Log("  --crash KIND         die on purpose, to check the stack trace comes out.");
    SDL_Log("                       KIND is read, write, throw, abort, crt or");
    SDL_Log("                       fastfail");
}

}  // namespace

int main(int argc, char **argv) {
    // First thing, so a crash while parsing arguments still names itself.
    Backtrace::install();

    std::string photoDirectory;
    // A second library, shown after the first. Two sources behind one feed.
    std::string alsoDirectory;
    // A museum catalogue over http, rather than a directory.
    bool artic = false;
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
    // Which photo of the opened album to look at. Sizes differ enough between
    // artworks that the tiled view behaves differently on one and the next.
    int fullscreenSlot = 0;
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
    // The scale to move to part way through, so the reflow can be captured.
    float dpiChangeTo = 0.0f;
    // A pretend accelerometer reading, for a machine that has none.
    bool tilted = false;
    float tiltTo = 0.0f;
    // Zooms the fullscreen photo, which is the only thing that reaches for the
    // hi-res texture. Needs --fullscreen, and fires after it.
    bool zoom = false;
    // How many times to zoom in. Each step is the same one the double tap
    // makes, and the tiled view only has somewhere to go past the first.
    int zoomSteps = 1;
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
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                fullscreenSlot = std::max(0, std::atoi(argv[++i]));
            }
        } else if (arg == "--select") {
            select = true;
        } else if (arg == "--rotate") {
            rotate = true;
        } else if (arg == "--delete") {
            deleteSelection = true;
        } else if (arg == "--popup" && i + 1 < argc) {
            popupButton = std::atoi(argv[++i]);
        } else if (arg == "--backdrop-blur" && i + 1 < argc) {
            const std::string kind = argv[++i];
            if (kind == "box") {
                App::BACKDROP_BLUR = App::BACKDROP_BLUR_BOX;
            } else if (kind == "gaussian") {
                App::BACKDROP_BLUR = App::BACKDROP_BLUR_GAUSSIAN;
            } else {
                SDL_Log("Unknown --backdrop-blur %s, wanted box or gaussian", kind.c_str());
                return 1;
            }
        } else if (arg == "--backdrop-sigma" && i + 1 < argc) {
            const float sigma = (float)std::atof(argv[++i]);
            if (sigma < 0.0f || sigma > 32.0f) {
                SDL_Log("--backdrop-sigma %s is outside 0 to 32", argv[i]);
                return 1;
            }
            App::BACKDROP_BLUR_SIGMA = sigma;
        } else if (arg == "--zoom") {
            zoom = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                zoomSteps = std::max(1, std::atoi(argv[++i]));
            }
        } else if (arg == "--tilt" && i + 1 < argc) {
            tilted = true;
            tiltTo = (float)std::atof(argv[++i]);
        } else if (arg == "--dpi-change" && i + 1 < argc) {
            dpiChangeTo = (float)std::atof(argv[++i]);
        } else if (arg == "--scrub") {
            scrub = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                scrubAt = (float)std::atof(argv[++i]);
            }
        } else if (arg == "--help" || arg == "-h" || arg == "/?") {
            // Before SDL_Init, so there is nothing to tear down.
            printUsage();
            return 0;
        } else if (arg == "--crash" && i + 1 < argc) {
            // The handler is only worth having if it fires, and the way to know
            // is to break the process on purpose. Each kind leaves by a
            // different door: fastfail is the one that skips every handler but
            // the vectored one, and it is what an out of range container does.
            std::string kind = argv[++i];
            volatile int *nowhere = nullptr;
            if (kind == "read") {
                SDL_Log("crash: reading through a null pointer");
                return *nowhere;
            } else if (kind == "write") {
                SDL_Log("crash: writing through a null pointer");
                *nowhere = 1;
            } else if (kind == "throw") {
                SDL_Log("crash: throwing with nothing to catch it");
                throw std::runtime_error("a deliberate crash");
            } else if (kind == "abort") {
                SDL_Log("crash: abort");
                std::abort();
            } else if (kind == "crt") {
                SDL_Log("crash: handing the CRT an argument it refuses");
#if defined(_WIN32)
                char room[4];
                // Deliberately too long. This is what a container going out of
                // range looks like from the outside: a bare 0xC0000409.
                strcpy_s(room, sizeof(room), "far too long for this");
                SDL_Log("crash: the CRT let that through, which it should not");
#else
                // The bounds checked string functions are a Microsoft
                // extension, and so is the handler that catches their failure.
                SDL_Log("crash: nothing to demonstrate here, this one is MSVC's");
#endif
            } else if (kind == "fastfail") {
                SDL_Log("crash: fail fast");
                // Nothing catches this one. It leaves through the kernel
                // without raising an exception, so no handler sees it. Here to
                // show what an unreadable exit looks like, next to the rest.
#if defined(_WIN32)
                __fastfail(FAST_FAIL_FATAL_APP_EXIT);
#else
                std::abort();
#endif
            }
            SDL_Log("crash: no such kind: %s", kind.c_str());
            return 2;
        } else if (arg == "--artic") {
            artic = true;
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

    // Again, because SDL_Init puts its own exception filter in during startup
    // and ours has to be the one on top.
    Backtrace::install();

    // Sensors are asked for but not required: a machine with none still runs,
    // it just never leans.
    if (!SDL_InitSubSystem(SDL_INIT_SENSOR)) {
        SDL_Log("No sensor subsystem (%s), the wall will not lean", SDL_GetError());
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

    // A window with its ordinary frame. WindowFrame then takes the caption area
    // into the client area, which is not the same as asking for a borderless
    // one: the frame stays, so snapping, the resize borders and the shadow are
    // the system's to handle rather than ours to imitate.
    SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
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
    applyDisplayScale(window);
    applySafeArea(window, safeAreaOverridden, safeAreaOverride);

    const bool extendedFrame = WindowFrame::install(window);
    Canvas::initFonts();

    RenderView renderView;
    if (!renderView.init(window)) {
        SDL_Log("RenderView init failed");
        return 1;
    }

    LocalDataSource dataSource(photoDirectory);
    // Declared before the layer, so they are destroyed after it. The feed holds
    // a bare pointer to whichever of these it was given, and shutting the layer
    // down reaches through that pointer - which has to still be there.
    std::unique_ptr<ArticDataSource> articSource;
    std::unique_ptr<LocalDataSource> alsoSource;
    std::unique_ptr<ConcatenatedDataSource> combinedSource;
    DataSource *feedSource = &dataSource;
    if (artic) {
        articSource = std::make_unique<ArticDataSource>();
        feedSource = articSource.get();
        SDL_Log("Browsing api.artic.edu");
    }
    if (!alsoDirectory.empty()) {
        alsoSource = std::make_unique<LocalDataSource>(alsoDirectory);
        combinedSource = std::make_unique<ConcatenatedDataSource>(feedSource, alsoSource.get());
        feedSource = combinedSource.get();
        SDL_Log("Also showing %s", alsoDirectory.c_str());
    }

    GridLayoutInterface layoutInterface(4);
    GridLayer gridLayer(GridLayer::itemWidthForDensity(), GridLayer::itemHeightForDensity(), &layoutInterface,
                        &renderView);
    // The wall leans with the device, as the original did. A desktop reports no
    // accelerometer and this opens nothing, which is the common case.
    SDL_Sensor *accelerometer = nullptr;
    {
        int count = 0;
        SDL_SensorID *sensors = SDL_GetSensors(&count);
        for (int i = 0; i < count && accelerometer == nullptr; ++i) {
            if (SDL_GetSensorTypeForID(sensors[i]) == SDL_SENSOR_ACCEL) {
                accelerometer = SDL_OpenSensor(sensors[i]);
            }
        }
        SDL_free(sensors);
        if (accelerometer != nullptr) {
            SDL_Log("Accelerometer open, the wall will lean with the device");
        }
    }

    if (extendedFrame) {
        // After the HUD exists, because the hit test asks it where the crumbs
        // end and where the buttons begin. The top edge and the caption strip
        // are inside the client area now, so the app answers for them; the
        // other three edges are still the frame's.
        SDL_SetWindowHitTest(window, windowHitTest, gridLayer.getHud());
    }

    renderView.setRootLayer(&gridLayer);
    renderView.onSurfaceCreated();

    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    renderView.onSurfaceChanged(pixelWidth, pixelHeight);

    applyPhotoResolution(pixelWidth, pixelHeight);

    if (WindowFrame::isExtended()) {
        // The caption is the app's to draw now, so the buttons in it are the
        // app's to act on. The layer has no window, so the window comes from
        // here.
        gridLayer.getHud()->getCaptionButtons()->setActions(
            [window]() { SDL_MinimizeWindow(window); },
            [window]() {
                if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) {
                    SDL_RestoreWindow(window);
                } else {
                    SDL_MaximizeWindow(window);
                }
            },
            []() {
                // Through the queue rather than straight out, so the shutdown
                // at the end of main still runs.
                SDL_Event quit;
                SDL_zero(quit);
                quit.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quit);
            });
        gridLayer.getHud()->getCaptionButtons()->setMaximized(
            (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0);
    }

    gridLayer.setDataSource(feedSource);
    if (!artic) {
        SDL_Log("Scanning %s", photoDirectory.c_str());
    }

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
    // One frame, as a callable, because the browser owns the frame clock and
    // calls back rather than letting the app spin. Captured by reference: with
    // simulate_infinite_loop set, Emscripten leaves main's stack standing, so
    // everything here stays alive for as long as the callback runs.
    auto drawFrame = [&]() {
        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent)) {
            switch (sdlEvent.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_SENSOR_UPDATE:
                if (accelerometer != nullptr && sdlEvent.sensor.which == SDL_GetSensorID(accelerometer)) {
                    queueAccelerometer(renderView, window, sdlEvent.sensor.data);
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
                applySafeArea(window, safeAreaOverridden, safeAreaOverride);
                applyPhotoResolution(pixelWidth, pixelHeight);
                renderView.onSurfaceChanged(pixelWidth, pixelHeight);
                renderView.requestRender();
                break;
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
                // The maximise button shows a different glyph either side of
                // this, and the window can be maximised from the keyboard or by
                // snapping it, not only by that button.
                if (WindowFrame::isExtended()) {
                    gridLayer.getHud()->getCaptionButtons()->setMaximized(
                        (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0);
                }
                renderView.requestRender();
                break;
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                // Dragged to a monitor with a different scale, or the scale on
                // this one changed underneath us. Sizes fixed at the old
                // density are wrong everywhere now, so the wall is rebuilt
                // before the layout runs again.
                if (applyDisplayScale(window)) {
                    gridLayer.onDensityChanged();
                }
                SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
                applySafeArea(window, safeAreaOverridden, safeAreaOverride);
                applyPhotoResolution(pixelWidth, pixelHeight);
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
                // Always, so chrome that lights under the pointer hears about
                // it. The touch queue below still only sees a drag.
                renderView.queuePointerMove(sdlEvent.motion.x * pointerScale(),
                                            sdlEvent.motion.y * pointerScale());
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
        // Deliberately not on the same frame as the actions below: the reflow
        // empties the display list, and entering fullscreen in the same frame
        // would be reading it before anything refilled it. A real change lands
        // between frames, not inside one.
        if (tilted) {
            // Every frame, because the camera animates towards the offset and
            // one reading would be overtaken by the next frame's easing. A real
            // device sends these continuously too.
            const float values[3] = {tiltTo, 0.0f, 0.0f};
            queueAccelerometer(renderView, window, values);
        }
        if (dpiChangeTo > 0.0f && frameNumber == (screenshotFrames * 7) / 8) {
            // The same path the window event takes, so what is captured here is
            // what a real move between monitors does.
            SDL_Log("Pretending the display scale became %.3f", dpiChangeTo);
            sForcedDisplayScale = dpiChangeTo;
            if (applyDisplayScale(window)) {
                gridLayer.onDensityChanged();
            }
            SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
            applySafeArea(window, safeAreaOverridden, safeAreaOverride);
            applyPhotoResolution(pixelWidth, pixelHeight);
            renderView.onSurfaceChanged(pixelWidth, pixelHeight);
        }
        if (timeline && frameNumber == (screenshotFrames * 3) / 4) {
            gridLayer.setState(GridLayer::STATE_TIMELINE);
        }
        if (fullscreen && frameNumber == (screenshotFrames * 3) / 4) {
            gridLayer.getInputProcessor()->setCurrentSelectedSlot(fullscreenSlot);
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
        // Just after the photo goes fullscreen, rather than near the end. The
        // tiles of a zoomed picture are fetched over the network, and a
        // screenshot taken a frame after the zoom would only ever catch the
        // screennail underneath them.
        if (zoom && frameNumber == (screenshotFrames * 13) / 16) {
            for (int step = 0; step < zoomSteps; ++step) {
                gridLayer.zoomInToSelectedItem();
            }
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
    };

#if defined(__EMSCRIPTEN__)
    // Zero means "whenever the browser next paints", which is
    // requestAnimationFrame, and is what a page should be pacing off. The call
    // does not return.
    emscripten_set_main_loop_arg(
        [](void *arg) {
            auto *frame = (decltype(drawFrame) *)arg;
            (*frame)();
        },
        &drawFrame, 0, 1);
#else
    while (running) {
        drawFrame();
    }
#endif

    if (accelerometer != nullptr) {
        SDL_CloseSensor(accelerometer);
    }
    gridLayer.shutdown();
    renderView.shutdown();
    Canvas::shutdownFonts();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

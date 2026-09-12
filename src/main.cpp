// Entry point replacing com.cooliris.media.Gallery. Settings come from INI/environment;
// command-line actions drive a single run. See --help.
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#if defined(__ANDROID__)
// There is no process to start: SDLActivity loads this library and calls in.
// The header renames main() to the entry point SDL looks for.
#include <SDL3/SDL_main.h>
#endif

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
#include "Settings.h"
#include "Canvas.h"
#include "ConcatenatedDataSource.h"
#include "GridLayer.h"
#include "GridLayoutInterface.h"
#include "Input.h"
#include "LocalDataSource.h"
#if defined(__ANDROID__)
#include "AndroidBridge.h"
#include "MediaStoreDataSource.h"
#include "RegionDecoder.h"
#endif
#include "PopupMenu.h"
#include "RenderView.h"
#include "CaptionButtons.h"
#include "HudLayer.h"
#include "Texture.h"
#include "WindowFrame.h"
#include "gles2.h"

namespace {

std::string defaultPhotoDirectory() {
    // Reads the Known Folder on Windows and the XDG user directory on Linux,
    // so a relocated or translated Pictures folder still resolves.
    if (const char *pictures = SDL_GetUserFolder(SDL_FOLDER_PICTURES)) {
        return pictures;
    }
    // No XDG config, or a platform SDL has no folder for.
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
#elif defined(__ANDROID__)
    // Packed into the apk. SDL's file functions read a relative path through
    // the asset manager, so this is the path inside assets/ and not on disk.
    return "assets";
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

// Writes framebuffer pixels for --screenshot. Returns false before the first
// composite produces pixels so the caller can retry.
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

// Hit testing for the client-area caption drag strip and top resize border.
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

    // Caption dragging excludes HUD crumbs and buttons: SDL consumes draggable-area
    // clicks before the app receives them.
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

// Display-density override for --dpi-change reflow captures.
float sForcedDisplayScale = 0.0f;

bool applyDisplayScale(SDL_Window *window) {
    float displayScale = (sForcedDisplayScale > 0.0f) ? sForcedDisplayScale : SDL_GetWindowDisplayScale(window);
    if (displayScale <= 0.0f) {
        displayScale = 1.0f;
    }
    // Wall density combines SDL display scale with the content scale for its handset layout.
    const float wanted = displayScale * App::CONTENT_SCALE;
    if (SDL_fabsf(wanted - App::PIXEL_DENSITY) < 0.001f) {
        return false;
    }
    App::PIXEL_DENSITY = wanted;
    // Chrome follows display scale alone.
    App::UI_DENSITY = displayScale;
    SDL_Log("PIXEL_DENSITY %.3f (display %.3f x content %.3f), UI_DENSITY %.3f, drawables from the %.1fx bucket",
            App::PIXEL_DENSITY, displayScale, App::CONTENT_SCALE, App::UI_DENSITY, App::drawableBucketDensity());
    return true;
}

// SDL safe-area insets in window coordinates; normally zero on desktop.
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

// Set fullscreen decode size from the window, clamped to limit texture memory.
void applyPhotoResolution(int pixelWidth, int pixelHeight) {
    int longEdge = (pixelWidth > pixelHeight) ? pixelWidth : pixelHeight;
    int screenNail = std::min(2048, std::max(1024, longEdge));
    // Allow twice the screennail resolution for zoom.
    int hiRes = std::min(4096, screenNail * 2);
    if (screenNail == App::SCREEN_NAIL_MAX_EDGE && hiRes == App::HI_RES_MAX_EDGE) {
        return;
    }
    App::SCREEN_NAIL_MAX_EDGE = screenNail;
    App::HI_RES_MAX_EDGE = hiRes;
    SDL_Log("Screennail max edge %d, hi-res max edge %d", App::SCREEN_NAIL_MAX_EDGE, App::HI_RES_MAX_EDGE);
}

// Rotate SDL hardware accelerometer axes into display orientation.
// The wall currently uses only the first axis.
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
        // Treat unknown orientation as portrait, including desktop sensors.
        alongScreen = values[0];
        break;
    }
    renderView.queueAccelerometer(alongScreen, values[1], values[2]);
}

// Apply global settings; report invalid values and return false.
bool applyBackdropBlur(const std::string &kind) {
    if (kind == "box") {
        App::BACKDROP_BLUR = App::BACKDROP_BLUR_BOX;
        return true;
    }
    if (kind == "gaussian") {
        App::BACKDROP_BLUR = App::BACKDROP_BLUR_GAUSSIAN;
        return true;
    }
    SDL_Log("Unknown backdrop blur \"%s\", wanted box or gaussian", kind.c_str());
    return false;
}

bool applyBackdropSigma(float sigma) {
    if (sigma < 0.0f || sigma > 32.0f) {
        SDL_Log("A backdrop sigma of %g is outside 0 to 32", sigma);
        return false;
    }
    App::BACKDROP_BLUR_SIGMA = sigma;
    return true;
}

bool applySafeArea(const std::string &text, App::SafeAreaInsets *insets) {
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (SDL_sscanf(text.c_str(), "%f,%f,%f,%f", &values[0], &values[1], &values[2], &values[3]) != 4) {
        SDL_Log("A safe area of \"%s\" is not left,top,right,bottom", text.c_str());
        return false;
    }
    insets->left = values[0];
    insets->top = values[1];
    insets->right = values[2];
    insets->bottom = values[3];
    return true;
}

bool applySettings(const Settings::Store &settings, App::SafeAreaInsets *safeArea, bool *safeAreaOverridden) {
    if (settings.has("wall.scale")) {
        const float scale = settings.getFloat("wall.scale", App::CONTENT_SCALE);
        if (scale <= 0.0f) {
            SDL_Log("A wall scale of %g is not a size", scale);
            return false;
        }
        App::CONTENT_SCALE = scale;
    }
    if (settings.has("backdrop.blur") && !applyBackdropBlur(settings.get("backdrop.blur", ""))) {
        return false;
    }
    if (settings.has("backdrop.sigma") &&
        !applyBackdropSigma(settings.getFloat("backdrop.sigma", App::BACKDROP_BLUR_SIGMA))) {
        return false;
    }
    if (settings.has("window.safe-area")) {
        if (!applySafeArea(settings.get("window.safe-area", ""), safeArea)) {
            return false;
        }
        *safeAreaOverridden = true;
    }
    return true;
}

// Minimum window size is 320x320 display units to fit HUD and wall.
// Convert to SDL window coordinates; SDL also enlarges an existing undersized window.
const int kMinimumWindowPoints = 320;

void applyMinimumSize(SDL_Window *window) {
    float scale = SDL_GetWindowDisplayScale(window);
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    const int minimum = (int)((float)kMinimumWindowPoints * scale + 0.5f);
    SDL_SetWindowMinimumSize(window, minimum, minimum);
}

// SDL_Log takes a printf format, and GCC rejects an empty one.
void logBlankLine() {
    SDL_Log("%s", "");
}

void printUsage() {
    SDL_Log("Usage: gallery3d [options]");
    logBlankLine();
    SDL_Log("  Browses a directory of photos as a 3D wall, one stack per folder.");
    SDL_Log("  Set library.photos to choose it; the default is your Pictures folder.");
    logBlankLine();
    SDL_Log("Options");
    SDL_Log("  --config PATH        read settings from this file instead of looking");
    SDL_Log("  --help               this");
    logBlankLine();
    SDL_Log("Settings live in an ini file or the environment, not on the command line.");
    SDL_Log("Each is written section.key, and the environment variable follows from the");
    SDL_Log("name:");
    logBlankLine();
    for (const Settings::Known &setting : Settings::known()) {
        SDL_Log("  %-18s %s", setting.name, setting.summary);
        SDL_Log("  %-18s %s", "", Settings::environmentNameFor(setting.name).c_str());
    }
    logBlankLine();
    SDL_Log("The file is looked for in order, first one found wins:");
    for (const std::string &path : Settings::searchPaths()) {
        SDL_Log("  %s", path.c_str());
    }
    logBlankLine();
    SDL_Log("  [backdrop]");
    SDL_Log("  blur = gaussian");
    SDL_Log("  sigma = 4.0");
    logBlankLine();
    SDL_Log("Checking a build without a hand on the mouse. These drive the app to a");
    SDL_Log("state and render a fixed number of frames, so a screenshot is repeatable.");
    SDL_Log("  --window-size WxH    open the window at this size, to see the layout");
    SDL_Log("                       at one the mouse cannot reach");
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
    SDL_Log("  --popup N[,R]        tap button N on the selection bar, then row R of the");
    SDL_Log("                       popup it opens (needs --select)");
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

#if defined(__ANDROID__)
    // Here, because this is the one thread that can look an app class up by
    // name. The loader threads start later and cannot.
    AndroidBridge::init();
    RegionDecoder::initAndroid();
#endif

    // Read --config before loading settings, then process run arguments.
    std::string configPath;
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--config") {
            configPath = argv[i + 1];
            break;
        }
    }
    if (configPath.empty()) {
        if (const char *fromEnvironment = std::getenv("GALLERY3D_CONFIG")) {
            configPath = fromEnvironment;
        }
    }
    Settings::Store settings;
    if (!settings.load(configPath)) {
        SDL_Log("No settings file at %s", configPath.c_str());
        return 1;
    }
    if (!settings.path().empty()) {
        SDL_Log("Settings from %s", settings.path().c_str());
    }
    for (const Settings::Known &setting : Settings::known()) {
        if (settings.has(setting.name)) {
            // Log each setting's source.
            SDL_Log("  %s = %s (%s)", setting.name, settings.get(setting.name, "").c_str(),
                    settings.sourceOf(setting.name).c_str());
        }
    }
    for (const std::string &complaint : settings.complaints()) {
        // Report unknown keys without aborting.
        SDL_Log("Settings: %s", complaint.c_str());
    }

    std::string photoDirectory = settings.get("library.photos", std::string());
    // A second library, shown after the first. Two sources behind one feed.
    std::string alsoDirectory = settings.get("library.also", std::string());
    // The web build defaults to the museum source because it cannot browse a local filesystem.
#if defined(__EMSCRIPTEN__)
    const bool articByDefault = true;
#else
    const bool articByDefault = false;
#endif
    bool artic = settings.getBool("library.artic", articByDefault);
    // Override safe-area insets to exercise cutout layout on desktop.
    bool safeAreaOverridden = false;
    App::SafeAreaInsets safeAreaOverride;
    if (!applySettings(settings, &safeAreaOverride, &safeAreaOverridden)) {
        return 1;
    }
    std::string screenshotPath;
    int screenshotFrames = 240;
    // Open an album during a scripted capture.
    int openSlot = -1;
    // Enter timeline view during a scripted capture.
    bool timeline = false;
    // Enter fullscreen during a scripted capture.
    bool fullscreen = false;
    // Photo index within the opened album.
    int fullscreenSlot = 0;
    // Enter selection mode during a scripted capture.
    bool select = false;
    // Selection operations; require --select.
    bool rotate = false;
    bool deleteSelection = false;
    // Hold the time bar at a fraction from 0 to 1 to show its date popup. Requires --open.
    bool scrub = false;
    float scrubAt = 0.5f;
    // The scale to move to part way through, so the reflow can be captured.
    float dpiChangeTo = 0.0f;
    // A pretend accelerometer reading, for a machine that has none.
    bool tilted = false;
    float tiltTo = 0.0f;
    // Zoom after entering fullscreen; requires --fullscreen.
    bool zoom = false;
    // Number of double-tap zoom steps.
    int zoomSteps = 1;
    // Taps a button on the bottom selection bar, so the popup it opens can be
    // captured. Needs --select. -1 for off.
    int popupButton = -1;
    // Which row of the popup that button opens, -1 for none.
    int popupRow = -1;
    // Initial window dimensions.
    int windowWidth = 1280;
    int windowHeight = 800;
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
            // N taps bar button N; N,R also taps popup row R.
            if (SDL_sscanf(argv[++i], "%d,%d", &popupButton, &popupRow) < 1) {
                SDL_Log("--popup wants a button index, and optionally a row after a comma");
                return 1;
            }
        } else if (arg == "--window-size" && i + 1 < argc) {
            int width = 0;
            int height = 0;
            if (SDL_sscanf(argv[++i], "%dx%d", &width, &height) != 2 || width <= 0 || height <= 0) {
                SDL_Log("A window size of \"%s\" is not WIDTHxHEIGHT", argv[i]);
                return 1;
            }
            windowWidth = width;
            windowHeight = height;
        } else if (arg == "--config" && i + 1 < argc) {
            // Already read, before any of this.
            ++i;
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
            // Exercise crash handlers with distinct termination paths.
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
                // Trigger the CRT invalid-parameter path with an oversized string.
                strcpy_s(room, sizeof(room), "far too long for this");
                SDL_Log("crash: the CRT let that through, which it should not");
#else
                // The bounds checked string functions are a Microsoft
                // extension, and so is the handler that catches their failure.
                SDL_Log("crash: nothing to demonstrate here, this one is MSVC's");
#endif
            } else if (kind == "fastfail") {
                SDL_Log("crash: fail fast");
                // __fastfail exits through the kernel without raising a catchable exception.
#if defined(_WIN32)
                __fastfail(FAST_FAIL_FATAL_APP_EXIT);
#else
                std::abort();
#endif
            }
            SDL_Log("crash: no such kind: %s", kind.c_str());
            return 2;
        } else {
            // Settings are accepted through the file and environment, not flags.
            SDL_Log("No such option: %s", arg.c_str());
            printUsage();
            return 1;
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

    // Extend content into the caption while retaining the system frame, snapping and shadow.
    SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window *window = SDL_CreateWindow("Gallery3D", windowWidth, windowHeight, windowFlags);
    if (window == nullptr) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }
    applyMinimumSize(window);

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
    // Sources must outlive the layer: feed shutdown uses their bare pointers.
    std::unique_ptr<ArticDataSource> articSource;
    std::unique_ptr<LocalDataSource> alsoSource;
    std::unique_ptr<ConcatenatedDataSource> combinedSource;
    DataSource *feedSource = &dataSource;
#if defined(__ANDROID__)
    // The library is the media store's, not a directory's. Scoped storage
    // leaves an app nothing to walk from Android 10 on.
    MediaStoreDataSource mediaStoreSource;
    feedSource = &mediaStoreSource;
#endif
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
    // Open an accelerometer if one is available.
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
        // Install after HUD creation so hit testing can exclude controls.
        // The app handles the caption and top resize edge; the frame handles the other edges.
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
        // Supply window actions to the caption-button layer.
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
#if defined(__ANDROID__)
    SDL_Log("Reading the photo library from the media store");
#else
    if (!artic) {
        SDL_Log("Scanning %s", photoDirectory.c_str());
    }
#endif

    // Pointer state, turned into the MotionEvents the ported gesture code wants.
    // SDL reports pointer positions in window units; the renderer works in
    // pixels, so scale on the way in.
    bool mouseDown = false;
    MotionEvent event;

    // Track SDL fingers in press order and combine them into Android-style multi-pointer
    // events.
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
    // Require elapsed time as well as --frames so nonblocking swaps cannot outrun
    // loading/compositing.
    const uint64_t startTicks = SDL_GetTicks();
    const uint64_t screenshotAfterMs = (uint64_t)screenshotFrames * 1000ull / 60ull;
    bool running = true;
    // Browser frame callback. simulate_infinite_loop preserves main's stack,
    // keeping references captured here alive.
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
                // Update the glyph for maximise changes from buttons, keyboard or snapping.
                if (WindowFrame::isExtended()) {
                    gridLayer.getHud()->getCaptionButtons()->setMaximized(
                        (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0);
                }
                renderView.requestRender();
                break;
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                // Rebuild density-dependent resources before relayout on display-scale changes.
                if (applyDisplayScale(window)) {
                    gridLayer.onDensityChanged();
                }
                // The floor is in the display's units, so moving to a display
                // of another scale moves the floor with it.
                applyMinimumSize(window);
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
        // Apply density changes on a separate frame: reflow clears the display list,
        // which must refill before fullscreen actions read it.
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
            // Set selection mode and select a slot explicitly; without pointer focus,
            // enterSelectionMode would create an empty selection and cancel itself.
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
        // And a row of the popup that opened, a few frames later so it has
        // been laid out and knows where its rows are.
        if (popupButton >= 0 && popupRow >= 0 && frameNumber == (screenshotFrames * 7) / 8 + 4) {
            PopupMenu *popup = gridLayer.getHud()->getPopupMenu();
            float x = 0.0f;
            float y = 0.0f;
            if (popup->rowCenter((size_t)popupRow, &x, &y)) {
                MotionEvent press;
                press.xs[0] = x;
                press.ys[0] = y;
                press.action = MotionEvent::ACTION_DOWN;
                popup->onTouchEvent(press);
                press.action = MotionEvent::ACTION_UP;
                popup->onTouchEvent(press);
            } else {
                SDL_Log("--popup row %d is not there", popupRow);
            }
        }
        // Zoom soon after fullscreen entry to allow network tiles to load before capture.
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
    // Zero selects requestAnimationFrame. This call does not return.
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

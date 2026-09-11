#include "WindowFrame.h"

#include <SDL3/SDL.h>

#include "App.h"

#if defined(_WIN32)

#include <windows.h>
// After windows.h.
#include <commctrl.h>
#include <dwmapi.h>

namespace {

bool sExtended = false;

// The caption is as tall as Windows would have drawn it. Matching the system
// means the buttons land where the pointer expects them, and a window snapped
// beside a normal one lines up.
int sCaptionHeightPx = 32;

// Windows 11 names these; the SDK that built this may predate them, so they are
// spelled out rather than relied on.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

// How thick the resize frame is, at this window's dpi. Asked for every time
// rather than cached, because the window can move to a display with a different
// scale and these change with it.
int frameThickness(HWND window, int metric) {
    UINT dpi = GetDpiForWindow(window);
    if (dpi == 0) {
        dpi = 96;
    }
    return GetSystemMetricsForDpi(metric, dpi);
}

int captionHeightFor(HWND window) {
    UINT dpi = GetDpiForWindow(window);
    if (dpi == 0) {
        dpi = 96;
    }
    return GetSystemMetricsForDpi(SM_CYCAPTION, dpi);
}

LRESULT CALLBACK subclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    switch (message) {
    case WM_NCCALCSIZE: {
        if (wParam == FALSE) {
            break;
        }
        NCCALCSIZE_PARAMS *params = (NCCALCSIZE_PARAMS *)lParam;
        const RECT proposed = params->rgrc[0];

        // Let Windows work out the frame it wants, then take back only the
        // caption. Doing the arithmetic by hand instead gets the borders wrong
        // on some dpi settings, and gets them wrong again when maximised.
        DefSubclassProc(window, message, wParam, lParam);
        RECT *client = &params->rgrc[0];

        if (IsZoomed(window)) {
            // A maximised window is deliberately larger than the monitor by the
            // frame thickness, so its borders fall offscreen. Keeping the
            // caption would put the content offscreen with them, so only the
            // caption's own height comes back here.
            client->top = proposed.top + frameThickness(window, SM_CYSIZEFRAME) +
                          frameThickness(window, SM_CXPADDEDBORDER);
        } else {
            // The whole way up. The top resize edge now sits inside the client
            // area, which is what the app's hit test covers.
            client->top = proposed.top;
        }
        sCaptionHeightPx = captionHeightFor(window);
        return 0;
    }
    case WM_DPICHANGED:
        sCaptionHeightPx = captionHeightFor(window);
        break;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

}  // namespace

namespace WindowFrame {

bool install(SDL_Window *sdlWindow) {
    if (sdlWindow == nullptr) {
        return false;
    }
    HWND window = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(sdlWindow),
                                               SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (window == nullptr) {
        SDL_Log("WindowFrame: no HWND, leaving the title bar alone");
        return false;
    }

    if (!SetWindowSubclass(window, subclassProc, 1, 0)) {
        SDL_Log("WindowFrame: could not subclass the window");
        return false;
    }

    // One pixel of frame extended into the client area. Without this the window
    // loses its drop shadow, which is the visible difference between a window
    // with a real frame and a borderless one pretending.
    MARGINS margins = {0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(window, &margins);

    // The caption is gone but its border is not, and a light border around a
    // dark wall reads as a mistake.
    BOOL dark = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    sCaptionHeightPx = captionHeightFor(window);

    // The frame only changes once the window is asked to recalculate it.
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                     SWP_NOACTIVATE);

    sExtended = true;
    SDL_Log("Window content extends under the title bar (caption %d px)", sCaptionHeightPx);
    return true;
}

bool isExtended() {
    return sExtended;
}

float captionHeight() {
    return (float)sCaptionHeightPx;
}

float captionButtonsWidth() {
    // Three buttons at the width Windows uses for one, which is what makes a
    // pointer thrown at the top right corner hit close.
    return 3.0f * 46.0f * App::UI_DENSITY;
}

}  // namespace WindowFrame

#else

namespace WindowFrame {

bool install(SDL_Window *) {
    return false;
}

bool isExtended() {
    return false;
}

float captionHeight() {
    return 0.0f;
}

float captionButtonsWidth() {
    return 0.0f;
}

}  // namespace WindowFrame

#endif

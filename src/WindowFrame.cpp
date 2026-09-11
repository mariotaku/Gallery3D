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

// Match the Windows caption height.
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

// Query resize-frame thickness at the current DPI; it changes between displays.
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

        // Let Windows compute DPI/maximised frame borders, then reclaim the caption.
        DefSubclassProc(window, message, wParam, lParam);
        RECT *client = &params->rgrc[0];

        if (IsZoomed(window)) {
            // Maximised windows exceed monitor bounds by frame thickness. Reclaim only
            // the caption height so content stays onscreen.
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

    // Extend one frame pixel into the client area to retain the drop shadow.
    MARGINS margins = {0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(window, &margins);

    // Use a dark border to match the content.
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
    // Reserve three system-width caption buttons.
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

package me.mariotaku.gallery3d;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * The wall's activity. SDLActivity loads the native libraries named below, then
 * calls into main().
 *
 * The wall paints every pixel of the screen, so the window runs under the
 * status and navigation bars with both left transparent. What that costs is the
 * space behind them: the native side reads the insets below and keeps its
 * controls clear of them, the same way it already handles a display cutout.
 */
public class MainActivity extends SDLActivity {

    // Last insets seen, in pixels. Read from the SDL thread, written from the
    // ui thread, so they are volatile rather than locked: four independent
    // numbers a frame late are not worth a lock on the draw path.
    private static volatile int sInsetLeft;
    private static volatile int sInsetTop;
    private static volatile int sInsetRight;
    private static volatile int sInsetBottom;

    /**
     * The activity, for the helpers the native side calls. They run on a loader
     * thread with no activity of their own to reach for.
     */
    public static Activity getContext() {
        // Activity, not Context: SDLActivity returns one and a static method
        // may only hide another that returns the same thing. Every caller here
        // wants a Context, which an Activity is.
        return SDLActivity.getContext();
    }

    /** Left, top, right, bottom in pixels, behind the system bars and cutout. */
    public static int[] systemBarInsets() {
        return new int[] {sInsetLeft, sInsetTop, sInsetRight, sInsetBottom};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        drawUnderSystemBars();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        // The folder picker's answer. Anything else is SDL's own file dialog.
        if (requestCode == StorageBridge.PICK_FOLDER_REQUEST) {
            StorageBridge.onFolderPicked(this, resultCode == RESULT_OK && data != null ? data.getData() : null);
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // SDLActivity sets its own system ui visibility when focus returns,
        // which drops the layout flags. Putting them back here is what keeps
        // the surface full height rather than stopping at the bars.
        if (hasFocus) {
            drawUnderSystemBars();
        }
    }

    private void drawUnderSystemBars() {
        Window window = getWindow();
        // The theme has already put the layout under the bars. This paints them
        // transparent, which the translucent flags alone do not: they ask the
        // system for its own scrim.
        window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        window.setStatusBarColor(Color.TRANSPARENT);
        window.setNavigationBarColor(Color.TRANSPARENT);

        final View content = window.getDecorView();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
        } else {
            content.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            // Let the window extend into a notch rather than stopping short of
            // it, since the insets below already account for one.
            window.getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        // A view that fits system windows is inset by them, which is exactly
        // what stops the surface reaching the screen edges. SDL builds its own
        // layout inside the content frame, so both have to be told not to.
        stopFittingSystemWindows(findViewById(android.R.id.content));

        content.setOnApplyWindowInsetsListener((view, insets) -> {
            readInsets(insets);
            // Consume nothing: SDL's own view still wants to see these.
            return insets;
        });
        content.requestApplyInsets();
    }

    private static void stopFittingSystemWindows(View view) {
        if (view == null) {
            return;
        }
        view.setFitsSystemWindows(false);
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); ++i) {
                stopFittingSystemWindows(group.getChildAt(i));
            }
        }
    }

    private static void readInsets(WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            android.graphics.Insets bars = insets.getInsets(
                WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            sInsetLeft = bars.left;
            sInsetTop = bars.top;
            sInsetRight = bars.right;
            sInsetBottom = bars.bottom;
            return;
        }
        sInsetLeft = insets.getSystemWindowInsetLeft();
        sInsetTop = insets.getSystemWindowInsetTop();
        sInsetRight = insets.getSystemWindowInsetRight();
        sInsetBottom = insets.getSystemWindowInsetBottom();
    }

    @Override
    protected String[] getLibraries() {
        // Load order matters: each one links against the ones before it, and
        // the last name is the library SDL looks in for main().
        return new String[] {
            "SDL3",
            "SDL3_image",
            // No SDL3_ttf: TextBridge draws the text through the platform.
            "main",
        };
    }
}

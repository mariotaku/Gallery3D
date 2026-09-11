package me.mariotaku.gallery3d;

import org.libsdl.app.SDLActivity;

/**
 * The wall's activity. SDLActivity loads the native libraries named below, then
 * calls into main().
 */
public class MainActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        // Load order matters: each one links against the ones before it, and
        // the last name is the library SDL looks in for main().
        return new String[] {
            "SDL3",
            "SDL3_image",
            "SDL3_ttf",
            "main",
        };
    }
}

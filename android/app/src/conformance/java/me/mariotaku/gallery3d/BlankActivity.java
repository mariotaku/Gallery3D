package me.mariotaku.gallery3d;

import android.app.Activity;

/**
 * An activity with nothing on it, for the instrumentation tests.
 *
 * SDL reads the apk's assets and the app's folders through the activity it is
 * given, and the bridges reach their context through the same one. MainActivity
 * would do, except that it starts SDL's main thread, which in this build type
 * runs every test on its own.
 */
public class BlankActivity extends Activity {
}

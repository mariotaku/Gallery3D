package me.mariotaku.gallery3d;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;

import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.libsdl.app.SDL;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * The native tests, one JUnit test each, run on the device without the wall.
 *
 * They call the platform the way the app does: decoding through BitmapFactory
 * and BitmapRegionDecoder, text through Paint, drawables through the
 * resources. SDL is set up by hand around a blank activity, which is what it
 * reads the apk's assets through.
 *
 * Run them with
 *
 *   ANDROID_SERIAL=emulator-5554 android/gradlew -p android connectedConformanceAndroidTest
 *
 * and add -Pandroid.testInstrumentationRunnerArguments.filter=decode to run the
 * tests whose name contains "decode".
 */
@RunWith(Parameterized.class)
public class ConformanceTest {

    static {
        // In link order. main is the conformance build type's, which holds the
        // tests and the entry points below.
        System.loadLibrary("SDL3");
        System.loadLibrary("SDL3_image");
        System.loadLibrary("main");
    }

    private static Activity sActivity;
    private static String sFixtureRoot;
    private static boolean sFixturesCopied;

    private final String mName;

    public ConformanceTest(String name) {
        mName = name;
    }

    @Parameterized.Parameters(name = "{0}")
    public static List<Object[]> tests() {
        String filter = InstrumentationRegistry.getArguments().getString("filter", "");
        List<Object[]> tests = new ArrayList<>();
        for (String name : nativeNames()) {
            if (name.contains(filter)) {
                tests.add(new Object[] {name});
            }
        }
        return tests;
    }

    @BeforeClass
    public static void setUp() {
        Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();
        Context target = instrumentation.getTargetContext();
        Intent intent = new Intent(target, BlankActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        sActivity = instrumentation.startActivitySync(intent);

        SDL.setupJNI();
        SDL.initialize();
        SDL.setContext(sActivity);

        sFixtureRoot = new File(target.getFilesDir(), "fixtures").getPath();
        sFixturesCopied = nativeSetUp(sFixtureRoot);
    }

    @AfterClass
    public static void tearDown() {
        SDL.setContext(null);
        if (sActivity != null) {
            sActivity.finish();
            sActivity = null;
        }
    }

    @Test
    public void run() {
        assertTrue("Some fixtures could not be copied out of the apk", sFixturesCopied);
        // Failed checks, skipped tests and tests run.
        int[] counts = new int[3];
        String report = new String(nativeRun(mName, sFixtureRoot, counts), StandardCharsets.UTF_8);
        assertNotEquals("The tests could not set up SDL or the fonts\n" + report, -1, counts[0]);
        assertEquals("No test is named " + mName, 1, counts[2]);
        assertEquals(report, 0, counts[0]);
        assumeTrue(report, counts[1] == 0);
    }

    /** The native tests' names, in the order they are registered. */
    private static native String[] nativeNames();

    /**
     * Finds the bridge classes from this thread, and copies the fixtures out of
     * the apk into fixtureRoot. False when a fixture could not be copied.
     */
    private static native boolean nativeSetUp(String fixtureRoot);

    /**
     * Runs the test with exactly this name. Returns its report in UTF-8, and
     * fills counts with the failed checks (-1 when the run could not start),
     * the skipped tests and the tests run.
     */
    private static native byte[] nativeRun(String name, String fixtureRoot, int[] counts);
}

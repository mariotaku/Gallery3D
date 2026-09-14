package me.mariotaku.gallery3d;

import android.content.ContentResolver;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.BitmapRegionDecoder;
import android.graphics.ColorSpace;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Build;
import android.util.Log;

import java.io.InputStream;

/**
 * Crops rectangles out of a large photo with BitmapRegionDecoder, so a zoomed
 * picture decodes only what is on screen.
 *
 * The decoder is the expensive part and is kept open across tiles. The native
 * side holds it as an opaque object and hands it back with every call, which
 * keeps the Rect and Options juggling on this side rather than in JNI.
 */
public final class RegionDecoderBridge {

    private static final String TAG = "Gallery3D";

    private RegionDecoderBridge() {
    }

    /** A decoder for the photo at this content uri, or null. */
    public static Object open(String uri) {
        Context context = MainActivity.getContext();
        if (context == null || uri == null) {
            return null;
        }
        ContentResolver resolver = context.getContentResolver();
        try (InputStream input = resolver.openInputStream(Uri.parse(uri))) {
            if (input == null) {
                return null;
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                return BitmapRegionDecoder.newInstance(input);
            }
            // The boolean is isShareable, which the platform has ignored for
            // years and which the replacement above drops.
            return BitmapRegionDecoder.newInstance(input, false);
        } catch (Exception error) {
            // A format with no region decoder behind it lands here, which is
            // not a failure worth a stack trace.
            Log.i(TAG, "No region decoder for " + uri + ": " + error);
            return null;
        }
    }

    public static int width(Object decoder) {
        return (decoder instanceof BitmapRegionDecoder) ? ((BitmapRegionDecoder) decoder).getWidth() : 0;
    }

    public static int height(Object decoder) {
        return (decoder instanceof BitmapRegionDecoder) ? ((BitmapRegionDecoder) decoder).getHeight() : 0;
    }

    /**
     * The rectangle given in the original's pixels, reduced by sampleSize.
     * BitmapRegionDecoder rounds a sample size down to a power of two, which is
     * what the tile grid asks for anyway.
     */
    public static Bitmap decodeRegion(Object decoder, int x, int y, int width, int height, int sampleSize) {
        if (!(decoder instanceof BitmapRegionDecoder)) {
            return null;
        }
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = Math.max(1, sampleSize);
        // The native side reads these bytes as RGBA, and a tile is drawn over
        // the photo below it rather than blended into it.
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        options.inPremultiplied = true;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            // In sRGB, the same as the screennail under it.
            options.inPreferredColorSpace = ColorSpace.get(ColorSpace.Named.SRGB);
        }
        try {
            return ((BitmapRegionDecoder) decoder)
                .decodeRegion(new Rect(x, y, x + width, y + height), options);
        } catch (Exception error) {
            Log.w(TAG, "Could not decode a region", error);
            return null;
        }
    }

    public static void close(Object decoder) {
        if (decoder instanceof BitmapRegionDecoder) {
            ((BitmapRegionDecoder) decoder).recycle();
        }
    }

    /** Frees a tile once its pixels have been copied out. */
    public static void recycle(Bitmap bitmap) {
        if (bitmap != null) {
            bitmap.recycle();
        }
    }
}

package me.mariotaku.gallery3d;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.Log;

/**
 * The app's drawables, resolved through its own resources.
 *
 * The build copies assets/drawable-*dpi into res/drawable-*dpi under the same
 * names, so Android picks the bucket for the display and scales the pixels to
 * its density. It copies assets/drawable into res/drawable-nodpi with
 * NODPI_PREFIX in front: that folder's art is drawn at its own pixel size, and
 * several names also exist in a bucket, where they are different art.
 */
public final class DrawableBridge {

    private static final String TAG = "Gallery3D";
    private static final String NODPI_PREFIX = "nodpi_";
    private static final String NINE_PATCH_SUFFIX = ".9";

    private DrawableBridge() {
    }

    /** The drawable from a density bucket, scaled to the display, or null where no bucket has it. */
    public static Bitmap decodeScaled(String name) {
        return decode(resourceName(name));
    }

    /** The plain folder's drawable at its own pixel size, or null. */
    public static Bitmap decodeUnscaled(String name) {
        return decode(NODPI_PREFIX + resourceName(name));
    }

    /** The compiled nine-patch chunk, or null for art that is not a nine-patch. */
    public static byte[] ninePatchChunk(Bitmap bitmap) {
        return bitmap != null ? bitmap.getNinePatchChunk() : null;
    }

    /** Frees a decoded drawable once its pixels have been copied out. */
    public static void recycle(Bitmap bitmap) {
        if (bitmap != null) {
            bitmap.recycle();
        }
    }

    /** aapt names a nine-patch's resource without its .9, as it drops the .png. */
    private static String resourceName(String name) {
        if (name.endsWith(NINE_PATCH_SUFFIX)) {
            return name.substring(0, name.length() - NINE_PATCH_SUFFIX.length());
        }
        return name;
    }

    private static Bitmap decode(String resourceName) {
        Context context = MainActivity.getContext();
        if (context == null) {
            return null;
        }
        Resources resources = context.getResources();
        int id = resources.getIdentifier(resourceName, "drawable", context.getPackageName());
        if (id == 0) {
            return null;
        }
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        try {
            return BitmapFactory.decodeResource(resources, id, options);
        } catch (RuntimeException | OutOfMemoryError error) {
            Log.w(TAG, "Could not decode drawable " + resourceName, error);
            return null;
        }
    }
}

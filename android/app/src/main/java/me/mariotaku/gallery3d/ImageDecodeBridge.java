package me.mariotaku.gallery3d;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.ColorSpace;
import android.os.Build;
import android.util.Log;

/**
 * Decodes a photo straight to a reduced size.
 *
 * BitmapFactory reduces while decoding, which is what keeps a full size surface
 * off the heap: a 4000x3000 photo wanted as a thumbnail never exists at 48MB.
 * Doing this through the platform rather than a bundled decoder also covers HEIF
 * and whatever else the vendor handles.
 */
public final class ImageDecodeBridge {

    private static final String TAG = "Gallery3D";

    private ImageDecodeBridge() {
    }

    /**
     * The size a decode for maxEdge gives, as Bitmap::fitWithin in the native
     * code works it out: the long edge becomes maxEdge and the short edge is
     * rounded to the nearest pixel, never below 1. A picture that already
     * fits keeps its size.
     */
    static int[] fitWithin(int width, int height, int maxEdge) {
        final int longEdge = Math.max(width, height);
        if (width <= 0 || height <= 0 || maxEdge <= 0 || longEdge <= maxEdge) {
            return new int[] {width, height};
        }
        final int shortEdge = Math.min(width, height);
        final int scaled = (int) Math.max(1L, ((long) shortEdge * maxEdge + longEdge / 2) / longEdge);
        return width >= height ? new int[] {maxEdge, scaled} : new int[] {scaled, maxEdge};
    }

    /**
     * Decodes at the size fitWithin gives. inSampleSize halves, so the decode
     * lands between that size and twice it, and the last step scales down.
     */
    public static Bitmap decodeSampled(byte[] encoded, int maxEdge) {
        if (encoded == null || encoded.length == 0 || maxEdge <= 0) {
            return null;
        }
        try {
            BitmapFactory.Options measure = new BitmapFactory.Options();
            measure.inJustDecodeBounds = true;
            BitmapFactory.decodeByteArray(encoded, 0, encoded.length, measure);
            final int longest = Math.max(measure.outWidth, measure.outHeight);
            if (longest <= 0) {
                return null;
            }

            BitmapFactory.Options options = new BitmapFactory.Options();
            // Powers of two only: anything else is rounded down to one. The
            // sample stops before a halving would fall short of maxEdge, since
            // the size is the same on every platform and a scale only shrinks.
            int sample = 1;
            while (longest / (sample * 2) >= maxEdge) {
                sample *= 2;
            }
            options.inSampleSize = sample;
            options.inPreferredConfig = Bitmap.Config.ARGB_8888;
            options.inPremultiplied = true;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                // Otherwise the decoder can keep the photo's own colour space,
                // such as Display P3, whose values the native side reads as
                // sRGB.
                options.inPreferredColorSpace = ColorSpace.get(ColorSpace.Named.SRGB);
            }
            final Bitmap decoded = BitmapFactory.decodeByteArray(encoded, 0, encoded.length, options);
            if (decoded == null) {
                return null;
            }
            final int[] size = fitWithin(measure.outWidth, measure.outHeight, maxEdge);
            if (decoded.getWidth() == size[0] && decoded.getHeight() == size[1]) {
                return decoded;
            }
            final Bitmap scaled = Bitmap.createScaledBitmap(decoded, size[0], size[1], true);
            if (scaled != decoded) {
                decoded.recycle();
            }
            return scaled;
        } catch (Exception | OutOfMemoryError error) {
            Log.w(TAG, "Could not decode a photo", error);
            return null;
        }
    }

    /** Frees a decoded photo once its pixels have been copied out. */
    public static void recycle(Bitmap bitmap) {
        if (bitmap != null) {
            bitmap.recycle();
        }
    }
}

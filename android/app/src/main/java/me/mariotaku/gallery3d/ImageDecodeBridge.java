package me.mariotaku.gallery3d;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
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
     * Decodes so the result is no smaller than maxEdge on its long edge.
     * inSampleSize halves, so the result can be up to twice that; the caller
     * scales the last step.
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
            // Powers of two only: anything else is rounded down to one, and a
            // sample size that overshoots would decode smaller than asked for.
            int sample = 1;
            while (longest / (sample * 2) >= maxEdge) {
                sample *= 2;
            }
            options.inSampleSize = sample;
            options.inPreferredConfig = Bitmap.Config.ARGB_8888;
            options.inPremultiplied = true;
            return BitmapFactory.decodeByteArray(encoded, 0, encoded.length, options);
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

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
     * Decodes reduced by the sample Bitmap::sampleSizeFor gives, at whatever
     * size the platform decoder hands back for it. The other platforms follow
     * this decoder's sizes, so nothing is scaled after.
     */
    public static Bitmap decodeSampled(byte[] encoded, int maxEdge) {
        // A maxEdge of 0 or below decodes whole, still through the platform, so
        // the colours are converted the same way at every size.
        if (encoded == null || encoded.length == 0) {
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
            // sample stops before a halving would fall short of maxEdge, as
            // Bitmap::sampleSizeFor does.
            int sample = 1;
            while (maxEdge > 0 && longest / (sample * 2) >= maxEdge) {
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

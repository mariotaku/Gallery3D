package me.mariotaku.gallery3d;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;

/**
 * Text drawn the way the platform draws it.
 *
 * Paint carries the system's own font and, behind it, the chain the system
 * falls back through for a glyph that font has no answer for. That is the whole
 * reason for coming over here: Japanese, Korean and emoji come out as
 * themselves rather than as boxes.
 *
 * The native side asks for the size first and then for the pixels, and both
 * answers have to agree, so the two go through the same Paint set up the same
 * way.
 */
public final class TextBridge {

    private TextBridge() {
    }

    private static Paint paintFor(float fontSize, boolean bold) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        // DEFAULT is the system's text face, and the fallback chain hangs off it.
        paint.setTypeface(Typeface.create(Typeface.DEFAULT, bold ? Typeface.BOLD : Typeface.NORMAL));
        paint.setTextSize(fontSize);
        // The caller tints the coverage, so the colour drawn here is never seen.
        paint.setColor(0xFFFFFFFF);
        return paint;
    }

    /**
     * Width and height in pixels, as {@link #render} would draw them.
     */
    public static int[] measure(String text, float fontSize, boolean bold) {
        Paint paint = paintFor(fontSize, bold);
        Paint.FontMetrics metrics = paint.getFontMetrics();
        // The whole line box, not the tight bounds of these particular glyphs,
        // so two strings at one size line up with each other.
        int height = (int) Math.ceil(metrics.descent - metrics.ascent);
        int width = (int) Math.ceil(paint.measureText(text));
        return new int[] {Math.max(width, 0), Math.max(height, 0)};
    }

    /**
     * The glyphs in white on transparent, sized as {@link #measure} reports.
     * Null when there is nothing to draw.
     */
    public static Bitmap render(String text, float fontSize, boolean bold) {
        Paint paint = paintFor(fontSize, bold);
        Paint.FontMetrics metrics = paint.getFontMetrics();
        int width = (int) Math.ceil(paint.measureText(text));
        int height = (int) Math.ceil(metrics.descent - metrics.ascent);
        if (width <= 0 || height <= 0) {
            return null;
        }
        Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        // drawText puts the baseline at y, and the caller wants the line drawn
        // from the top edge of what measure reported.
        canvas.drawText(text, 0.0f, -metrics.ascent, paint);
        return bitmap;
    }
}

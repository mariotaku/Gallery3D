package me.mariotaku.gallery3d;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.LinkedHashMap;
import java.util.Map;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Reads the photo library out of MediaStore for the native wall.
 *
 * The cursor walk stays on this side and the results cross as JSON, which the
 * native code already has a parser for. Doing it the other way would be several
 * hundred lines of JNI for the same rows.
 */
public final class MediaStoreBridge {

    private static final String TAG = "Gallery3D";

    private static final Uri IMAGES = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;

    private MediaStoreBridge() {
    }

    private static ContentResolver resolver() {
        Context context = MainActivity.getContext();
        return context != null ? context.getContentResolver() : null;
    }

    /**
     * One entry per folder holding photos: its id, its name and how many it has.
     * Matches what the wall draws on a stack before it opens.
     */
    /**
     * The bucket the camera writes to, by id rather than by name.
     *
     * MediaStore derives a bucket's id from the lowercased path of the folder
     * holding it, so the id for DCIM/Camera can be worked out without reading a
     * row. Going by the name instead would miss the folder wherever the system
     * translates it, and would match any other app that made a folder called
     * Camera.
     */
    private static String cameraBucketId() {
        String path = Environment.getExternalStorageDirectory() + "/DCIM/Camera";
        // Matches MediaProvider.computeBucketValues, which is what fills the
        // column being compared against.
        return String.valueOf(path.toLowerCase().hashCode());
    }

    public static String queryBuckets() {
        ContentResolver resolver = resolver();
        if (resolver == null) {
            return "[]";
        }
        String[] columns = {
            MediaStore.Images.Media.BUCKET_ID,
            MediaStore.Images.Media.BUCKET_DISPLAY_NAME,
            MediaStore.Images.Media.DATE_TAKEN,
        };
        // Counting with a GROUP BY is not available through the public api on
        // every version, so the rows are walked and tallied here instead.
        Map<String, long[]> counts = new LinkedHashMap<>();
        Map<String, String> names = new LinkedHashMap<>();

        try (Cursor cursor = resolver.query(IMAGES, columns, null, null,
                MediaStore.Images.Media.DATE_TAKEN + " DESC")) {
            if (cursor == null) {
                return "[]";
            }
            int bucketColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.BUCKET_ID);
            int nameColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.BUCKET_DISPLAY_NAME);
            int takenColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATE_TAKEN);
            while (cursor.moveToNext()) {
                String bucket = cursor.getString(bucketColumn);
                if (bucket == null) {
                    continue;
                }
                String name = cursor.getString(nameColumn);
                long taken = cursor.getLong(takenColumn);
                long[] tally = counts.get(bucket);
                if (tally == null) {
                    // count, newest date taken
                    counts.put(bucket, new long[] {1, taken});
                    names.put(bucket, name != null ? name : bucket);
                } else {
                    tally[0]++;
                    if (taken > tally[1]) {
                        tally[1] = taken;
                    }
                }
            }
        } catch (RuntimeException error) {
            Log.w(TAG, "Could not read the photo buckets", error);
            return "[]";
        }

        JSONArray buckets = new JSONArray();
        final String cameraId = cameraBucketId();
        try {
            for (Map.Entry<String, long[]> entry : counts.entrySet()) {
                JSONObject bucket = new JSONObject();
                bucket.put("id", entry.getKey());
                bucket.put("name", names.get(entry.getKey()));
                bucket.put("count", entry.getValue()[0]);
                bucket.put("dateTaken", entry.getValue()[1]);
                bucket.put("camera", cameraId.equals(entry.getKey()));
                buckets.put(bucket);
            }
        } catch (JSONException error) {
            Log.w(TAG, "Could not write the bucket list", error);
            return "[]";
        }
        return buckets.toString();
    }

    /** Every photo in one folder, newest first. */
    public static String queryBucket(String bucketId) {
        ContentResolver resolver = resolver();
        if (resolver == null || bucketId == null) {
            return "[]";
        }
        String[] columns = {
            MediaStore.Images.Media._ID,
            MediaStore.Images.Media.DISPLAY_NAME,
            MediaStore.Images.Media.MIME_TYPE,
            MediaStore.Images.Media.DATE_TAKEN,
            MediaStore.Images.Media.DATE_MODIFIED,
            MediaStore.Images.Media.DATE_ADDED,
            MediaStore.Images.Media.ORIENTATION,
            MediaStore.Images.Media.WIDTH,
            MediaStore.Images.Media.HEIGHT,
        };
        JSONArray photos = new JSONArray();
        try (Cursor cursor = resolver.query(IMAGES, columns,
                MediaStore.Images.Media.BUCKET_ID + " = ?", new String[] {bucketId},
                MediaStore.Images.Media.DATE_TAKEN + " DESC")) {
            if (cursor == null) {
                return "[]";
            }
            int idColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media._ID);
            int nameColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DISPLAY_NAME);
            int mimeColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.MIME_TYPE);
            int takenColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATE_TAKEN);
            int modifiedColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATE_MODIFIED);
            int addedColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATE_ADDED);
            int orientationColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.ORIENTATION);
            int widthColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.WIDTH);
            int heightColumn = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.HEIGHT);

            while (cursor.moveToNext()) {
                JSONObject photo = new JSONObject();
                photo.put("id", cursor.getLong(idColumn));
                photo.put("name", nonNull(cursor.getString(nameColumn)));
                photo.put("mime", nonNull(cursor.getString(mimeColumn)));
                photo.put("dateTaken", cursor.getLong(takenColumn));
                photo.put("dateModified", cursor.getLong(modifiedColumn));
                photo.put("dateAdded", cursor.getLong(addedColumn));
                photo.put("orientation", cursor.getInt(orientationColumn));
                photo.put("width", cursor.getInt(widthColumn));
                photo.put("height", cursor.getInt(heightColumn));
                photos.put(photo);
            }
        } catch (JSONException | RuntimeException error) {
            Log.w(TAG, "Could not read bucket " + bucketId, error);
            return "[]";
        }
        return photos.toString();
    }

    /**
     * The bytes of one photo. Reading through the resolver rather than a file
     * path is what keeps this working once scoped storage is in force, where
     * the DATA column no longer names something the app may open.
     */
    public static byte[] readImage(long id) {
        ContentResolver resolver = resolver();
        if (resolver == null) {
            return null;
        }
        Uri uri = Uri.withAppendedPath(IMAGES, String.valueOf(id));
        try (InputStream input = resolver.openInputStream(uri)) {
            if (input == null) {
                return null;
            }
            ByteArrayOutputStream out = new ByteArrayOutputStream(1 << 16);
            byte[] buffer = new byte[1 << 16];
            int read;
            while ((read = input.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        } catch (Exception error) {
            Log.w(TAG, "Could not read image " + id, error);
            return null;
        }
    }

    /** Whether reading the library needs a permission the user has not given. */
    public static boolean needsPermission() {
        Context context = MainActivity.getContext();
        if (context == null) {
            return true;
        }
        String permission = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            ? "android.permission.READ_MEDIA_IMAGES"
            : "android.permission.READ_EXTERNAL_STORAGE";
        return context.checkSelfPermission(permission) != android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    /** The permission to ask for on this version. */
    public static String permissionName() {
        return (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            ? "android.permission.READ_MEDIA_IMAGES"
            : "android.permission.READ_EXTERNAL_STORAGE";
    }

    private static String nonNull(String value) {
        return value != null ? value : "";
    }
}

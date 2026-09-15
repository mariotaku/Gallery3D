package me.mariotaku.gallery3d;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.database.Cursor;
import android.media.ExifInterface;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.DocumentsContract;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * The places photos can come from besides the media store: storage volumes and
 * folders the user grants through the storage access framework.
 *
 * Scoped storage lets an app read another folder only through a tree uri the
 * user picks in the system's folder picker. The grant is kept across restarts
 * with takePersistableUriPermission, and every read goes back through the uri.
 * Lists cross to the native side as JSON, the same as MediaStoreBridge.
 */
public final class StorageBridge {

    private static final String TAG = "Gallery3D";

    /** The request code MainActivity matches the folder picker's result by. */
    static final int PICK_FOLDER_REQUEST = 0x6A11;

    private static final String EXTERNAL_STORAGE_AUTHORITY = "com.android.externalstorage.documents";

    /** Reads photos' EXIF while a folder is listed. Daemon threads, so they never keep the app up. */
    private static final ExecutorService EXIF_READERS = Executors.newFixedThreadPool(
            Math.max(2, Math.min(4, Runtime.getRuntime().availableProcessors())), runnable -> {
                Thread thread = new Thread(runnable, "ExifReader");
                thread.setDaemon(true);
                return thread;
            });
    private static final String PREFERENCES = "storage";
    private static final String CHOSEN_SOURCE = "source";

    private StorageBridge() {
    }

    /**
     * What the home pill offers, in order: the media store, each mounted
     * storage volume, each folder granted before, and the picker for the rest,
     * which lists the cloud providers that share folders. One object each, with
     * kind (library, volume, tree or picker), id and name.
     */
    public static String sources() {
        Context context = MainActivity.getContext();
        JSONArray sources = new JSONArray();
        try {
            sources.put(source("library", "library", "All photos"));
            if (context == null) {
                return sources.toString();
            }
            StorageManager storage = context.getSystemService(StorageManager.class);
            if (storage != null) {
                for (StorageVolume volume : storage.getStorageVolumes()) {
                    String state = volume.getState();
                    if (!Environment.MEDIA_MOUNTED.equals(state)
                            && !Environment.MEDIA_MOUNTED_READ_ONLY.equals(state)) {
                        continue;
                    }
                    String id = volumeId(volume);
                    if (id != null) {
                        sources.put(source("volume", id, volume.getDescription(context)));
                    }
                }
            }
            for (UriPermission permission : context.getContentResolver().getPersistedUriPermissions()) {
                Uri uri = permission.getUri();
                if (permission.isReadPermission() && DocumentsContract.isTreeUri(uri)) {
                    sources.put(source("tree", uri.toString(), treeName(context, uri)));
                }
            }
            sources.put(source("picker", "picker", "Other storage…"));
        } catch (JSONException | RuntimeException error) {
            Log.w(TAG, "Could not list the storage sources", error);
        }
        return sources.toString();
    }

    /**
     * Opens the system folder picker, at the root of the storage volume with
     * this id when one is given. The answer comes back through
     * MainActivity.onActivityResult to onFolderPicked. False when there is no
     * activity to open it from.
     */
    public static boolean pickFolder(String volume) {
        final Activity activity = MainActivity.getContext();
        if (activity == null) {
            return false;
        }
        Intent intent = null;
        if (volume != null && !volume.isEmpty()) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                StorageVolume found = findVolume(activity, volume);
                if (found != null) {
                    intent = found.createOpenDocumentTreeIntent();
                }
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                        DocumentsContract.buildRootUri(EXTERNAL_STORAGE_AUTHORITY, volume));
            }
        }
        if (intent == null) {
            intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        }
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        final Intent picker = intent;
        activity.runOnUiThread(() -> {
            try {
                activity.startActivityForResult(picker, PICK_FOLDER_REQUEST);
            } catch (ActivityNotFoundException error) {
                Log.w(TAG, "No folder picker on this device", error);
                nativeFolderPicked(null);
            }
        });
        return true;
    }

    /** Keeps the grant for a picked folder and hands its uri to the wall. */
    static void onFolderPicked(Context context, Uri tree) {
        if (tree == null) {
            nativeFolderPicked(null);
            return;
        }
        try {
            context.getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException error) {
            // Still readable until the app stops, which is better than nothing.
            Log.w(TAG, "Could not keep the permission for " + tree, error);
        }
        nativeFolderPicked(tree.toString());
    }

    /** The source the wall showed last: library, or a tree uri. */
    public static String chosenSource() {
        SharedPreferences preferences = preferences();
        return preferences != null ? preferences.getString(CHOSEN_SOURCE, "library") : "library";
    }

    public static void setChosenSource(String id) {
        SharedPreferences preferences = preferences();
        if (preferences != null) {
            preferences.edit().putString(CHOSEN_SOURCE, id).apply();
        }
    }

    /** The name the home pill shows for a tree: its folder's display name. */
    public static String treeName(String tree) {
        Context context = MainActivity.getContext();
        return context != null && tree != null ? treeName(context, Uri.parse(tree)) : "";
    }

    /**
     * Every folder in the tree that holds photos, the tree's own folder
     * included, with id (the folder's document uri), name and count. Hidden
     * folders are skipped. Empty when the tree cannot be read.
     */
    public static String listFolders(String treeText) {
        ContentResolver resolver = resolver();
        if (resolver == null || treeText == null) {
            return "";
        }
        Uri tree = Uri.parse(treeText);
        JSONArray folders = new JSONArray();
        try {
            String rootId = DocumentsContract.getTreeDocumentId(tree);
            ArrayDeque<String[]> pending = new ArrayDeque<>();
            pending.add(new String[] {rootId, treeName(resolver, tree, rootId)});
            while (!pending.isEmpty()) {
                String[] folder = pending.poll();
                int photos = 0;
                Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, folder[0]);
                try (Cursor cursor = resolver.query(children, new String[] {
                        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                        DocumentsContract.Document.COLUMN_MIME_TYPE,
                }, null, null, null)) {
                    if (cursor == null) {
                        continue;
                    }
                    while (cursor.moveToNext()) {
                        String id = cursor.getString(0);
                        String name = cursor.getString(1);
                        String mime = cursor.getString(2);
                        if (id == null || name == null || name.startsWith(".")) {
                            continue;
                        }
                        if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                            pending.add(new String[] {id, name});
                        } else if (mime != null && mime.startsWith("image/")) {
                            ++photos;
                        }
                    }
                }
                if (photos > 0) {
                    JSONObject entry = new JSONObject();
                    entry.put("id", DocumentsContract.buildDocumentUriUsingTree(tree, folder[0]).toString());
                    entry.put("name", folder[1]);
                    entry.put("count", photos);
                    folders.put(entry);
                }
            }
        } catch (JSONException | RuntimeException error) {
            Log.w(TAG, "Could not list the folders in " + treeText, error);
            return "";
        }
        return folders.toString();
    }

    /**
     * The photos directly in one folder, with uri, name, mime, dateTaken and
     * dateModified in milliseconds, orientation in degrees, width and height.
     * The date taken, orientation and size come from the photo's EXIF where it
     * has any.
     */
    public static String listPhotos(String folderText) {
        ContentResolver resolver = resolver();
        if (resolver == null || folderText == null) {
            return "[]";
        }
        Uri folder = Uri.parse(folderText);
        JSONArray photos = new JSONArray();
        List<JSONObject> rows = new ArrayList<>();
        List<Uri> uris = new ArrayList<>();
        List<String> mimes = new ArrayList<>();
        try {
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(folder,
                    DocumentsContract.getDocumentId(folder));
            try (Cursor cursor = resolver.query(children, new String[] {
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE,
                    DocumentsContract.Document.COLUMN_LAST_MODIFIED,
            }, null, null, null)) {
                if (cursor == null) {
                    return "[]";
                }
                while (cursor.moveToNext()) {
                    String id = cursor.getString(0);
                    String name = cursor.getString(1);
                    String mime = cursor.getString(2);
                    if (id == null || name == null || name.startsWith(".") || mime == null
                            || !mime.startsWith("image/")) {
                        continue;
                    }
                    Uri uri = DocumentsContract.buildDocumentUriUsingTree(folder, id);
                    JSONObject photo = new JSONObject();
                    photo.put("uri", uri.toString());
                    photo.put("name", name);
                    photo.put("mime", mime);
                    photo.put("dateModified", cursor.isNull(3) ? 0 : cursor.getLong(3));
                    rows.add(photo);
                    uris.add(uri);
                    mimes.add(mime);
                }
            }
            readExifAll(resolver, rows, uris, mimes);
            for (JSONObject photo : rows) {
                photos.put(photo);
            }
        } catch (JSONException | RuntimeException error) {
            Log.w(TAG, "Could not list the photos in " + folderText, error);
            return "[]";
        }
        return photos.toString();
    }

    /** The bytes of one document. */
    public static byte[] readDocument(String uri) {
        ContentResolver resolver = resolver();
        if (resolver == null || uri == null) {
            return null;
        }
        try (InputStream input = resolver.openInputStream(Uri.parse(uri))) {
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
            Log.w(TAG, "Could not read " + uri, error);
            return null;
        }
    }

    private static native void nativeFolderPicked(String tree);

    private static JSONObject source(String kind, String id, String name) throws JSONException {
        JSONObject source = new JSONObject();
        source.put("kind", kind);
        source.put("id", id);
        source.put("name", name != null ? name : "");
        return source;
    }

    /**
     * The id ExternalStorageProvider gives the volume's root: "primary" for the
     * shared storage, and the file system uuid for the rest.
     */
    private static String volumeId(StorageVolume volume) {
        return volume.isPrimary() ? "primary" : volume.getUuid();
    }

    private static StorageVolume findVolume(Context context, String id) {
        StorageManager storage = context.getSystemService(StorageManager.class);
        if (storage == null) {
            return null;
        }
        for (StorageVolume volume : storage.getStorageVolumes()) {
            if (id.equals(volumeId(volume))) {
                return volume;
            }
        }
        return null;
    }

    private static String treeName(Context context, Uri tree) {
        try {
            return treeName(context.getContentResolver(), tree, DocumentsContract.getTreeDocumentId(tree));
        } catch (RuntimeException error) {
            return "";
        }
    }

    private static String treeName(ContentResolver resolver, Uri tree, String documentId) {
        Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, documentId);
        try (Cursor cursor = resolver.query(document,
                new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                return cursor.getString(0);
            }
        } catch (RuntimeException error) {
            Log.w(TAG, "Could not name " + tree, error);
        }
        return "";
    }

    /**
     * Fills each row's EXIF fields. Opening and reading every photo is most of
     * the time a folder takes to list, and the reads do not depend on each
     * other, so a few run at once.
     */
    private static void readExifAll(ContentResolver resolver, List<JSONObject> rows, List<Uri> uris,
            List<String> mimes) throws JSONException {
        List<Future<?>> pending = new ArrayList<>(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            final int index = i;
            pending.add(EXIF_READERS.submit(() -> {
                readExif(resolver, uris.get(index), mimes.get(index), rows.get(index));
                return null;
            }));
        }
        for (int i = 0; i < pending.size(); ++i) {
            try {
                pending.get(i).get();
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                return;
            } catch (ExecutionException error) {
                Log.i(TAG, "No EXIF for " + uris.get(i) + ": " + error.getCause());
                // The row still needs the fields the native side reads.
                JSONObject row = rows.get(i);
                if (!row.has("orientation")) {
                    row.put("orientation", 0);
                    row.put("dateTaken", 0);
                    row.put("width", 0);
                    row.put("height", 0);
                }
            }
        }
    }

    private static void readExif(ContentResolver resolver, Uri uri, String mime, JSONObject photo)
            throws JSONException {
        int orientation = 0;
        long taken = 0;
        int width = 0;
        int height = 0;
        // RAW files have EXIF too, but a RAW beside its JPEG is not shown, and
        // reading a whole RAW over a cloud provider to find out is slow.
        final boolean jpeg = mime.equals("image/jpeg");
        if (jpeg || mime.equals("image/heic") || mime.equals("image/heif") || mime.equals("image/webp")) {
            ExifInterface exif = null;
            // A file descriptor lets ExifInterface seek to the metadata. From a
            // stream it copies a HEIF or WebP whole into memory first.
            try (ParcelFileDescriptor file = resolver.openFileDescriptor(uri, "r")) {
                if (file != null) {
                    exif = new ExifInterface(file.getFileDescriptor());
                }
            } catch (Exception error) {
                // A cloud provider can answer with a pipe, which cannot seek.
                // Only a JPEG is worth reading from the start then.
                if (jpeg) {
                    try (InputStream input = resolver.openInputStream(uri)) {
                        if (input != null) {
                            exif = new ExifInterface(input);
                        }
                    } catch (Exception streamError) {
                        Log.i(TAG, "No EXIF in " + uri + ": " + streamError);
                    }
                }
            }
            if (exif != null) {
                orientation = degreesFor(exif.getAttributeInt(ExifInterface.TAG_ORIENTATION,
                        ExifInterface.ORIENTATION_NORMAL));
                taken = dateFor(exif.getAttribute(ExifInterface.TAG_DATETIME_ORIGINAL));
                width = exif.getAttributeInt(ExifInterface.TAG_IMAGE_WIDTH, 0);
                height = exif.getAttributeInt(ExifInterface.TAG_IMAGE_LENGTH, 0);
            }
        }
        photo.put("orientation", orientation);
        photo.put("dateTaken", taken);
        photo.put("width", width);
        photo.put("height", height);
    }

    private static int degreesFor(int orientation) {
        switch (orientation) {
            case ExifInterface.ORIENTATION_ROTATE_90:
                return 90;
            case ExifInterface.ORIENTATION_ROTATE_180:
                return 180;
            case ExifInterface.ORIENTATION_ROTATE_270:
                return 270;
            default:
                return 0;
        }
    }

    /** EXIF's "yyyy:MM:dd HH:mm:ss", in the device's time zone, or 0. */
    private static long dateFor(String text) {
        if (text == null) {
            return 0;
        }
        try {
            SimpleDateFormat format = new SimpleDateFormat("yyyy:MM:dd HH:mm:ss", Locale.US);
            java.util.Date date = format.parse(text);
            return date != null ? date.getTime() : 0;
        } catch (ParseException error) {
            return 0;
        }
    }

    private static ContentResolver resolver() {
        Context context = MainActivity.getContext();
        return context != null ? context.getContentResolver() : null;
    }

    private static SharedPreferences preferences() {
        Context context = MainActivity.getContext();
        return context != null ? context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE) : null;
    }
}

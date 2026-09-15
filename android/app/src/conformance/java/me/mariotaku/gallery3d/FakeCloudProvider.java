package me.mariotaku.gallery3d;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetFileDescriptor;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Point;
import android.graphics.Shader;
import android.net.Uri;
import android.os.Bundle;
import android.os.CancellationSignal;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * A documents provider shaped like a large cloud drive, for the conformance
 * tests of the folder tree walk and for trying the wall against a slow drive.
 *
 * Three levels of ten folders under the root, 1110 folders in all, and twenty
 * photos in each of the thousand at the bottom. About one bottom folder in
 * fifty answers the first time the way cloud providers do: half of its photos,
 * marked EXTRA_LOADING, and a change notification once the rest are
 * "fetched". One photo in four has a thumbnail stored landscape with a
 * rotation of 90 in EXTRA_ORIENTATION, one in four a thumbnail with no
 * rotation, and the rest no thumbnail. No photo can be opened.
 *
 * Every answer waits as a Network would: a round trip for each call, and a
 * thumbnail's bytes at the network's speed. Offline fails every call. A
 * document id can name its network, as "4g@root/A0" does, which is how the
 * tests pick one. An id without one takes the network chosen in
 * FakeCloudNetworkActivity.
 */
public class FakeCloudProvider extends DocumentsProvider {

    static final String AUTHORITY = "me.mariotaku.gallery3d.conformance.fakecloud";
    static final String ROOT_DOCUMENT = "root";

    /** A network to answer as, like the throttling presets of a browser's developer tools. */
    enum Network {
        HOME("home", "Fast home network", 5, 100_000),
        FIVE_G("5g", "5G", 20, 50_000),
        FOUR_G("4g", "4G", 60, 9_000),
        BAD_FOUR_G("bad4g", "Bad 4G", 400, 400),
        OFFLINE("offline", "Offline", 0, 0);

        /** The name a document id gives the network by. */
        final String id;
        final String title;
        /** The wait for each call to reach the drive and come back. */
        final long roundTripMs;
        /** How fast bytes come down, in kilobits a second. */
        final int kilobitsPerSecond;

        Network(String id, String title, long roundTripMs, int kilobitsPerSecond) {
            this.id = id;
            this.title = title;
            this.roundTripMs = roundTripMs;
            this.kilobitsPerSecond = kilobitsPerSecond;
        }

        String describe() {
            if (this == OFFLINE) {
                return "No connection";
            }
            final String speed = kilobitsPerSecond >= 1000 ? (kilobitsPerSecond / 1000) + " Mbit/s"
                    : kilobitsPerSecond + " kbit/s";
            return roundTripMs + " ms round trip, " + speed;
        }

        static Network forId(String id) {
            for (Network network : values()) {
                if (network.id.equals(id)) {
                    return network;
                }
            }
            return null;
        }
    }

    private static final int FANOUT = 10;
    private static final int LEVELS = 3;
    private static final int PHOTOS = 20;
    private static final long LOADING_MS = 40;

    private static final String PREFERENCES = "fakecloud";
    private static final String CHOSEN_NETWORK = "network";

    private static final String[] ROOT_COLUMNS = {
        Root.COLUMN_ROOT_ID, Root.COLUMN_FLAGS, Root.COLUMN_TITLE, Root.COLUMN_DOCUMENT_ID,
    };
    private static final String[] DOCUMENT_COLUMNS = {
        Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
        Document.COLUMN_LAST_MODIFIED, Document.COLUMN_FLAGS, Document.COLUMN_SIZE,
    };

    private static volatile Network sChosen;

    /** When each loading folder was first asked for, so later queries answer whole. */
    private final Map<String, Long> mFirstAsked = new ConcurrentHashMap<>();
    private final Handler mHandler = new Handler(Looper.getMainLooper());

    /** The network ids without one of their own answer as. */
    static Network chosenNetwork(Context context) {
        Network chosen = sChosen;
        if (chosen == null) {
            chosen = Network.forId(preferences(context).getString(CHOSEN_NETWORK, ""));
            if (chosen == null) {
                chosen = Network.HOME;
            }
            sChosen = chosen;
        }
        return chosen;
    }

    static void chooseNetwork(Context context, Network network) {
        sChosen = network;
        preferences(context).edit().putString(CHOSEN_NETWORK, network.id).apply();
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Cursor queryRoots(String[] projection) {
        MatrixCursor roots = new MatrixCursor(projection != null ? projection : ROOT_COLUMNS);
        roots.newRow()
            .add(Root.COLUMN_ROOT_ID, "cloud")
            .add(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_IS_CHILD)
            .add(Root.COLUMN_TITLE, "Fake cloud")
            .add(Root.COLUMN_DOCUMENT_ID, ROOT_DOCUMENT);
        return roots;
    }

    @Override
    public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        travel(networkOf(documentId), 0);
        MatrixCursor result = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        addRow(result, documentId);
        return result;
    }

    @Override
    public Cursor queryChildDocuments(String parentId, String[] projection, String sortOrder)
            throws FileNotFoundException {
        final Network network = networkOf(parentId);
        travel(network, 0);
        MatrixCursor result = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        final int depth = depthOf(parentId);
        if (depth < LEVELS) {
            for (int i = 0; i < FANOUT; ++i) {
                addRow(result, parentId + "/" + (char) ('A' + depth) + i);
            }
            return result;
        }

        int shown = PHOTOS;
        if (Math.floorMod(pathOf(parentId).hashCode(), 50) == 0) {
            final long loadingMs = Math.max(LOADING_MS, network.roundTripMs * 2);
            long now = SystemClock.uptimeMillis();
            Long first = mFirstAsked.putIfAbsent(parentId, now);
            if (first == null || now - first < loadingMs) {
                shown = PHOTOS / 2;
                Bundle extras = new Bundle();
                extras.putBoolean(DocumentsContract.EXTRA_LOADING, true);
                result.setExtras(extras);
                final Uri notify = DocumentsContract.buildChildDocumentsUri(AUTHORITY, parentId);
                result.setNotificationUri(getContext().getContentResolver(), notify);
                if (first == null) {
                    mHandler.postDelayed(() -> getContext().getContentResolver().notifyChange(notify, null),
                            loadingMs);
                }
            }
        }
        for (int i = 0; i < shown; ++i) {
            addRow(result, parentId + "/IMG_" + i + ".jpg");
        }
        return result;
    }

    @Override
    public boolean isChildDocument(String parentDocumentId, String documentId) {
        return documentId.startsWith(parentDocumentId + "/");
    }

    @Override
    public ParcelFileDescriptor openDocument(String documentId, String mode, CancellationSignal signal)
            throws FileNotFoundException {
        throw new FileNotFoundException("The fake cloud serves listings and thumbnails only");
    }

    @Override
    public AssetFileDescriptor openDocumentThumbnail(String documentId, Point sizeHint, CancellationSignal signal)
            throws FileNotFoundException {
        final int index = photoIndex(documentId);
        if (!hasThumbnail(index)) {
            throw new FileNotFoundException("No thumbnail of " + documentId);
        }
        final int edge = Math.max(16, Math.min(512, Math.max(sizeHint.x, sizeHint.y)));
        final File file = thumbnailFile(edge);
        travel(networkOf(documentId), file.length());
        Bundle extras = null;
        if (index % 4 == 0) {
            extras = new Bundle();
            extras.putInt(DocumentsContract.EXTRA_ORIENTATION, 90);
        }
        ParcelFileDescriptor descriptor = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
        return new AssetFileDescriptor(descriptor, 0, AssetFileDescriptor.UNKNOWN_LENGTH, extras);
    }

    /** Waits as one call bringing back this many bytes would on the network. */
    private static void travel(Network network, long bytes) throws FileNotFoundException {
        if (network == Network.OFFLINE) {
            throw new FileNotFoundException("The fake cloud is offline");
        }
        SystemClock.sleep(network.roundTripMs + bytes * 8 / network.kilobitsPerSecond);
    }

    private Network networkOf(String documentId) {
        final int at = documentId.indexOf('@');
        final Network named = at > 0 ? Network.forId(documentId.substring(0, at)) : null;
        return named != null ? named : chosenNetwork(getContext());
    }

    /** The document id without its network. */
    private static String pathOf(String documentId) {
        return documentId.substring(documentId.indexOf('@') + 1);
    }

    private static int depthOf(String documentId) {
        int depth = 0;
        for (int i = 0; i < documentId.length(); ++i) {
            if (documentId.charAt(i) == '/') {
                ++depth;
            }
        }
        return depth;
    }

    /** The i of a photo's IMG_i.jpg, or -1 for a folder. */
    private static int photoIndex(String documentId) {
        final int start = documentId.lastIndexOf("/IMG_");
        if (start < 0 || !documentId.endsWith(".jpg")) {
            return -1;
        }
        try {
            return Integer.parseInt(documentId.substring(start + 5, documentId.length() - 4));
        } catch (NumberFormatException error) {
            return -1;
        }
    }

    private static boolean hasThumbnail(int photoIndex) {
        return photoIndex >= 0 && photoIndex % 4 <= 1;
    }

    /**
     * A landscape JPEG edge pixels wide with a red top left corner, written
     * once for each edge.
     */
    private synchronized File thumbnailFile(int edge) throws FileNotFoundException {
        File file = new File(getContext().getCacheDir(), "fakecloud-thumbnail-" + edge + ".jpg");
        if (file.length() > 0) {
            return file;
        }
        final int height = edge * 3 / 4;
        Bitmap bitmap = Bitmap.createBitmap(edge, height, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        Paint paint = new Paint();
        paint.setShader(new LinearGradient(0, 0, edge, height, Color.rgb(40, 90, 160), Color.rgb(230, 200, 120),
                Shader.TileMode.CLAMP));
        canvas.drawRect(0, 0, edge, height, paint);
        paint.setShader(null);
        paint.setColor(Color.RED);
        canvas.drawRect(0, 0, edge / 4f, height / 4f, paint);
        ByteArrayOutputStream encoded = new ByteArrayOutputStream();
        bitmap.compress(Bitmap.CompressFormat.JPEG, 85, encoded);
        bitmap.recycle();
        try (FileOutputStream out = new FileOutputStream(file)) {
            encoded.writeTo(out);
        } catch (IOException error) {
            file.delete();
            throw new FileNotFoundException("Could not write " + file + ": " + error);
        }
        return file;
    }

    private static void addRow(MatrixCursor cursor, String documentId) {
        final int index = photoIndex(documentId);
        final boolean photo = index >= 0;
        final String path = pathOf(documentId);
        cursor.newRow()
            .add(Document.COLUMN_DOCUMENT_ID, documentId)
            .add(Document.COLUMN_DISPLAY_NAME, path.substring(path.lastIndexOf('/') + 1))
            .add(Document.COLUMN_MIME_TYPE, photo ? "image/jpeg" : Document.MIME_TYPE_DIR)
            .add(Document.COLUMN_LAST_MODIFIED, 1700000000000L + Math.floorMod(path.hashCode(), 86400000))
            .add(Document.COLUMN_FLAGS, hasThumbnail(index) ? Document.FLAG_SUPPORTS_THUMBNAIL : 0)
            .add(Document.COLUMN_SIZE, photo ? 3092L : null);
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
    }
}

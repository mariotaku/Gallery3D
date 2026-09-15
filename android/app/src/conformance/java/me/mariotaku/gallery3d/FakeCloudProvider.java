package me.mariotaku.gallery3d;

import android.database.Cursor;
import android.database.MatrixCursor;
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

import java.io.FileNotFoundException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * A documents provider shaped like a large cloud drive, for the conformance
 * tests of the folder tree walk.
 *
 * Three levels of ten folders under the root, 1110 folders in all, and twenty
 * photos in each of the thousand at the bottom. Every child query answers a
 * millisecond late. About one bottom folder in fifty answers the first time the
 * way cloud providers do: half of its photos, marked EXTRA_LOADING, and a change
 * notification once the rest are "fetched". No photo can be opened; the walk
 * never opens one.
 */
public class FakeCloudProvider extends DocumentsProvider {

    static final String AUTHORITY = "me.mariotaku.gallery3d.conformance.fakecloud";
    static final String ROOT_DOCUMENT = "root";

    private static final int FANOUT = 10;
    private static final int LEVELS = 3;
    private static final int PHOTOS = 20;
    private static final long LATENCY_MS = 1;
    private static final long LOADING_MS = 40;

    private static final String[] ROOT_COLUMNS = {
        Root.COLUMN_ROOT_ID, Root.COLUMN_FLAGS, Root.COLUMN_TITLE, Root.COLUMN_DOCUMENT_ID,
    };
    private static final String[] DOCUMENT_COLUMNS = {
        Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
        Document.COLUMN_LAST_MODIFIED, Document.COLUMN_FLAGS, Document.COLUMN_SIZE,
    };

    /** When each loading folder was first asked for, so later queries answer whole. */
    private final Map<String, Long> mFirstAsked = new ConcurrentHashMap<>();
    private final Handler mHandler = new Handler(Looper.getMainLooper());

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
    public Cursor queryDocument(String documentId, String[] projection) {
        MatrixCursor result = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        addRow(result, documentId);
        return result;
    }

    @Override
    public Cursor queryChildDocuments(String parentId, String[] projection, String sortOrder)
            throws FileNotFoundException {
        SystemClock.sleep(LATENCY_MS);
        MatrixCursor result = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        final int depth = depthOf(parentId);
        if (depth < LEVELS) {
            for (int i = 0; i < FANOUT; ++i) {
                addRow(result, parentId + "/" + (char) ('A' + depth) + i);
            }
            return result;
        }

        int shown = PHOTOS;
        if (Math.floorMod(parentId.hashCode(), 50) == 0) {
            long now = SystemClock.uptimeMillis();
            Long first = mFirstAsked.putIfAbsent(parentId, now);
            if (first == null || now - first < LOADING_MS) {
                shown = PHOTOS / 2;
                Bundle extras = new Bundle();
                extras.putBoolean(DocumentsContract.EXTRA_LOADING, true);
                result.setExtras(extras);
                final Uri notify = DocumentsContract.buildChildDocumentsUri(AUTHORITY, parentId);
                result.setNotificationUri(getContext().getContentResolver(), notify);
                if (first == null) {
                    mHandler.postDelayed(() -> getContext().getContentResolver().notifyChange(notify, null),
                            LOADING_MS);
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
        throw new FileNotFoundException("The fake cloud serves listings only");
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

    private static void addRow(MatrixCursor cursor, String documentId) {
        final boolean photo = documentId.endsWith(".jpg");
        cursor.newRow()
            .add(Document.COLUMN_DOCUMENT_ID, documentId)
            .add(Document.COLUMN_DISPLAY_NAME, documentId.substring(documentId.lastIndexOf('/') + 1))
            .add(Document.COLUMN_MIME_TYPE, photo ? "image/jpeg" : Document.MIME_TYPE_DIR)
            .add(Document.COLUMN_LAST_MODIFIED, 1700000000000L + Math.floorMod(documentId.hashCode(), 86400000))
            .add(Document.COLUMN_FLAGS, 0)
            .add(Document.COLUMN_SIZE, photo ? 3092L : null);
    }
}

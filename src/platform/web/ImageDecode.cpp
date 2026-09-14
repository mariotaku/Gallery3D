#include "graphics/ImageDecode.h"

#include <SDL3/SDL.h>

#include <emscripten/emscripten.h>

#include <memory>

namespace {

// What one in-flight decode needs to survive until the browser answers. The
// bytes have to stay put because the Blob is built over wasm memory, and the
// callback has to outlive the call that started it.
struct PendingDecode {
    std::vector<uint8_t> bytes;
    int maxEdge = 0;
    ImageDecode::Callback done;
    // Where the JS side writes the decoded pixels.
    Bitmap bitmap;
};

// The pixels of a decoded picture of width by height go here, straight from
// the canvas that read them. Null when the bitmap cannot be allocated.
extern "C" EMSCRIPTEN_KEEPALIVE uint8_t *gallery3dDecodeTarget(void *handle, int width, int height) {
    auto *pending = (PendingDecode *)handle;
    pending->bitmap = Bitmap(width, height);
    return pending->bitmap.valid() ? pending->bitmap.pixels() : nullptr;
}

// Handed back from JS once the pixels are in the target, or with written false
// when the browser could not decode them.
extern "C" EMSCRIPTEN_KEEPALIVE void gallery3dDecodeDone(void *handle, int written) {
    std::unique_ptr<PendingDecode> pending((PendingDecode *)handle);
    Bitmap bitmap;
    if (written != 0) {
        bitmap = std::move(pending->bitmap);
        const std::vector<uint8_t> &bytes = pending->bytes;
        if (bytes.size() > 2 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
            // A JPEG has no alpha, so it is opaque and premultiplied as it is.
            bitmap.markOpaque();
        } else {
            // The canvas hands out straight alpha.
            bitmap.premultiply();
        }
    }
    if (pending->done) {
        pending->done(std::move(bitmap));
    }
}

}  // namespace

namespace ImageDecode {

bool isAsynchronous() {
    return true;
}

void decode(std::vector<uint8_t> bytes, int maxEdge, Callback done) {
    if (bytes.empty()) {
        if (done) {
            done(Bitmap());
        }
        return;
    }
    auto *pending = new PendingDecode{std::move(bytes), maxEdge, std::move(done), Bitmap()};

    // createImageBitmap decodes and downsizes asynchronously. Read pixels through
    // OffscreenCanvas because ImageBitmap has no direct pixel-read API.
    MAIN_THREAD_ASYNC_EM_ASM({
        var handle = $0;
        var pointer = $1;
        var length = $2;
        var maxEdge = $3;
        // A copy, because the wasm heap may be detached and regrown while the
        // decode is in flight and the Blob would be looking at the old one.
        var data = HEAPU8.slice(pointer, pointer + length);
        createImageBitmap(new Blob([data]))
            .then(function (image) {
                var width = image.width;
                var height = image.height;
                if (maxEdge > 0 && (width > maxEdge || height > maxEdge)) {
                    var scale = Math.min(maxEdge / width, maxEdge / height);
                    width = Math.max(1, Math.round(width * scale));
                    height = Math.max(1, Math.round(height * scale));
                }
                var canvas = new OffscreenCanvas(width, height);
                var context = canvas.getContext('2d', { willReadFrequently: true });
                context.drawImage(image, 0, 0, width, height);
                image.close();
                var pixels = context.getImageData(0, 0, width, height).data;
                var target = _gallery3dDecodeTarget(handle, width, height);
                if (target) {
                    HEAPU8.set(pixels, target);
                }
                _gallery3dDecodeDone(handle, target ? 1 : 0);
            })
            .catch(function (error) {
                console.error('decode failed', error);
                _gallery3dDecodeDone(handle, 0);
            });
    }, (int)(intptr_t)pending, (int)(intptr_t)pending->bytes.data(), (int)pending->bytes.size(), maxEdge);
}

}  // namespace ImageDecode

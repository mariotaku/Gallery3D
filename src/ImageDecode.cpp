#include "ImageDecode.h"

#include <SDL3/SDL.h>

#if defined(__EMSCRIPTEN__)

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
};

// Handed back from JS once the pixels are in memory. `pixels` is a buffer the
// JS side allocated with malloc and this side owns from here on.
extern "C" EMSCRIPTEN_KEEPALIVE void gallery3dDecodeDone(void *handle, uint8_t *pixels, int width,
                                                         int height) {
    std::unique_ptr<PendingDecode> pending((PendingDecode *)handle);
    Bitmap bitmap;
    if (pixels != nullptr && width > 0 && height > 0) {
        // Convert browser straight-alpha pixels to premultiplied Bitmap storage.
        bitmap = Bitmap::fromStraightRGBA(pixels, width, height);
        free(pixels);
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
    auto *pending = new PendingDecode{std::move(bytes), maxEdge, std::move(done)};

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
                var buffer = _malloc(pixels.length);
                HEAPU8.set(pixels, buffer);
                _gallery3dDecodeDone(handle, buffer, width, height);
            })
            .catch(function (error) {
                console.error('decode failed', error);
                _gallery3dDecodeDone(handle, 0, 0, 0);
            });
    }, (int)(intptr_t)pending, (int)(intptr_t)pending->bytes.data(), (int)pending->bytes.size(), maxEdge);
}

}  // namespace ImageDecode

#else

namespace ImageDecode {

bool isAsynchronous() {
    return false;
}

void decode(std::vector<uint8_t> bytes, int maxEdge, Callback done) {
    // SDL_image decodes on the calling thread and answers before returning.
    Bitmap bitmap = Bitmap::loadFromMemory(bytes.data(), bytes.size(), maxEdge);
    if (done) {
        done(std::move(bitmap));
    }
}

}  // namespace ImageDecode

#endif

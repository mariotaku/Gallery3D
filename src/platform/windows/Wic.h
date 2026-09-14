// What the Windows image code shares: WIC's factory, COM pointers, and copying
// a WIC source into a Bitmap.
#pragma once

#include <cstddef>

#include <windows.h>
// After windows.h, which it depends on.
#include <wincodec.h>

#include "graphics/Bitmap.h"

namespace Wic {

// A COM pointer, released when it goes out of scope.
template <typename T>
class Ptr {
  public:
    Ptr() = default;
    Ptr(const Ptr &) = delete;
    Ptr &operator=(const Ptr &) = delete;

    Ptr(Ptr &&other) noexcept : mPointer(other.mPointer) {
        other.mPointer = nullptr;
    }

    Ptr &operator=(Ptr &&other) noexcept {
        if (this != &other) {
            reset();
            mPointer = other.mPointer;
            other.mPointer = nullptr;
        }
        return *this;
    }

    ~Ptr() {
        reset();
    }

    void reset() {
        if (mPointer != nullptr) {
            mPointer->Release();
            mPointer = nullptr;
        }
    }

    T *get() const {
        return mPointer;
    }

    T *operator->() const {
        return mPointer;
    }

    // Releases what is held and hands out the slot for a call that fills it.
    T **put() {
        reset();
        return &mPointer;
    }

    explicit operator bool() const {
        return mPointer != nullptr;
    }

  private:
    T *mPointer = nullptr;
};

// The imaging factory for the calling thread, with COM started on the thread
// first. Null when WIC cannot be reached.
IWICImagingFactory *factory();

// A decoder reading encoded bytes in place. The bytes have to outlive it.
Ptr<IWICBitmapDecoder> decoderFor(const void *bytes, size_t size);

// A source's pixels at its own size, or a rectangle of them, as a premultiplied
// BGRA Bitmap. Invalid when WIC cannot convert them.
Bitmap copy(IWICBitmapSource *source, const WICRect *rect);

// Whether the source's pixel format carries alpha.
bool hasAlpha(IWICBitmapSource *source);

// The colour profile a frame's pixels are stored in, for inSrgb. Null when the
// frame names none or names sRGB, and either way its pixels are taken as sRGB.
Ptr<IWICColorContext> colorProfileOf(IWICBitmapFrameDecode *frame);

// The source converted from profile to sRGB, as straight BGRA. The source
// itself when profile is null or WIC cannot convert from it, so a photo whose
// profile WIC does not understand still shows.
Ptr<IWICBitmapSource> inSrgb(IWICBitmapSource *source, IWICColorContext *profile);

// The bitmap resized with WIC's Fant filter, which averages every pixel it
// shrinks away rather than sampling a few. BGRA, whatever order it came in.
Bitmap scaled(const Bitmap &bitmap, int width, int height);

// Whether an image stored beside a frame, such as its thumbnail, has the
// frame's shape to within 2%. A 4:3 thumbnail with bars stored for a 3:2 photo
// would squash the picture it stands in for.
bool sameShape(UINT width, UINT height, UINT frameWidth, UINT frameHeight);

}  // namespace Wic

// Thumbnails through the shell. IShellItemImageFactory answers from Explorer's
// thumbnail cache, and on a miss asks the format's thumbnail handler, which
// writes what it makes back to the cache.
#include "graphics/SystemThumbnail.h"

#include <algorithm>

#include "media/PhotoLibrary.h"
#include "platform/windows/Wic.h"

// After windows.h, which Wic.h includes and these depend on. initguid.h makes
// propkey.h define the property keys here rather than only declare them.
#include <shobjidl.h>
#include <initguid.h>
#include <propkey.h>

namespace {

std::wstring wideOf(const std::string &utf8) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (length <= 1) {
        return std::wstring();
    }
    std::wstring wide((size_t)length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), length);
    wide.resize((size_t)length - 1);
    return wide;
}

// A GDI bitmap, deleted when it goes out of scope.
struct GdiBitmap {
    ~GdiBitmap() {
        if (handle != nullptr) {
            DeleteObject(handle);
        }
    }

    HBITMAP handle = nullptr;
};

}  // namespace

bool SystemThumbnail::supported() {
    return true;
}

Bitmap SystemThumbnail::load(const std::string &path, int maxEdge) {
    // The factory comes first: asking for it starts COM on the thread, which
    // the shell needs as well.
    IWICImagingFactory *imaging = Wic::factory();
    const std::wstring wide = wideOf(path);
    Wic::Ptr<IShellItem2> item;
    Wic::Ptr<IShellItemImageFactory> images;
    if (imaging == nullptr || maxEdge <= 0 || wide.empty() ||
        FAILED(SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_PPV_ARGS(item.put()))) ||
        FAILED(item->QueryInterface(IID_PPV_ARGS(images.put())))) {
        return Bitmap();
    }

    // The cache keeps a few sizes and hands back the next one up, which is
    // reduced below. Making a thumbnail reads the file, so one kept online
    // only gets what the cache has or nothing.
    SIIGBF flags = SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK;
    if (PhotoLibrary::isOnlineOnly(path)) {
        flags |= SIIGBF_INCACHEONLY;
    }
    GdiBitmap upright;
    Wic::Ptr<IWICBitmap> copied;
    Wic::Ptr<IWICBitmapSource> source;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(images->GetImage(SIZE{maxEdge, maxEdge}, flags, &upright.handle)) || upright.handle == nullptr ||
        FAILED(imaging->CreateBitmapFromHBITMAP(upright.handle, nullptr, WICBitmapUsePremultipliedAlpha,
                                                copied.put())) ||
        FAILED(copied->QueryInterface(IID_PPV_ARGS(source.put()))) || FAILED(source->GetSize(&width, &height)) ||
        width == 0 || height == 0 || std::max(width, height) < (UINT)maxEdge) {
        return Bitmap();
    }
    Bitmap bitmap = Wic::copy(source.get(), nullptr);
    if (!bitmap.valid()) {
        return Bitmap();
    }

    // A handler that makes its thumbnail without alpha can leave the channel
    // at zero, which would draw nothing. Nothing in such a picture is meant to
    // be transparent. The same pass finds a thumbnail that is opaque already,
    // so the upload does not read it again.
    uint8_t *pixel = bitmap.pixels();
    const uint8_t *end = pixel + (size_t)bitmap.width() * bitmap.height() * 4;
    bool anyAlpha = false;
    bool opaque = true;
    for (const uint8_t *p = pixel; p < end && (opaque || !anyAlpha); p += 4) {
        anyAlpha = anyAlpha || p[3] != 0;
        opaque = opaque && p[3] == 255;
    }
    if (!anyAlpha) {
        for (; pixel < end; pixel += 4) {
            pixel[3] = 255;
        }
    }
    if (!anyAlpha || opaque) {
        bitmap.markOpaque();
    }

    // A format with no orientation tag fails to answer, which is upright.
    ULONG orientation = 1;
    item->GetUInt32(PKEY_Photo_Orientation, &orientation);
    // Reduced by the sample maxEdge gives, picking pixels as a decode of a
    // format with no reduction of its own does, then turned back to how the
    // photo's pixels are stored, since the shell hands thumbnails out upright.
    return bitmap
        .sampledFromWhole(Sampling::Picked, Bitmap::sampleSizeFor(bitmap.width(), bitmap.height(), maxEdge))
        .toStoredOrientation((int)orientation);
}

// Regions through WIC.
//
// JPEG's codec crops and reduces while it decodes, so every tile is its own
// small decode. A RAW photo's tiles come from the full-size JPEG preview the
// camera stored beside the raw data. Other codecs, HEIF's among them, decode at
// full size whatever size is asked for, so for those the whole picture is
// decoded once and every reduced tile is taken from it, the way Android's
// decoder reduces the format.
#include "graphics/RegionDecoder.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

#include "platform/windows/Wic.h"

bool RegionDecoder::looksSupported(const std::string &mimeType) {
    // open() decides for certain. These three decode row after row from the
    // top, which leaves a crop little faster than the screennail already shown.
    return mimeType.rfind("image/", 0) == 0 && mimeType != "image/png" && mimeType != "image/gif" &&
           mimeType != "image/bmp";
}

void RegionDecoder::initAndroid() {
}

namespace {

class WicRegionDecoder : public RegionDecoder {
  public:
    ~WicRegionDecoder() override {
        // The last tile may be released on a thread that never decoded, and
        // releasing WIC objects wants COM up on it.
        Wic::factory();
    }

    bool open(const std::string &path);

  protected:
    Bitmap decode(int x, int y, int width, int height, int sampleSize, Bitmap::Size size) override;

  private:
    // A rectangle of source, or all of it when rect is null, in sRGB and
    // marked opaque the way the frame's format says.
    Bitmap copyOf(IWICBitmapSource *source, const WICRect *rect) const;

    // The whole picture at full size, decoded the first time a reduced tile
    // wants it. Invalid if it cannot be decoded.
    const Bitmap &whole();

    // The encoded file, which the decoder reads in place, so it is declared
    // first to be destroyed last.
    std::vector<uint8_t> mBytes;
    Wic::Ptr<IWICBitmapDecoder> mDecoder;
    Wic::Ptr<IWICBitmapFrameDecode> mFrame;
    // What the frame's colours mean, so every tile is converted to sRGB the
    // way the screennail is. Null for a frame already in sRGB.
    Wic::Ptr<IWICColorContext> mProfile;
    // The frame, or for RAW its preview.
    Wic::Ptr<IWICBitmapSource> mSource;
    UINT mSourceWidth = 0;
    UINT mSourceHeight = 0;
    // Whether mSource is a JPEG frame, whose codec reduces a tile itself.
    bool mCodecReduces = false;
    // Whether mSource is the frame, rather than a RAW photo's preview.
    bool mFromFrame = false;
    // Whether mSource's format has no alpha, so every tile is marked opaque.
    bool mOpaque = false;
    // Whether mSource holds CMYK with no profile to convert it through, so its
    // pixels are converted by Cmyk::toPixels as a whole decode's are.
    bool mCmyk = false;
    Bitmap mWhole;
    // WIC objects are not promised to be safe across threads, and one frame
    // decoding tile after tile continues from where the last tile left off.
    std::mutex mMutex;
};

bool WicRegionDecoder::open(const std::string &path) {
    // Read whole, so the photo's file is not held open while it is zoomed.
    // WIC would hand out grey for what a file cut short is missing.
    if (!Bitmap::readFile(path, &mBytes) || Bitmap::endsEarly(mBytes.data(), mBytes.size())) {
        return false;
    }
    mDecoder = Wic::decoderFor(mBytes.data(), mBytes.size());
    GUID container;
    if (!mDecoder || FAILED(mDecoder->GetContainerFormat(&container)) || container == GUID_ContainerFormatPng ||
        container == GUID_ContainerFormatGif || container == GUID_ContainerFormatBmp ||
        container == GUID_ContainerFormatIco) {
        return false;
    }
    UINT width = 0;
    UINT height = 0;
    if (FAILED(mDecoder->GetFrame(0, mFrame.put())) || FAILED(mFrame->GetSize(&width, &height)) || width == 0 ||
        height == 0) {
        return false;
    }
    mWidth = (int)width;
    mHeight = (int)height;
    mProfile = Wic::colorProfileOf(mFrame.get());
    mSampling = Bitmap::samplingOf(mBytes.data(), mBytes.size());

    Wic::Ptr<IWICBitmapSourceTransform> transform;
    if (FAILED(mFrame->QueryInterface(IID_PPV_ARGS(transform.put())))) {
        Wic::Ptr<IWICBitmapSource> preview;
        UINT previewWidth = 0;
        UINT previewHeight = 0;
        // At least half the frame, or the tiles would be no sharper than the
        // screennail.
        if (SUCCEEDED(mDecoder->GetPreview(preview.put())) &&
            SUCCEEDED(preview->GetSize(&previewWidth, &previewHeight)) && previewWidth * 2 >= width &&
            previewHeight * 2 >= height) {
            mSource = std::move(preview);
            mSourceWidth = previewWidth;
            mSourceHeight = previewHeight;
        }
    }
    if (!mSource) {
        if (FAILED(mFrame->QueryInterface(IID_PPV_ARGS(mSource.put())))) {
            return false;
        }
        mSourceWidth = width;
        mSourceHeight = height;
        mFromFrame = true;
        mCodecReduces = container == GUID_ContainerFormatJpeg && transform;
    }

    mOpaque = !Wic::hasAlpha(mSource.get());
    mCmyk = !mProfile && Wic::isCmyk(mSource.get());
    return true;
}

Bitmap WicRegionDecoder::copyOf(IWICBitmapSource *source, const WICRect *rect) const {
    Bitmap bitmap = mCmyk ? Wic::copyInks(source, rect) : Wic::copy(Wic::inSrgb(source, mProfile.get()).get(), rect);
    if (!bitmap.valid()) {
        return Bitmap();
    }
    if (mOpaque) {
        bitmap.markOpaque();
    } else {
        // WIC hands some formats out with alpha whether or not the file uses
        // it, WebP among them.
        bitmap.markOpaqueUnlessTransparent();
    }
    return bitmap;
}

const Bitmap &WicRegionDecoder::whole() {
    if (!mWhole.valid()) {
        mWhole = copyOf(mSource.get(), nullptr);
    }
    return mWhole;
}

Bitmap WicRegionDecoder::decode(int x, int y, int width, int height, int sampleSize, Bitmap::Size size) {
    if (Wic::factory() == nullptr) {
        return Bitmap();
    }

    std::lock_guard<std::mutex> lock(mMutex);

    if (mFromFrame && sampleSize == 1) {
        const WICRect rect = {x, y, width, height};
        return copyOf(mSource.get(), &rect);
    }

    if (mCodecReduces) {
        // The JPEG codec reduces by a half, a quarter or an eighth, rounding
        // up, and a sample past that is picked from an eighth. The window is
        // Skia's: the whole reduced picture for the whole rectangle, and
        // otherwise the rectangle divided and rounded down.
        const int native = std::min(sampleSize, 8);
        const bool all = x == 0 && y == 0 && width == mWidth && height == mHeight;
        const Bitmap::Size window = all ? Bitmap::sampledSize(Sampling::Jpeg, width, height, native)
                                        : Bitmap::sampledRegionSize(width, height, native);
        const WICRect rect = {x / native, y / native, window.width, window.height};
        const Wic::Ptr<IWICBitmap> reduced = Wic::reducedByCodec(mFrame.get(), (UINT)native, &rect);
        const Bitmap tile = reduced ? copyOf(reduced.get(), nullptr) : Bitmap();
        return tile.valid() ? tile.picked(size.width, size.height) : Bitmap();
    }

    if (mFromFrame) {
        const Bitmap &picture = whole();
        const Bitmap part = picture.valid() ? picture.cropped(x, y, width, height) : Bitmap();
        if (!part.valid()) {
            return Bitmap();
        }
        return mSampling == Sampling::Rescaled ? part.scaled(size.width, size.height)
                                               : part.picked(size.width, size.height);
    }

    // A RAW photo's preview is a size of its own, so its rectangle is scaled
    // to the tile. Android has no decoder for these.
    const double toSourceX = (double)mSourceWidth / (double)mWidth;
    const double toSourceY = (double)mSourceHeight / (double)mHeight;
    WICRect rect;
    rect.X = std::min((INT)mSourceWidth - 1, (INT)(x * toSourceX));
    rect.Y = std::min((INT)mSourceHeight - 1, (INT)(y * toSourceY));
    rect.Width = std::max(1, std::min((INT)mSourceWidth, (INT)std::ceil((x + width) * toSourceX)) - rect.X);
    rect.Height = std::max(1, std::min((INT)mSourceHeight, (INT)std::ceil((y + height) * toSourceY)) - rect.Y);
    const Bitmap tile = copyOf(mSource.get(), &rect);
    return tile.valid() ? tile.scaled(size.width, size.height) : Bitmap();
}

}  // namespace

RegionDecoderPtr RegionDecoder::open(const std::string &path) {
    auto decoder = std::make_shared<WicRegionDecoder>();
    if (!decoder->open(path)) {
        return nullptr;
    }
    return decoder;
}

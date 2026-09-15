// Regions through WIC.
//
// JPEG's codec crops and reduces while it decodes, so every tile is its own
// small decode. A RAW photo's tiles come from the full-size JPEG preview the
// camera stored beside the raw data, which behaves the same. Other codecs,
// HEIF's among them, decode at full size whatever size is asked for: a reduced
// tile from the HEIF codec costs as much as the whole picture. For those, a
// reduced tile is cut from the whole picture decoded once at that level.
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

// How much narrower than a tile level an image may be and still serve that
// level. The tile grid picks the level whose width first covers the picture
// on screen, so an image a little short of it still has about a pixel for
// every pixel shown.
bool nearlyCovers(UINT width, UINT wanted) {
    return (unsigned long long)width * 10 >= (unsigned long long)wanted * 9;
}

class WicRegionDecoder : public RegionDecoder {
  public:
    ~WicRegionDecoder() override {
        // The last tile may be released on a thread that never decoded, and
        // releasing WIC objects wants COM up on it.
        Wic::factory();
    }

    bool open(const std::string &path);

  protected:
    Bitmap decode(int x, int y, int width, int height, int outWidth, int outHeight) override;

  private:
    // The whole picture at no less than 1/sample of its size, decoding it if
    // what is held is too small. Invalid if it cannot be decoded.
    const Bitmap &levelFor(int sample);

    // A rectangle of source, or all of it when rect is null, in sRGB and
    // marked opaque the way the frame's format says.
    Bitmap copyOf(IWICBitmapSource *source, const WICRect *rect) const;

    // The rectangle of the frame reduced to 1/sample by Wic::reducedByCodec.
    // Invalid when the codec has no such size, and the caller scales instead.
    Bitmap reducedByCodec(int left, int top, int right, int bottom, int sample) const;

    // The encoded file, which the decoder reads in place, so it is declared
    // first to be destroyed last.
    std::vector<uint8_t> mBytes;
    Wic::Ptr<IWICBitmapDecoder> mDecoder;
    Wic::Ptr<IWICBitmapFrameDecode> mFrame;
    // What the frame's colours mean, so every level and tile is converted to
    // sRGB the way the screennail is. Null for a frame already in sRGB.
    Wic::Ptr<IWICColorContext> mProfile;
    // The frame, or for RAW its preview.
    Wic::Ptr<IWICBitmapSource> mSource;
    // Whether mSource is a JPEG frame, whose codec reduces a tile itself.
    bool mCodecReduces = false;
    UINT mSourceWidth = 0;
    UINT mSourceHeight = 0;
    // Whether mSource reduces while it decodes, which JPEG does.
    bool mReducesWhileDecoding = false;
    // Whether mSource's format has no alpha, so every level and tile is marked
    // opaque.
    bool mOpaque = false;
    // Whether mSource holds CMYK with no profile to convert it through, so its
    // pixels are converted by Cmyk::toPixels as a whole decode's are.
    bool mCmyk = false;
    // The frame's embedded thumbnail in the frame's shape, if it has one. A
    // HEIF photo's is about a quarter of its width.
    Wic::Ptr<IWICBitmapSource> mThumbnail;
    UINT mThumbnailWidth = 0;
    // For a codec that does not reduce while decoding: the finest level it has
    // been decoded at, which serves every coarser one too.
    Bitmap mLevel;
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
            // A camera's preview is a JPEG.
            mReducesWhileDecoding = true;
        }
    }
    if (!mSource) {
        if (FAILED(mFrame->QueryInterface(IID_PPV_ARGS(mSource.put())))) {
            return false;
        }
        mSourceWidth = width;
        mSourceHeight = height;
        mReducesWhileDecoding = container == GUID_ContainerFormatJpeg;
        mCodecReduces = mReducesWhileDecoding && transform;
    }

    mOpaque = !Wic::hasAlpha(mSource.get());
    mCmyk = !mProfile && Wic::isCmyk(mSource.get());

    if (!mReducesWhileDecoding) {
        Wic::Ptr<IWICBitmapSource> thumbnail;
        UINT thumbnailWidth = 0;
        UINT thumbnailHeight = 0;
        if (SUCCEEDED(mFrame->GetThumbnail(thumbnail.put())) &&
            SUCCEEDED(thumbnail->GetSize(&thumbnailWidth, &thumbnailHeight)) &&
            Wic::sameShape(thumbnailWidth, thumbnailHeight, width, height)) {
            mThumbnail = std::move(thumbnail);
            mThumbnailWidth = thumbnailWidth;
        }
    }
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

Bitmap WicRegionDecoder::reducedByCodec(int left, int top, int right, int bottom, int sample) const {
    // In the reduced pixels, whose size is rounded up.
    const INT width = (mWidth + sample - 1) / sample;
    const INT height = (mHeight + sample - 1) / sample;
    WICRect rect;
    rect.X = left / sample;
    rect.Y = top / sample;
    rect.Width = std::min(width, (right + sample - 1) / sample) - rect.X;
    rect.Height = std::min(height, (bottom + sample - 1) / sample) - rect.Y;
    const Wic::Ptr<IWICBitmap> reduced = Wic::reducedByCodec(mFrame.get(), (UINT)sample, &rect);
    return reduced ? copyOf(reduced.get(), nullptr) : Bitmap();
}

const Bitmap &WicRegionDecoder::levelFor(int sample) {
    const UINT wantedWidth = std::max(1u, (UINT)mWidth / (UINT)sample);
    const UINT wantedHeight = std::max(1u, (UINT)mHeight / (UINT)sample);
    if (mLevel.valid() && nearlyCovers((UINT)mLevel.width(), wantedWidth)) {
        return mLevel;
    }
    // The thumbnail is already decoded and a fraction of the cost, when it is
    // big enough for the level.
    if (mThumbnail && nearlyCovers(mThumbnailWidth, wantedWidth)) {
        Bitmap thumbnail = copyOf(mThumbnail.get(), nullptr);
        if (thumbnail.valid()) {
            mLevel = std::move(thumbnail);
            return mLevel;
        }
    }
    Wic::Ptr<IWICBitmapScaler> scaler;
    if (FAILED(Wic::factory()->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(mSource.get(), wantedWidth, wantedHeight, WICBitmapInterpolationModeFant))) {
        mLevel = Bitmap();
        return mLevel;
    }
    mLevel = copyOf(scaler.get(), nullptr);
    return mLevel;
}

Bitmap WicRegionDecoder::decode(int x, int y, int width, int height, int outWidth, int outHeight) {
    const int left = x;
    const int top = y;
    const int right = x + width;
    const int bottom = y + height;
    IWICImagingFactory *imaging = Wic::factory();
    if (imaging == nullptr) {
        return Bitmap();
    }

    std::lock_guard<std::mutex> lock(mMutex);

    // The whole-number reduction the tile grid asks for.
    const int sample = std::max(1, width / outWidth);

    if (sample > 1 && !mReducesWhileDecoding) {
        const Bitmap &level = levelFor(sample);
        if (!level.valid()) {
            return Bitmap();
        }
        const double toLevelX = (double)level.width() / (double)mWidth;
        const double toLevelY = (double)level.height() / (double)mHeight;
        const int levelLeft = std::min(level.width() - 1, (int)(left * toLevelX));
        const int levelTop = std::min(level.height() - 1, (int)(top * toLevelY));
        const int levelRight = std::min(level.width(), (int)std::ceil(right * toLevelX));
        const int levelBottom = std::min(level.height(), (int)std::ceil(bottom * toLevelY));
        const Bitmap part = level.cropped(levelLeft, levelTop, std::max(1, levelRight - levelLeft),
                                          std::max(1, levelBottom - levelTop));
        // A level finer than this one, or the thumbnail a little short of it,
        // leaves the part off the size asked for.
        return Wic::scaled(part, outWidth, outHeight);
    }

    if (mCodecReduces && sample > 1) {
        // The codec reduces by up to eight, and the rest is scaled here.
        const int codecSample = std::min(sample, 8);
        Bitmap tile = reducedByCodec(left, top, right, bottom, codecSample);
        if (tile.valid()) {
            return tile.scaledCovering(outWidth, outHeight, (double)width / codecSample,
                                       (double)height / codecSample);
        }
    }

    // On top of however much smaller than the frame the source already is.
    // The JPEG codec reduces by up to eight on its own. Past that WIC's scaler
    // hands CMYK out as another format, so a CMYK tile stops at an eighth and
    // is scaled the rest of the way below.
    const int codecSample = mCmyk ? std::min(sample, 8) : sample;
    // Rounded up, as libjpeg sizes a reduced scan, so a pixel of the reduced
    // source starts at a multiple of the reduction.
    const UINT scaledWidth = (mSourceWidth + (UINT)codecSample - 1) / (UINT)codecSample;
    const UINT scaledHeight = (mSourceHeight + (UINT)codecSample - 1) / (UINT)codecSample;
    const double toScaledX = (double)scaledWidth / (double)mWidth;
    const double toScaledY = (double)scaledHeight / (double)mHeight;
    WICRect rect;
    rect.X = (INT)(left * toScaledX);
    rect.Y = (INT)(top * toScaledY);
    if (rect.X >= (INT)scaledWidth || rect.Y >= (INT)scaledHeight) {
        return Bitmap();
    }
    rect.Width = std::max(1, std::min((INT)scaledWidth, (INT)std::ceil(right * toScaledX)) - rect.X);
    rect.Height = std::max(1, std::min((INT)scaledHeight, (INT)std::ceil(bottom * toScaledY)) - rect.Y);

    Bitmap tile;
    if (scaledWidth == (UINT)mWidth && scaledHeight == (UINT)mHeight) {
        tile = copyOf(mSource.get(), &rect);
    } else {
        // Scaling the whole source and copying the rectangle out of it is what
        // lets WIC pass both the reduction and the crop to the codec.
        Wic::Ptr<IWICBitmapScaler> scaler;
        if (FAILED(imaging->CreateBitmapScaler(scaler.put())) ||
            FAILED(scaler->Initialize(mSource.get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant))) {
            return Bitmap();
        }
        tile = copyOf(scaler.get(), &rect);
    }
    if (!tile.valid()) {
        return Bitmap();
    }
    if (tile.width() == outWidth && tile.height() == outHeight) {
        return tile;
    }
    // A trimmed edge tile, or a source that is not the frame's size, still
    // needs the last step to the size the caller asked for.
    return tile.scaled(outWidth, outHeight);
}

}  // namespace

RegionDecoderPtr RegionDecoder::open(const std::string &path) {
    auto decoder = std::make_shared<WicRegionDecoder>();
    if (!decoder->open(path)) {
        return nullptr;
    }
    return decoder;
}

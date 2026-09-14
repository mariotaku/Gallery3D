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

    Bitmap decodeRegion(int x, int y, int width, int height, int outWidth, int outHeight) override;

  private:
    // The whole picture at no less than 1/sample of its size, decoding it if
    // what is held is too small. Invalid if it cannot be decoded.
    const Bitmap &levelFor(int sample);

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
    UINT mSourceWidth = 0;
    UINT mSourceHeight = 0;
    // Whether mSource reduces while it decodes, which JPEG does.
    bool mReducesWhileDecoding = false;
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
    if (!Bitmap::readFile(path, &mBytes)) {
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
    }

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

const Bitmap &WicRegionDecoder::levelFor(int sample) {
    const UINT wantedWidth = std::max(1u, (UINT)mWidth / (UINT)sample);
    const UINT wantedHeight = std::max(1u, (UINT)mHeight / (UINT)sample);
    if (mLevel.valid() && nearlyCovers((UINT)mLevel.width(), wantedWidth)) {
        return mLevel;
    }
    // The thumbnail is already decoded and a fraction of the cost, when it is
    // big enough for the level.
    if (mThumbnail && nearlyCovers(mThumbnailWidth, wantedWidth)) {
        Bitmap thumbnail = Wic::copy(Wic::inSrgb(mThumbnail.get(), mProfile.get()).get(), nullptr);
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
    mLevel = Wic::copy(Wic::inSrgb(scaler.get(), mProfile.get()).get(), nullptr);
    return mLevel;
}

Bitmap WicRegionDecoder::decodeRegion(int x, int y, int width, int height, int outWidth, int outHeight) {
    if (width <= 0 || height <= 0 || outWidth <= 0 || outHeight <= 0) {
        return Bitmap();
    }
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(mWidth, x + width);
    const int bottom = std::min(mHeight, y + height);
    if (right <= left || bottom <= top) {
        return Bitmap();
    }
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

    // On top of however much smaller than the frame the source already is.
    const UINT scaledWidth = std::max(1u, mSourceWidth / (UINT)sample);
    const UINT scaledHeight = std::max(1u, mSourceHeight / (UINT)sample);
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
        tile = Wic::copy(Wic::inSrgb(mSource.get(), mProfile.get()).get(), &rect);
    } else {
        // Scaling the whole source and copying the rectangle out of it is what
        // lets WIC pass both the reduction and the crop to the codec.
        Wic::Ptr<IWICBitmapScaler> scaler;
        if (FAILED(imaging->CreateBitmapScaler(scaler.put())) ||
            FAILED(scaler->Initialize(mSource.get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant))) {
            return Bitmap();
        }
        tile = Wic::copy(Wic::inSrgb(scaler.get(), mProfile.get()).get(), &rect);
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

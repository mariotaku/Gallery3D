// Regions through WIC. A codec that crops and reduces while decoding, such as
// JPEG's or HEIF's, does the work. RAW's codec cannot, so a RAW photo's regions
// come from the full-size JPEG preview the camera stored beside the raw data.
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

    Bitmap decodeRegion(int x, int y, int width, int height, int outWidth, int outHeight) override;

  private:
    // The encoded file, which the decoder reads in place, so it is declared
    // first to be destroyed last.
    std::vector<uint8_t> mBytes;
    Wic::Ptr<IWICBitmapDecoder> mDecoder;
    Wic::Ptr<IWICBitmapFrameDecode> mFrame;
    // The frame, or for RAW its preview.
    Wic::Ptr<IWICBitmapSource> mSource;
    UINT mSourceWidth = 0;
    UINT mSourceHeight = 0;
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
    }
    return true;
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

    // The whole-number reduction the tile grid asks for, on top of however
    // much smaller than the frame the source already is.
    const int sample = std::max(1, width / outWidth);
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
        tile = Wic::copy(mSource.get(), &rect);
    } else {
        // Scaling the whole source and copying the rectangle out of it is what
        // lets WIC pass both the reduction and the crop to the codec.
        Wic::Ptr<IWICBitmapScaler> scaler;
        if (FAILED(imaging->CreateBitmapScaler(scaler.put())) ||
            FAILED(scaler->Initialize(mSource.get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant))) {
            return Bitmap();
        }
        tile = Wic::copy(scaler.get(), &rect);
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

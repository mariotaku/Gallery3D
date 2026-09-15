# Pixels and the GL they go to: bitmaps, decoding, textures, text and the
# render loop.
target_sources(gallery3d_core PRIVATE
    Bitmap.cpp
    Bitmap.h
    Canvas.cpp
    Canvas.h
    CanvasTexture.cpp
    CanvasTexture.h
    ColorProfile.h
    CrossFadingTexture.cpp
    CrossFadingTexture.h
    DrawableLoad.h
    EmbeddedProfile.cpp
    EmbeddedProfile.h
    ImageDecode.h
    Layer.h
    RegionDecoder.cpp
    RegionDecoder.h
    RenderView.cpp
    RenderView.h
    SubsampledDecode.h
    SystemThumbnail.h
    TextBackend.h
    ThumbnailCache.cpp
    ThumbnailCache.h
    Texture.cpp
    Texture.h
    TiledImage.cpp
    TiledImage.h
    gles2.cpp
    gles2.h)

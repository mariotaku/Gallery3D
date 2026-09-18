# Off Windows, where there is no WIC, region and reduced decodes go through
# libjpeg, which SDL_image already links there, and LittleCMS converts their
# colour profiles to sRGB. Wall thumbnails come from the freedesktop.org
# thumbnail cache.
if(NOT WIN32)
    target_sources(gallery3d_core PRIVATE
        ColorProfile.cpp
        IccToSrgb.cpp
        IccToSrgb.h
        RegionDecoder.cpp
        SubsampledDecode.cpp
        SystemThumbnail.cpp)
    target_link_libraries(gallery3d_core PUBLIC JPEG::JPEG PkgConfig::LCMS2)
endif()

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
        SubsampledDecode.cpp)
    # A TV fills no freedesktop.org cache. It has a thumbnail service instead,
    # which the webOS folder implements against.
    if(NOT GALLERY3D_WEBOS)
        target_sources(gallery3d_core PRIVATE SystemThumbnail.cpp)
    endif()
    # fontconfig answers which file holds the machine's interface font. iOS asks
    # CoreText instead, and a system with neither falls back to the shipped face.
    # macOS carries no fontconfig unless one was installed by hand.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG IMPORTED_TARGET fontconfig)
    if(FONTCONFIG_FOUND)
        target_sources(gallery3d_core PRIVATE SystemFont.cpp)
        target_link_libraries(gallery3d_core PUBLIC PkgConfig::FONTCONFIG)
        set(GALLERY3D_HAVE_SYSTEM_FONT ON PARENT_SCOPE)
    endif()
    if(GALLERY3D_WEBOS)
        # vcpkg builds LittleCMS for the TV and ships a CMake package for it.
        # The distros ship a pkg-config file and no package.
        target_link_libraries(gallery3d_core PUBLIC JPEG::JPEG lcms2::lcms2)
    else()
        target_link_libraries(gallery3d_core PUBLIC JPEG::JPEG PkgConfig::LCMS2)
    endif()
endif()

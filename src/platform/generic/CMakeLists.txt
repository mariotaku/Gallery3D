# Fallbacks, each compiled only for the platforms that have no implementation
# of their own.

# No network, where neither libcurl nor the browser's fetch is there.
if(NOT GALLERY3D_DESKTOP AND NOT EMSCRIPTEN)
    target_sources(gallery3d_core PRIVATE
        Http.cpp)
endif()

# SDL_image, decoding on the calling thread. The browser decodes on its own.
if(NOT EMSCRIPTEN)
    target_sources(gallery3d_core PRIVATE
        ImageDecode.cpp)
endif()

# No region or reduced decode, where neither libjpeg nor a platform decoder is
# linked.
if(NOT GALLERY3D_DESKTOP AND NOT ANDROID)
    target_sources(gallery3d_core PRIVATE
        RegionDecoder.cpp
        SubsampledDecode.cpp)
endif()

# Text through SDL_ttf, where the platform draws none of its own.
if(NOT WIN32 AND NOT ANDROID)
    target_sources(gallery3d_core PRIVATE
        TextBackend.cpp)
    target_link_libraries(gallery3d_core PUBLIC SDL3_ttf::SDL3_ttf)
endif()

# The Windows shell's library, its search index and its window frame have no
# counterpart elsewhere.
if(NOT WIN32)
    target_sources(gallery3d_core PRIVATE
        PhotoIndex.cpp
        PhotoLibrary.cpp
        WindowFrame.cpp)
endif()

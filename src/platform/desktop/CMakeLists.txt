# HTTP through libcurl, and region and reduced decodes through libjpeg, which
# SDL_image already links on these platforms.
target_sources(gallery3d_core PRIVATE
    Http.cpp
    RegionDecoder.cpp
    SubsampledDecode.cpp)

target_link_libraries(gallery3d_core PUBLIC CURL::libcurl JPEG::JPEG)

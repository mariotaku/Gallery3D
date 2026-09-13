# HTTP through libcurl on every desktop. Off Windows, where there is no WIC,
# region and reduced decodes go through libjpeg, which SDL_image already
# links there.
target_sources(gallery3d_core PRIVATE
    Http.cpp)
target_link_libraries(gallery3d_core PUBLIC CURL::libcurl)

if(NOT WIN32)
    target_sources(gallery3d_core PRIVATE
        RegionDecoder.cpp
        SubsampledDecode.cpp)
    target_link_libraries(gallery3d_core PUBLIC JPEG::JPEG)
endif()

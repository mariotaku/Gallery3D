target_sources(gallery3d_core PRIVATE
    AndroidBridge.cpp
    AndroidBridge.h
    MediaStoreDataSource.cpp
    MediaStoreDataSource.h
    RegionDecoder.cpp
    StackTrace.cpp
    SubsampledDecode.cpp
    TextBackend.cpp)

# log for SDL's own output, android for the asset manager and the JNI the media
# source goes through, and jnigraphics to read a platform bitmap's pixels.
target_link_libraries(gallery3d_core PUBLIC log android jnigraphics)

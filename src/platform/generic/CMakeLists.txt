# Fallbacks, each compiled only for the platforms that have no implementation
# of their own.

# Decoding and PNG writing through SDL_image. Windows has WIC.
if(NOT WIN32)
    target_sources(gallery3d_core PRIVATE
        BitmapDecode.cpp)
    target_link_libraries(gallery3d_core PUBLIC SDL3_image::SDL3_image)
endif()

# Drawables from the assets folder. Android resolves them through its
# resources instead.
if(NOT ANDROID)
    target_sources(gallery3d_core PRIVATE
        DrawableLoad.cpp)
endif()

# No trash and no opening a file by path on a phone.
if(ANDROID OR IOS)
    target_sources(gallery3d_core PRIVATE
        FileOperations.cpp)
endif()

# Bitmap decoding on the calling thread, everywhere.
target_sources(gallery3d_core PRIVATE
    ImageDecode.cpp)

# No colour engine of its own. Windows converts inside WIC, and Linux and
# macOS have LittleCMS.
if(NOT GALLERY3D_DESKTOP OR WIN32)
    target_sources(gallery3d_core PRIVATE
        ColorProfile.cpp)
endif()

# No region decoder, where neither libjpeg, WIC nor a platform decoder is
# linked.
if(NOT GALLERY3D_DESKTOP AND NOT ANDROID)
    target_sources(gallery3d_core PRIVATE
        RegionDecoder.cpp)
endif()

# No reduced decode apart from Bitmap's own. On Windows that is WIC reducing
# inside its codecs.
if((NOT GALLERY3D_DESKTOP AND NOT ANDROID) OR WIN32)
    target_sources(gallery3d_core PRIVATE
        SubsampledDecode.cpp)
endif()

# Text through SDL_ttf, where the platform draws none of its own. It asks
# SystemFont for the interface font first; iOS is the one platform here that
# can name it, so the rest take the fallback.
if(NOT WIN32 AND NOT ANDROID)
    target_sources(gallery3d_core PRIVATE
        TextBackend.cpp)
    if(NOT GALLERY3D_HAVE_SYSTEM_FONT)
        target_sources(gallery3d_core PRIVATE
            SystemFont.cpp)
    endif()
    target_link_libraries(gallery3d_core PUBLIC SDL3_ttf::SDL3_ttf)
endif()

# The Windows shell's library, its search index and its window frame have no
# counterpart elsewhere. webOS has a library of a kind: the TV's storage manager
# names the volumes it has attached. It has no index a scan may read, so the
# fallback stands there and the folders are walked.
if(NOT WIN32)
    if(NOT GALLERY3D_WEBOS)
        target_sources(gallery3d_core PRIVATE
            PhotoLibrary.cpp)
    endif()
    target_sources(gallery3d_core PRIVATE
        PhotoIndex.cpp
        WindowFrame.cpp)
endif()

# No system thumbnails on a phone. Windows asks the shell, and Linux and macOS
# read the freedesktop.org cache.
if(NOT WIN32 AND NOT GALLERY3D_DESKTOP)
    target_sources(gallery3d_core PRIVATE
        SystemThumbnail.cpp)
endif()

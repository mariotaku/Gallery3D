target_sources(gallery3d_core PRIVATE
    Backtrace.cpp
    BitmapDecode.cpp
    FileOperations.cpp
    LocalTime.cpp
    PhotoIndex.cpp
    PhotoLibrary.cpp
    RegionDecoder.cpp
    SystemThumbnail.cpp
    TextBackend.cpp
    Wic.cpp
    Wic.h
    WindowFrame.cpp)

# dbghelp for the stack trace, dwmapi and comctl32 for the window frame, dwrite
# for the text, and windowscodecs for WIC, which decodes every image here.
target_link_libraries(gallery3d_core PUBLIC dbghelp dwmapi comctl32 dwrite windowscodecs)

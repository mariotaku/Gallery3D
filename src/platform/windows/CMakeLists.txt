target_sources(gallery3d_core PRIVATE
    Backtrace.cpp
    FileOperations.cpp
    LocalTime.cpp
    PhotoIndex.cpp
    PhotoLibrary.cpp
    TextBackend.cpp
    WindowFrame.cpp)

# dbghelp for the stack trace, dwmapi and comctl32 for the window frame, and
# dwrite for the text.
target_link_libraries(gallery3d_core PUBLIC dbghelp dwmapi comctl32 dwrite)

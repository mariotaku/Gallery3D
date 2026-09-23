# CoreText names the file iOS draws its own interface in, which no list of
# paths could: an app may not reach into /System/Library/Fonts by a spelled-out
# path, and the names there change between releases.
target_sources(gallery3d_core PRIVATE
    SystemFont.mm)
target_link_libraries(gallery3d_core PUBLIC "-framework CoreText" "-framework Foundation")
set(GALLERY3D_HAVE_SYSTEM_FONT ON PARENT_SCOPE)

# The TV's storage manager stands in for the Pictures library: it names the
# volumes the TV has attached, so a scan does not guess at a photo directory.
#
# luna-service2 carries the call, over a connection and a glib loop of this
# app's own. Both libraries come from the SDK's sysroot and are on a TV already.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0)
target_sources(gallery3d_core PRIVATE
    Luna.cpp
    Luna.h
    PhotoLibrary.cpp
    Storage.cpp
    Storage.h
    SystemThumbnail.cpp)
target_link_libraries(gallery3d_core PUBLIC luna-service2 PkgConfig::GLIB2)

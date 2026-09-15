# Only the parts that hold still: decoding, geometry and layout arithmetic.
# Anything needing a GL context stays with the screenshot checks instead.
add_executable(gallery3d_tests
    test_albumtitle.cpp
    test_assets.cpp
    test_backdrop.cpp
    test_background.cpp
    test_bitmap.cpp
    test_bytecache.cpp
    test_caption.cpp
    test_colorprofile.cpp
    decode_fixtures.cpp
    decode_fixtures.h
    freedesktop_cache.cpp
    freedesktop_cache.h
    test_datasource.cpp
    test_decode.cpp
    test_decode_region.cpp
    test_density.cpp
    test_details.cpp
    test_embeddedprofile.cpp
    test_fileoperations.cpp
    test_focusdecor.cpp
    test_foldername.cpp
    test_json.cpp
    test_label.cpp
    test_longpress.cpp
    test_main.cpp
    test_ordering.cpp
    test_overscroll.cpp
    test_photoindex.cpp
    test_photolibrary.cpp
    test_regiondecoder.cpp
    test_runner.cpp
    test_runner.h
    test_safearea.cpp
    test_settings.cpp
    test_system_thumbnail.cpp
    test_text.cpp
    test_thumbnailcache.cpp
    test_tiles.cpp
    test_tilt.cpp
    test_time.cpp
    test_timebarpopup.cpp
    test_wheel.cpp
    test_widgets.cpp
    test_windowframe.cpp
    test_zoompan.cpp
    tests.h)

target_link_libraries(gallery3d_tests PRIVATE gallery3d_core)
target_compile_definitions(gallery3d_tests PRIVATE
    GALLERY3D_ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets"
    GALLERY3D_FIXTURE_ROOT="${CMAKE_SOURCE_DIR}/tests/fixtures")

gallery3d_add_warnings(gallery3d_tests)
gallery3d_add_symbols(gallery3d_tests)

add_test(NAME gallery3d_tests COMMAND gallery3d_tests)

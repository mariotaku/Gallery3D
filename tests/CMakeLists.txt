# Only the parts that hold still: decoding, geometry and layout arithmetic.
# Anything needing a GL context stays with the screenshot checks instead.
set(GALLERY3D_TEST_SOURCES
    decode_fixtures.cpp
    decode_fixtures.h
    fake_document_tree.cpp
    fake_document_tree.h
    fake_media_store.cpp
    fake_media_store.h
    freedesktop_cache.cpp
    freedesktop_cache.h
    test_albumtitle.cpp
    test_assets.cpp
    test_backdrop.cpp
    test_background.cpp
    test_bitmap.cpp
    test_bytecache.cpp
    test_caption.cpp
    test_colorprofile.cpp
    test_datasource.cpp
    test_decode.cpp
    test_decode_region.cpp
    test_density.cpp
    test_details.cpp
    test_document_tree.cpp
    test_embeddedprofile.cpp
    test_fileoperations.cpp
    test_focusdecor.cpp
    test_foldername.cpp
    test_json.cpp
    test_label.cpp
    test_longpress.cpp
    test_media_store.cpp
    test_ordering.cpp
    test_overscroll.cpp
    test_photoindex.cpp
    test_photolibrary.cpp
    test_regiondecoder.cpp
    test_runner.cpp
    test_runner.h
    test_safearea.cpp
    test_settings.cpp
    test_app_pause.cpp
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

if(ANDROID)
    # The Android app's conformance build type. Its main library is the runner and an
    # Android host in place of the app's main.cpp, and the fixtures ride in the
    # apk's assets. scripts/android-tests.sh runs it.
    # These two write their JPEGs with libjpeg or WIC, and Android links
    # neither. The fixture tests check the same decoders there.
    list(REMOVE_ITEM GALLERY3D_TEST_SOURCES test_colorprofile.cpp test_regiondecoder.cpp)
    # The folder tree walk against FakeCloudProvider, which only the Android
    # conformance build has.
    list(APPEND GALLERY3D_TEST_SOURCES test_cloud_tree_android.cpp)
    target_sources(gallery3d PRIVATE ${GALLERY3D_TEST_SOURCES} android_host.cpp)
    # The apk's assets folder is the asset root, which SDL reads by relative
    # path. The fixture root is only known once the host has copied them out.
    target_compile_definitions(gallery3d PRIVATE
        GALLERY3D_ASSET_ROOT="."
        GALLERY3D_FIXTURE_ROOT="fixtures")
    return()
endif()

add_executable(gallery3d_tests ${GALLERY3D_TEST_SOURCES} test_main.cpp)

target_link_libraries(gallery3d_tests PRIVATE gallery3d_core)
target_compile_definitions(gallery3d_tests PRIVATE
    GALLERY3D_ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets"
    GALLERY3D_FIXTURE_ROOT="${CMAKE_SOURCE_DIR}/tests/fixtures")

gallery3d_add_warnings(gallery3d_tests)
gallery3d_add_symbols(gallery3d_tests)

add_test(NAME gallery3d_tests COMMAND gallery3d_tests)
# A sanitizer build reads leaks to ignore from here, and any other build
# ignores the variable. Set on the test rather than in CI, so a local
# linux-sanitize run gets the same answer.
#
# The slow unwinder, because the suppression names a fontconfig function and
# the distro builds fontconfig without frame pointers: the fast one stops in
# the library and never records the function, so nothing matches. Suppressing
# all of libfontconfig instead would also hide a pattern this code forgets to
# destroy, which is allocated inside the library too. It costs a second.
set_tests_properties(gallery3d_tests PROPERTIES
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:fast_unwind_on_malloc=0")

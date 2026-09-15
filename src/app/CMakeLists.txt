# Starting the app, and what every part of it reads: settings, display
# metrics, input events and the window frame.
# The Android conformance build type puts the test runner's host where main.cpp
# goes.
if(NOT (ANDROID AND GALLERY3D_TESTS))
    target_sources(gallery3d PRIVATE
        main.cpp)
endif()

target_sources(gallery3d_core PRIVATE
    App.cpp
    App.h
    Input.h
    Settings.cpp
    Settings.h
    WindowFrame.h)

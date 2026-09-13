# Starting the app, and what every part of it reads: settings, display
# metrics, input events and the window frame.
target_sources(gallery3d PRIVATE
    main.cpp)

target_sources(gallery3d_core PRIVATE
    App.cpp
    App.h
    Input.h
    Settings.cpp
    Settings.h
    WindowFrame.h)

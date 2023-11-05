
SET(CMAKE_AUTOMOC ON)
SET(CMAKE_AUTORCC ON)
SET(CMAKE_AUTOUIC ON)

add_executable(hx-20-crt
    hx20-crt.cpp
    hx20-devices/crt/hx20-crt-dev.cpp
    hx20-devices/crt/hx20-crt-dev-gfx-cfg.cpp
    hx20-devices/crt/hx20-crt-dev-text-cfg.cpp
    hx20-devices/crt/hx20-crt-dev-gfx-cfg.ui
    hx20-devices/crt/hx20-crt-dev-text-cfg.ui
    hx20-devices/disk/hx20-disk-dev.cpp
    hx20-devices/disk/tf20-adapters.cpp
    hx20-devices/disk/tf20drivediskimage.cpp
    hx20-devices/disk/tf20drivedirectory.cpp
    hx20-devices/disk/disk-drive-adapters.cpp
    hx20-ser-proto.cpp
    mainwindow.cpp
    dockwidgettitlebar.cpp
    tools/teledisk/parser.cpp
    tools/teledisk/lzh.cpp
    comms-debug.cpp
    application.qrc
    settings.cpp
    )

target_link_libraries(hx-20-crt Qt5::Core Qt5::Gui Qt5::Widgets)

add_subdirectory(tools)

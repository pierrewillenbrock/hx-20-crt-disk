
add_executable(hx-20-crt
    hx20-crt-dev.cpp
    hx20-crt.cpp
    hx20-disk-dev.cpp
    hx20-ser-proto.cpp
    )

target_link_libraries(hx-20-crt PkgConfig::GTKMM)

add_subdirectory(tools)

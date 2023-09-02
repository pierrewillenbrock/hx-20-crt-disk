cmake_minimum_required(VERSION 3.0)

project(hx-20-crt)

find_package(PkgConfig)
find_package(Boost COMPONENTS program_options REQUIRED )

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets )

add_compile_options(-Wall)

#add_executable(hx-20-crt main.cpp)

#install(TARGETS hx-20-crt RUNTIME DESTINATION bin)
add_subdirectory(src)


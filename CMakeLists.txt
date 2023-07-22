cmake_minimum_required(VERSION 3.0)

project(hx-20-crt)

find_package(PkgConfig)
find_package(Boost COMPONENTS program_options REQUIRED )

pkg_check_modules(GTKMM gtkmm-2.4 IMPORTED_TARGET)

add_compile_options(-Wall)

#add_executable(hx-20-crt main.cpp)

#install(TARGETS hx-20-crt RUNTIME DESTINATION bin)
add_subdirectory(src)


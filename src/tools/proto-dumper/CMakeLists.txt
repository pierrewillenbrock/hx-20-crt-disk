
add_executable(hx20-crt-proto-dumper
    hx20-crt-proto.c
    )

target_link_libraries(hx20-crt-proto-dumper Boost::program_options)

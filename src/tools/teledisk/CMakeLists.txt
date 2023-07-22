
add_executable(teledisk-converter
    main.cpp
    parser.cpp
    )

target_link_libraries(teledisk-converter Boost::program_options)

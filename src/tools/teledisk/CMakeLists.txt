
add_executable(teledisk-converter
    main.cpp
    parser.cpp
    lzh.cpp
    )

target_link_libraries(teledisk-converter Boost::program_options)

add_executable(lzhtest
    lzhtest.cpp
    lzh.cpp
    )

add_executable(lzhdec
    lzhdec.cpp
    lzh.cpp
    )

add_executable(savetest
    savetest.cpp
    parser.cpp
    lzh.cpp
    )

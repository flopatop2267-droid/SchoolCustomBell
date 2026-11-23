cmake_minimum_required(VERSION 3.16)
project(SchoolBell CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(school_bell
    src/main.cpp
    src/AudioPlayer.cpp)

# third_party/ holds: httplib.h, miniaudio.h, nlohmann/json.hpp (see README)
target_include_directories(school_bell PRIVATE src third_party)

if(MSVC)
    target_compile_options(school_bell PRIVATE /utf8)   # UTF-8 source files
    target_link_libraries(school_bell PRIVATE ws2_32 ole32)
endif()

# Copy the web UI next to the executable after every build
add_custom_command(TARGET school_bell POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/web $<TARGET_FILE_DIR:school_bell>/web)

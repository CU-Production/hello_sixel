cmake_minimum_required(VERSION 3.30)
project(testsixel)

set(CMAKE_CXX_STANDARD 20)

add_executable(testsixel main.cpp)
add_custom_command(TARGET testsixel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/missing_tex.png" "${CMAKE_CURRENT_BINARY_DIR}"
        COMMAND_EXPAND_LISTS
)

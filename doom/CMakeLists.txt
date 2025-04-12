add_executable(doom main.cpp libs.c)
add_custom_command(
        TARGET doom POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/doom1.wad" "${CMAKE_CURRENT_BINARY_DIR}/doom1.wad"
        VERBATIM
)

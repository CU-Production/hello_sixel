add_executable(imageviewer main.cpp)
add_custom_command(TARGET imageviewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/missing_tex.png" "${CMAKE_CURRENT_BINARY_DIR}"
        COMMAND_EXPAND_LISTS
)

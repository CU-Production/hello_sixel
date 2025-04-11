add_executable(gbemu main.cpp)
add_custom_command(TARGET gbemu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/PokemonRed.gb" "${CMAKE_CURRENT_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/SuperMarioLand.gb" "${CMAKE_CURRENT_BINARY_DIR}"
        COMMAND_EXPAND_LISTS
)
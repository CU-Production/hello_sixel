include(FetchContent)

FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
        GIT_TAG v6.0.2
        GIT_SHALLOW 1
)

FetchContent_MakeAvailable(ftxui)

add_executable(nesemu main.cpp cpu.cpp memory.cpp NES.cpp)
target_link_libraries(nesemu PRIVATE ftxui::component ftxui::dom ftxui::screen)

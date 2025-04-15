add_executable(gbemu main.cpp minigb_apu/minigb_apu.c)
target_compile_definitions(gbemu PRIVATE MINIGB_APU_AUDIO_FORMAT_S16SYS)

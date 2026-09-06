# tinyagi-sdl — desktop SDL2 port of the tiny_agi engine.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build -j$(nproc)
#   ./build/tinyagi_sdl ~/Downloads/quest/sq1
#
# Requires: libsdl2-dev
cmake_minimum_required(VERSION 3.13)

project(tinyagi_sdl C)

set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Platform-independent engine.
add_subdirectory(../agi ${CMAKE_CURRENT_BINARY_DIR}/tinyagi)

# Sound sequencer + synth mixer core, shared verbatim with the RP2350 targets.
# (pwm_synth.c itself is the Pico PWM *output* backend and is not built here.)
set(RP2350_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tinyagi-rp2350)

add_executable(tinyagi_sdl
    main.c
    platform.c
    font.c
    ${RP2350_DIR}/audio/pwm_synth_core.c
    ${RP2350_DIR}/agi_sound_player/agi_sound.c
)

# NB: ${RP2350_DIR}/audio is deliberately NOT an include dir — it contains a
# strings.h (the synth wavetable, whose array is literally named `strings`)
# which would shadow the C library's <strings.h> that <string.h> pulls in.
# The shared headers are included by relative path from main.c/platform.c.
target_include_directories(tinyagi_sdl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(tinyagi_sdl PRIVATE tinyagi ${SDL2_LIBRARIES} m)
target_link_directories(tinyagi_sdl PRIVATE ${SDL2_LIBRARY_DIRS})
target_compile_options(tinyagi_sdl PRIVATE ${SDL2_CFLAGS_OTHER} -Wall)

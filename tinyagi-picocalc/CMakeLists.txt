cmake_minimum_required(VERSION 3.13)

# Must be set before include(pico_sdk_import.cmake) so the toolchain
# loaded during project() sees the correct board.
set(PICO_BOARD pico2)

include(pico_sdk_import.cmake)

project(tinyagi_picocalc C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# Path to the PicoCalc hardware module sources (sibling repo)
set(PICOCALC_HW ${CMAKE_CURRENT_SOURCE_DIR}/../../PicoCalc/Code/picocalc_helloworld)

add_subdirectory(${PICOCALC_HW}/i2ckbd ${CMAKE_CURRENT_BINARY_DIR}/i2ckbd)
add_subdirectory(${PICOCALC_HW}/lcdspi ${CMAKE_CURRENT_BINARY_DIR}/lcdspi)

# AGI engine library (platform-independent interpreter)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../agi ${CMAKE_CURRENT_BINARY_DIR}/tinyagi)

# FatFS sources — added directly to avoid the submodule's CMakeLists.txt which
# links hardware_rtc (RP2040-only, unavailable on RP2350/Pico 2).
# rtc.c and ff_stdio.c are excluded; get_fattime() is stubbed in sdcard.c.
set(FATFS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fatfs/FatFs_SPI)
set(FATFS_SOURCES
    ${FATFS_DIR}/ff15/source/ff.c
    ${FATFS_DIR}/ff15/source/ffunicode.c
    ${FATFS_DIR}/ff15/source/ffsystem.c
    ${FATFS_DIR}/sd_driver/sd_spi.c
    ${FATFS_DIR}/sd_driver/spi.c
    ${FATFS_DIR}/sd_driver/sd_card.c
    ${FATFS_DIR}/sd_driver/crc.c
    ${FATFS_DIR}/sd_driver/demo_logging.c
    ${FATFS_DIR}/src/glue.c
    ${FATFS_DIR}/src/f_util.c
    ${FATFS_DIR}/src/my_debug.c
)

add_executable(tinyagi_picocalc
    main.c
    display.c
    kbd_input.c
    hw_config.c
    sdcard.c
    platform.c
    audio/pwm_synth.c
    agi_sound_player/agi_sound.c
    ${FATFS_SOURCES}
)

target_include_directories(tinyagi_picocalc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}           # so agi_sound.c can find ../audio/
    ${FATFS_DIR}/ff15/source
    ${FATFS_DIR}/sd_driver
    ${FATFS_DIR}/include
)

target_link_libraries(tinyagi_picocalc
    tinyagi
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_spi
    hardware_dma
    hardware_i2c
    hardware_pwm
    hardware_irq
    i2ckbd
    lcdspi
)

pico_enable_stdio_usb(tinyagi_picocalc 1)
pico_enable_stdio_uart(tinyagi_picocalc 1)
pico_add_extra_outputs(tinyagi_picocalc)

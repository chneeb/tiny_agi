cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2)
include(pico_sdk_import.cmake)

project(tinyagi_rp2350 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# ── External libraries (PicoCalc hardware modules) ───────────────────────────
set(PICOCALC_HW ${CMAKE_CURRENT_SOURCE_DIR}/../../PicoCalc/Code/picocalc_helloworld)
add_subdirectory(${PICOCALC_HW}/i2ckbd ${CMAKE_CURRENT_BINARY_DIR}/i2ckbd)
add_subdirectory(${PICOCALC_HW}/lcdspi ${CMAKE_CURRENT_BINARY_DIR}/lcdspi)

# ── AGI engine ───────────────────────────────────────────────────────────────
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../agi ${CMAKE_CURRENT_BINARY_DIR}/tinyagi)

# ── FatFS sources ────────────────────────────────────────────────────────────
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

# ── Sources shared by all targets ────────────────────────────────────────────
set(COMMON_SOURCES
    main.c
    platform.c
    sdcard.c
    ${FATFS_SOURCES}
)

set(AUDIO_SOURCES
    audio/pwm_synth.c
    agi_sound_player/agi_sound.c
)

# ── PicoCalc target ──────────────────────────────────────────────────────────
add_executable(tinyagi_picocalc
    ${COMMON_SOURCES}
    ${AUDIO_SOURCES}
    picocalc/display.c
    picocalc/kbd_input.c
    picocalc/hw_config.c
)

target_include_directories(tinyagi_picocalc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/picocalc
    ${FATFS_DIR}/ff15/source
    ${FATFS_DIR}/sd_driver
    ${FATFS_DIR}/include
)

target_compile_definitions(tinyagi_picocalc PRIVATE
    SOUND_ENABLED=1
    KB_ENTER_CODE=0x0A
    KB_F10_CODE=0x90
    SYS_CLOCK_KHZ=133000
    USE_VREG_BOOST=0
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

# ── RESTOUCH target ──────────────────────────────────────────────────────────
add_executable(tinyagi_restouch
    ${COMMON_SOURCES}
    ${AUDIO_SOURCES}
    restouch/display.c
    restouch/lcdspi.c
    restouch/kbd_input.c
    restouch/hw_config.c
)

target_include_directories(tinyagi_restouch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/restouch
    ${FATFS_DIR}/ff15/source
    ${FATFS_DIR}/sd_driver
    ${FATFS_DIR}/include
)

target_compile_definitions(tinyagi_restouch PRIVATE
    SOUND_ENABLED=0
    KB_ENTER_CODE=0x0D
    KB_F10_CODE=0x8A
    SYS_CLOCK_KHZ=300000
    USE_VREG_BOOST=1
    # ST7789 LCD on spi1
    LCD_SPI_PORT=spi1
    LCD_PIN_DC=8
    LCD_PIN_CS=9
    LCD_PIN_CLK=10
    LCD_PIN_MOSI=11
    LCD_PIN_RST=15
    LCD_PIN_BL=13
    LCD_SPI_CLOCK_HZ=80000000
    # Touch CS parked HIGH on shared SPI bus
    TOUCH_PIN_CS=16
    # SD card on spi1
    SD_PIN_CS=22
    # M5Stack CardKB on i2c1
    KB_I2C_PORT=i2c1
    KB_PIN_SDA=2
    KB_PIN_SCL=3
)

target_link_libraries(tinyagi_restouch
    tinyagi
    pico_stdlib
    hardware_clocks
    hardware_vreg
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_pwm
    hardware_irq
)

pico_enable_stdio_usb(tinyagi_restouch 1)
pico_enable_stdio_uart(tinyagi_restouch 1)
pico_add_extra_outputs(tinyagi_restouch)

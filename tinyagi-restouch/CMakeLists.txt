cmake_minimum_required(VERSION 3.13)

# RP2350 (Pico 2) — must be set before pico_sdk_import
set(PICO_BOARD pico2)

include(pico_sdk_import.cmake)

project(tinyagi_restouch C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# ── Pin / bus configuration ──────────────────────────────────────────────────
# ST7789 320×240 on spi1 (shared with SD card and touch CS).
set(LCD_SPI_PORT  spi1)
set(LCD_PIN_DC    8)
set(LCD_PIN_CS    9)
set(LCD_PIN_CLK   10)
set(LCD_PIN_MOSI  11)
set(LCD_PIN_RST   15)
set(LCD_PIN_BL    13)
set(LCD_SPI_CLOCK_HZ 80000000)

# XPT2046 touch CS — not used, but driven HIGH to keep MISO free.
set(TOUCH_PIN_CS  16)

# SD card shares spi1: SCK=10, MOSI=11, MISO=12, CS=22.
set(SD_PIN_CS     22)

# M5Stack CardKB on i2c1 (GP2=SDA, GP3=SCL).
set(KB_I2C_PORT   i2c1)
set(KB_PIN_SDA    2)
set(KB_PIN_SCL    3)

# ── AGI engine (platform-independent library) ────────────────────────────────
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../agi ${CMAKE_CURRENT_BINARY_DIR}/tinyagi)

# ── FatFs_SPI sources (shared with picocalc — reuse the same submodule) ─────
set(FATFS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tinyagi-picocalc/fatfs/FatFs_SPI)
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

add_executable(tinyagi_restouch
    main.c
    display.c
    lcdspi.c
    kbd_input.c
    hw_config.c
    sdcard.c
    platform.c
    ${FATFS_SOURCES}
)

target_include_directories(tinyagi_restouch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FATFS_DIR}/ff15/source
    ${FATFS_DIR}/sd_driver
    ${FATFS_DIR}/include
)

target_compile_definitions(tinyagi_restouch PRIVATE
    # LCD
    LCD_SPI_PORT=${LCD_SPI_PORT}
    LCD_PIN_DC=${LCD_PIN_DC}
    LCD_PIN_CS=${LCD_PIN_CS}
    LCD_PIN_CLK=${LCD_PIN_CLK}
    LCD_PIN_MOSI=${LCD_PIN_MOSI}
    LCD_PIN_RST=${LCD_PIN_RST}
    LCD_PIN_BL=${LCD_PIN_BL}
    LCD_SPI_CLOCK_HZ=${LCD_SPI_CLOCK_HZ}
    # Touch CS (parked high on shared bus)
    TOUCH_PIN_CS=${TOUCH_PIN_CS}
    # SD (used by hw_config.c and lcdspi.c shared-bus init)
    SD_PIN_CS=${SD_PIN_CS}
    # Keyboard
    KB_I2C_PORT=${KB_I2C_PORT}
    KB_PIN_SDA=${KB_PIN_SDA}
    KB_PIN_SCL=${KB_PIN_SCL}
)

target_link_libraries(tinyagi_restouch
    tinyagi
    pico_stdlib
    hardware_clocks
    hardware_vreg
    hardware_spi
    hardware_i2c
    hardware_dma
)

pico_enable_stdio_usb(tinyagi_restouch 1)
pico_enable_stdio_uart(tinyagi_restouch 1)
pico_add_extra_outputs(tinyagi_restouch)

cmake_minimum_required(VERSION 3.13)

# Platform select. PICO_BOARD/PICO_PLATFORM is global to a configure, so the
# RP2040 and RP2350 targets can't coexist in one build. Default: the three
# RP2350 (pico2) targets. -DTINYAGI_RP2040_PIZERO=ON: only the RP2040-PiZero DVI
# target (use a separate build dir, e.g. build_rp2040/).
option(TINYAGI_RP2040_PIZERO "Build the RP2040-PiZero DVI target instead of the RP2350 targets" OFF)
if(TINYAGI_RP2040_PIZERO)
    set(PICO_BOARD pico)    # RP2040
else()
    set(PICO_BOARD pico2)   # RP2350 / Pico 2
endif()
include(pico_sdk_import.cmake)

project(tinyagi_rp2350 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# littlefs (flash game/save cache) — shared by all targets
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/littlefs)

# ── External libraries (PicoCalc hardware modules) — LCD targets only ────────
if(NOT TINYAGI_RP2040_PIZERO)
    set(PICOCALC_HW ${CMAKE_CURRENT_SOURCE_DIR}/../../PicoCalc/Code/picocalc_helloworld)
    add_subdirectory(${PICOCALC_HW}/i2ckbd ${CMAKE_CURRENT_BINARY_DIR}/i2ckbd)
    add_subdirectory(${PICOCALC_HW}/lcdspi ${CMAKE_CURRENT_BINARY_DIR}/lcdspi)
endif()

# ── AGI engine ───────────────────────────────────────────────────────────────
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../agi ${CMAKE_CURRENT_BINARY_DIR}/tinyagi)

# ── FatFS sources ────────────────────────────────────────────────────────────
set(FATFS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fatfs/FatFs_SPI)
set(FATFS_SOURCES
    ${FATFS_DIR}/ff15/source/ff.c
    ${FATFS_DIR}/ff15/source/ffunicode.c
    ${FATFS_DIR}/ff15/source/ffsystem.c
    ${FATFS_DIR}/sd_driver/sd_spi.c
    # sd_driver/spi.c is added per-target: stock for picocalc/restouch, a
    # DMA-poll patched copy (dvi/sd_spi_poll.c) for DVI — see that file. The poll
    # driver is required on DVI (keepalive alone is much worse — tested).
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
    flashfs.c
    littlefs/lfs.c
    littlefs/lfs_util.c
    ${FATFS_SOURCES}
)

set(AUDIO_SOURCES
    audio/pwm_synth.c
    agi_sound_player/agi_sound.c
)

# ══ RP2350 targets (PicoCalc, RESTOUCH, DVI) ═════════════════════════════════
if(NOT TINYAGI_RP2040_PIZERO)

# ── PicoCalc target ──────────────────────────────────────────────────────────
add_executable(tinyagi_picocalc
    ${COMMON_SOURCES}
    ${AUDIO_SOURCES}
    ${FATFS_DIR}/sd_driver/spi.c   # stock SD driver
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
    AUDIO_PIN=26
    FLASHFS_ENABLED=0             # play directly from SD (no flash cache)
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
    hardware_flash
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
    ${FATFS_DIR}/sd_driver/spi.c   # stock SD driver
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
    FLASHFS_ENABLED=1             # flash game/save cache on (faster loads + SD-optional play; the
                                  # LCD/SD baud reclaims make the shared spi1 LCD+SD bus coexist)
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
    hardware_flash
)

pico_enable_stdio_usb(tinyagi_restouch 1)
pico_enable_stdio_uart(tinyagi_restouch 1)
pico_add_extra_outputs(tinyagi_restouch)

# ── DVI target (Waveshare RP2350-PiZero) — requires pico_lib submodule ───────
# Guard: if the submodule is not checked out, this block is silently skipped
# and the picocalc/restouch targets still build normally.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/pico_lib/dvi/CMakeLists.txt)
    add_subdirectory(pico_lib/dvi  ${CMAKE_CURRENT_BINARY_DIR}/pico_lib_dvi)
    add_subdirectory(pico_lib/util ${CMAKE_CURRENT_BINARY_DIR}/pico_lib_util)

    add_executable(tinyagi_dvi
        ${COMMON_SOURCES}
        ${AUDIO_SOURCES}
        dvi/sd_spi_poll.c   # DMA-poll SD driver (replaces stock spi.c) — required on DVI
        dvi/display.cpp
        dvi/kbd_input.c
        dvi/hw_config.c
        dvi/usb_descriptors.c
    )

    target_include_directories(tinyagi_dvi PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/dvi
        ${FATFS_DIR}/ff15/source
        ${FATFS_DIR}/sd_driver
        ${FATFS_DIR}/include
        pico_lib
    )

    target_compile_definitions(tinyagi_dvi PRIVATE
        DVI_TARGET=1                   # enables DVI-only debug hooks in shared main.c
        DVI_ENABLE_CDC=0               # 1 = USB-C serial console; 0 = UART-only (crash isolation test)
        DVI_KEEPALIVE_TIMER=1          # workaround for lost-wakeup room-transition hang (confirmed)
        DVI_DOUBLE_BUFFER=1            # 1 = core1 scans a front buffer (no black flash during loads); 0 = live scan
        SOUND_ENABLED=1                # audio ON (HDMI data-island); may glitch audio-decoding monitors
        DVI_HDMI_AUDIO=1               # 1 = HDMI data-island audio (impl works; see CLAUDE.md open issue)
        KB_ENTER_CODE=0x0D
        KB_F10_CODE=0x8A
        SYS_CLOCK_KHZ=252000          # 21 x 12 MHz — PIO-USB constraint
        USE_VREG_BOOST=1
        PICO_FLASH_SIZE_BYTES=16777216 # 16 MB on the RP2350-PiZero (pico2 default is 4 MB)
        FLASHFS_ENABLED=1              # flash game/save cache — required (fixes the room-transition blanking)
        PICO_PIO_USE_GPIO_BASE=1       # DVI uses GPIO 32-39 (RP2350B extended range)
        PICO_DEFAULT_PIO_USB_DP_PIN=28 # PIO-USB D+ on GPIO 28
        PICO_STACK_SIZE=4096
        PICO_CORE1_STACK_SIZE=3072
        CFG_TUSB_CONFIG_FILE="tusb_config.h"
    )

    target_link_libraries(tinyagi_dvi
        tinyagi
        pico_stdlib
        pico_multicore
        hardware_clocks
        hardware_vreg
        hardware_spi
        hardware_dma
        hardware_pio
        hardware_pwm
        hardware_irq
        hardware_interp
        hardware_flash
        dvi
        util
        tinyusb_device
        tinyusb_host
        tinyusb_board
        tinyusb_pico_pio_usb
    )

    pico_enable_stdio_usb(tinyagi_dvi 0)   # blocked: SDK disables USB stdio when tinyusb_host is linked
    pico_enable_stdio_uart(tinyagi_dvi 1)
    pico_add_extra_outputs(tinyagi_dvi)
endif()

# ══ RP2040-PiZero DVI target ═════════════════════════════════════════════════
else()

# Reuses the entire dvi/ port; RP2040_PIZERO switches the board-specific bits
# (TMDS pins 26/24/22 clk 28, SD on spi0, native-USB keyboard host). No double
# buffer (264 KB SRAM), no HDMI audio, UART-only console.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/pico_lib/dvi/CMakeLists.txt)
    add_subdirectory(pico_lib/dvi  ${CMAKE_CURRENT_BINARY_DIR}/pico_lib_dvi)
    add_subdirectory(pico_lib/util ${CMAKE_CURRENT_BINARY_DIR}/pico_lib_util)

    add_executable(tinyagi_dvi_rp2040
        ${COMMON_SOURCES}
        ${AUDIO_SOURCES}
        dvi/sd_spi_poll.c   # DMA-poll SD driver (replaces stock spi.c)
        dvi/display.cpp
        dvi/kbd_input.c     # native-USB host path via RP2040_PIZERO guard
        dvi/hw_config.c     # SD on spi0 via RP2040_PIZERO guard
        # usb_descriptors.c omitted: no CDC device (native USB is the keyboard host)
    )

    target_include_directories(tinyagi_dvi_rp2040 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/dvi
        ${FATFS_DIR}/ff15/source
        ${FATFS_DIR}/sd_driver
        ${FATFS_DIR}/include
        pico_lib
    )

    target_compile_definitions(tinyagi_dvi_rp2040 PRIVATE
        DVI_TARGET=1
        RP2040_PIZERO=1                # board-specific pins + native-USB keyboard host
        DVI_ENABLE_CDC=0               # native USB is the HID host; console = UART only
        DVI_KEEPALIVE_TIMER=1
        DVI_DOUBLE_BUFFER=0            # 8-bit double buffer off (no room); instead:
        DVI_PACKED_FB=1               # 4-bit packed fb + scanout (32K each = 64K, same as one 8-bit
                                      # fb) → double-buffered, no flicker, at zero extra RAM
        SOUND_ENABLED=0                # HDMI audio breaks DVI timing on RP2040 (core1 can't carry
        DVI_HDMI_AUDIO=0               # TMDS + data islands); sequencer still runs for timing. TODO: PWM.
        KB_ENTER_CODE=0x0D
        KB_F10_CODE=0x8A
        SYS_CLOCK_KHZ=252000
        USE_VREG_BOOST=1
        PICO_FLASH_SIZE_BYTES=16777216 # RP2040-PiZero: 16 MB (littlefs cache = 15 MB, ~28 games)
        FLASHFS_ENABLED=1
        PICO_STACK_SIZE=4096
        PICO_CORE1_STACK_SIZE=3072
        CFG_TUSB_CONFIG_FILE="tusb_config.h"
    )

    target_link_libraries(tinyagi_dvi_rp2040
        tinyagi
        pico_stdlib
        pico_multicore
        hardware_clocks
        hardware_vreg
        hardware_spi
        hardware_dma
        hardware_pio
        hardware_pwm
        hardware_irq
        hardware_interp
        hardware_flash
        dvi
        util
        tinyusb_host
        tinyusb_board
    )

    pico_enable_stdio_usb(tinyagi_dvi_rp2040 0)
    pico_enable_stdio_uart(tinyagi_dvi_rp2040 1)
    pico_add_extra_outputs(tinyagi_dvi_rp2040)
endif()

endif()  # TINYAGI_RP2040_PIZERO

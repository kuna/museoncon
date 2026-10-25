cmake_minimum_required(VERSION 3.13)

# RP2350 (Pico 2). Use pico2_riscv to build for the Hazard3 RISC-V cores.
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)

include(pico_sdk_import.cmake)

project(bitcon VERSION 0.3.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Mode a board boots in until the configurator stores another one:
# KEYBOARD, LR2, BEATORAJA, INFINITAS or COMPOSITE. Leave unset to use
# config.h's default.
set(BITCON_OUTPUT_MODE "" CACHE STRING
    "KEYBOARD | LR2 | BEATORAJA | INFINITAS | COMPOSITE")

add_executable(bitcon
    main.c
    settings.c
    usb_descriptors.c
)

target_compile_definitions(bitcon PRIVATE
    BITCON_FW_VERSION="${PROJECT_VERSION}"
)

target_include_directories(bitcon PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

set(BITCON_MODES KEYBOARD LR2 BEATORAJA INFINITAS COMPOSITE)
if(NOT BITCON_OUTPUT_MODE STREQUAL "")
    if(NOT BITCON_OUTPUT_MODE IN_LIST BITCON_MODES)
        message(FATAL_ERROR "BITCON_OUTPUT_MODE must be one of: ${BITCON_MODES}")
    endif()
    # Expands to the matching OUT_* constant from config.h.
    target_compile_definitions(bitcon PRIVATE
        DEFAULT_OUTPUT_MODE=OUT_${BITCON_OUTPUT_MODE})
endif()

target_link_libraries(bitcon
    pico_stdlib
    pico_multicore
    pico_unique_id
    pico_bootrom
    hardware_flash
    hardware_watchdog
    tinyusb_device
    tinyusb_board
)

# We own the USB peripheral, so stdio must not try to claim it.
# Use UART if you need debug output.
pico_enable_stdio_usb(bitcon 0)
pico_enable_stdio_uart(bitcon 0)

pico_add_extra_outputs(bitcon)

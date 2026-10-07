cmake_minimum_required(VERSION 3.20)

# Default to the arm-none-eabi toolchain file unless the caller chose one
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake)
endif()

project(transtrack_sensor_manager C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

set(FW_VERSION_MAJOR 1 CACHE STRING "Application major version")
set(FW_VERSION_MINOR 1 CACHE STRING "Application minor version")
set(FW_VERSION_PATCH 0 CACHE STRING "Application patch version")

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(HAL_DIR ${TP}/stm32f4xx_hal_driver)
set(RTOS_DIR ${TP}/FreeRTOS-Kernel)
set(LD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ld)

set(STARTUP ${TP}/cmsis_device_f4/Source/Templates/gcc/startup_stm32f407xx.s)
set(SYSTEM  ${TP}/cmsis_device_f4/Source/Templates/system_stm32f4xx.c)

set(CMSIS_INCLUDES ${TP}/cmsis_core/Include ${TP}/cmsis_device_f4/Include)
set(COMMON_DEFS STM32F407xx HSE_VALUE=8000000U)

# ---------------------------------------------------------------- common (target)
set(COMMON_SRC
    common/src/crc32.c
    common/src/sha256.c
    common/src/fw_image.c
    common/src/boot_journal.c
    common/src/boot_journal_f4.c
    common/src/flash_f4.c
    common/src/syscalls_min.c)

function(firmware_outputs target)
    string(REPLACE ".elf" "" base ${target})
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> $<TARGET_FILE_DIR:${target}>/${base}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
        COMMENT "Creating ${base}.bin")
endfunction()

# ---------------------------------------------------------------- bootloader
add_executable(bootloader.elf
    bootloader/src/main.c
    ${COMMON_SRC}
    ${STARTUP}
    ${SYSTEM})
target_include_directories(bootloader.elf PRIVATE common/inc ${CMSIS_INCLUDES})
target_compile_definitions(bootloader.elf PRIVATE ${COMMON_DEFS})
target_compile_options(bootloader.elf PRIVATE -Os)
target_link_options(bootloader.elf PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/bootloader/linker/bootloader.ld -L${LD_DIR}
    -Wl,-Map=$<TARGET_FILE_DIR:bootloader.elf>/bootloader.map)
firmware_outputs(bootloader.elf)


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

# ---------------------------------------------------------------- application
set(HAL_SRC
    stm32f4xx_hal.c stm32f4xx_hal_cortex.c stm32f4xx_hal_rcc.c stm32f4xx_hal_rcc_ex.c
    stm32f4xx_hal_gpio.c stm32f4xx_hal_dma.c stm32f4xx_hal_uart.c stm32f4xx_hal_i2c.c
    stm32f4xx_hal_adc.c stm32f4xx_hal_adc_ex.c stm32f4xx_hal_rtc.c stm32f4xx_hal_rtc_ex.c
    stm32f4xx_hal_pwr.c stm32f4xx_hal_pwr_ex.c stm32f4xx_hal_tim.c stm32f4xx_hal_tim_ex.c
    stm32f4xx_hal_flash.c stm32f4xx_hal_flash_ex.c stm32f4xx_hal_exti.c)
list(TRANSFORM HAL_SRC PREPEND ${HAL_DIR}/Src/)

set(RTOS_SRC
    ${RTOS_DIR}/tasks.c ${RTOS_DIR}/list.c ${RTOS_DIR}/queue.c ${RTOS_DIR}/stream_buffer.c
    ${RTOS_DIR}/portable/GCC/ARM_CM4F/port.c ${RTOS_DIR}/portable/MemMang/heap_4.c)

set(APP_SRC
    app/src/main.c app/src/bsp.c app/src/stm32f4xx_it.c app/src/hal_timebase_tim6.c
    app/src/fault_handler.c app/src/mpu_config.c
    app/src/bme280.c app/src/bme280_port.c app/src/nmea.c app/src/gps.c
    app/src/fuel_calc.c app/src/fuel_sensor.c app/src/sensor_sm.c
    app/src/power_manager.c app/src/event_log.c app/src/boot_control.c
    app/src/host_link.c app/src/update_proto.c
    app/src/telemetry_task.c app/src/update_task.c app/src/sys_monitor.c)

function(add_app slot_letter slot_index vtor_offset)
    set(t app_slot_${slot_letter}.elf)
    add_executable(${t} ${APP_SRC} ${COMMON_SRC} ${HAL_SRC} ${RTOS_SRC} ${STARTUP} ${SYSTEM})
    target_include_directories(${t} PRIVATE
        app/inc common/inc ${CMSIS_INCLUDES} ${HAL_DIR}/Inc
        ${RTOS_DIR}/include ${RTOS_DIR}/portable/GCC/ARM_CM4F)
    target_compile_definitions(${t} PRIVATE ${COMMON_DEFS} USE_HAL_DRIVER
        APP_SLOT=${slot_index} USER_VECT_TAB_ADDRESS VECT_TAB_OFFSET=${vtor_offset}
        FW_VERSION_MAJOR=${FW_VERSION_MAJOR} FW_VERSION_MINOR=${FW_VERSION_MINOR}
        FW_VERSION_PATCH=${FW_VERSION_PATCH})
    # Third-party code is compiled without our extra warnings
    set_source_files_properties(${HAL_SRC} ${RTOS_SRC} PROPERTIES COMPILE_OPTIONS "-w")
    target_link_options(${t} PRIVATE
        -T${CMAKE_CURRENT_SOURCE_DIR}/app/linker/app_slot_${slot_letter}.ld -L${LD_DIR}
        -Wl,-Map=$<TARGET_FILE_DIR:${t}>/app_slot_${slot_letter}.map)
    firmware_outputs(${t})
endfunction()

add_app(a 0 0x00020200U)
add_app(b 1 0x00080200U)

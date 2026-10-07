# Toolchain file for arm-none-eabi-gcc (GNU Arm Embedded / STM32CubeIDE bundled toolchain)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Allow overriding the toolchain location: -DTOOLCHAIN_PREFIX=/path/to/bin/
if(NOT DEFINED TOOLCHAIN_PREFIX)
    set(TOOLCHAIN_PREFIX "")
endif()

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}arm-none-eabi-gcc)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}arm-none-eabi-objcopy CACHE FILEPATH "objcopy")
set(CMAKE_SIZE         ${TOOLCHAIN_PREFIX}arm-none-eabi-size    CACHE FILEPATH "size")

# Test-compile a static library instead of an executable (no startup code available yet)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT   "${MCU_FLAGS} -ffunction-sections -fdata-sections -fno-common -Wall -Wextra -Wno-unused-parameter")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

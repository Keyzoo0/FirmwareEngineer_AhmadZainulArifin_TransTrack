################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/bme280.c \
../Src/fuel_sensor.c \
../Src/gps.c \
../Src/main.c \
../Src/power_manager.c \
../Src/state_machine.c \
../Src/syscalls.c \
../Src/sysmem.c 

OBJS += \
./Src/bme280.o \
./Src/fuel_sensor.o \
./Src/gps.o \
./Src/main.o \
./Src/power_manager.o \
./Src/state_machine.o \
./Src/syscalls.o \
./Src/sysmem.o 

C_DEPS += \
./Src/bme280.d \
./Src/fuel_sensor.d \
./Src/gps.d \
./Src/main.d \
./Src/power_manager.d \
./Src/state_machine.d \
./Src/syscalls.d \
./Src/sysmem.d 


# Each subdirectory must supply rules for building sources it contributes
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.c Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DSTM32 -DSTM32F407G_DISC1 -DSTM32F4 -DSTM32F407VGTx -c -I../Inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src

clean-Src:
	-$(RM) ./Src/bme280.cyclo ./Src/bme280.d ./Src/bme280.o ./Src/bme280.su ./Src/fuel_sensor.cyclo ./Src/fuel_sensor.d ./Src/fuel_sensor.o ./Src/fuel_sensor.su ./Src/gps.cyclo ./Src/gps.d ./Src/gps.o ./Src/gps.su ./Src/main.cyclo ./Src/main.d ./Src/main.o ./Src/main.su ./Src/power_manager.cyclo ./Src/power_manager.d ./Src/power_manager.o ./Src/power_manager.su ./Src/state_machine.cyclo ./Src/state_machine.d ./Src/state_machine.o ./Src/state_machine.su ./Src/syscalls.cyclo ./Src/syscalls.d ./Src/syscalls.o ./Src/syscalls.su ./Src/sysmem.cyclo ./Src/sysmem.d ./Src/sysmem.o ./Src/sysmem.su

.PHONY: clean-Src


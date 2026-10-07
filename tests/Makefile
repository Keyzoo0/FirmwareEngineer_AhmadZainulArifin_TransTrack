# Host unit tests: make -C tests
CC      ?= cc
CFLAGS  += -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -g -fsanitize=address,undefined
INC      = -I../common/inc -I../app/inc
SRC      = test_main.c ../common/src/crc32.c ../common/src/sha256.c ../common/src/fw_image.c \
           ../common/src/boot_journal.c ../app/src/nmea.c ../app/src/fuel_calc.c \
           ../app/src/bme280.c ../app/src/sensor_sm.c ../app/src/update_proto.c

all: run

build/test_runner: $(SRC) unity_lite.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) -o $@ $(SRC)

build/python_image.img: ../scripts/prepare-firmware.py make_test_bin.py
	@mkdir -p build
	python3 make_test_bin.py build/test_app.bin
	python3 ../scripts/prepare-firmware.py build/test_app.bin --slot b --version 1.2.3 \
	    --build-time 0 -o $@

run: build/test_runner build/python_image.img
	cd build && cp python_image.img . 2>/dev/null; ./test_runner

clean:
	rm -rf build
.PHONY: all run clean

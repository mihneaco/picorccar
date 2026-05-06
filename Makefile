BUILD ?= dev
BUILD_DIR := build/$(BUILD)

CAR_TARGET := picorccar_car
CONTROLLER_TARGET := picorccar_controller

.PHONY: all configure car controller install-car install-controller clean

all: car controller

configure:
	cmake --preset $(BUILD)

car: configure
	cmake --build $(BUILD_DIR) --target $(CAR_TARGET)

controller: configure
	cmake --build $(BUILD_DIR) --target $(CONTROLLER_TARGET)

install-car: car
	cmake --install $(BUILD_DIR) --component car

install-controller: controller
	cmake --install $(BUILD_DIR) --component controller

clean:
	rm -rf build/

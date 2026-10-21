# Build the firmware into build/bitcon.uf2.
#
#   make build                 # default output mode from config.h
#   make build MODE=KEYBOARD   # KEYBOARD | GAMEPAD | COMPOSITE
#   make clean
#
# MODE only sets the mode a board boots in before the configurator page
# has stored one. A stored mode wins, and survives reflashing.
#
# Requires PICO_SDK_PATH to point at a pico-sdk 2.x checkout.

BUILD_DIR := build
MODE ?=

export PICO_SDK_PATH

.PHONY: build clean

build: pico_sdk_import.cmake
	cmake -B $(BUILD_DIR) -DBITCON_OUTPUT_MODE=$(MODE)
	cmake --build $(BUILD_DIR) --parallel
	@echo "→ $(BUILD_DIR)/bitcon.uf2"

pico_sdk_import.cmake:
	@test -n "$(PICO_SDK_PATH)" || { echo "PICO_SDK_PATH is not set" >&2; exit 1; }
	cp $(PICO_SDK_PATH)/external/pico_sdk_import.cmake .

clean:
	rm -rf $(BUILD_DIR)

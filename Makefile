# Default build type
BUILD_TYPE ?= debug

# Normalize to lowercase for folder name
BUILD_DIR = build/$(shell echo $(BUILD_TYPE) | tr A-Z a-z)

# CMake build type (Debug / Release ...)
CMAKE_BUILD_TYPE = $(shell echo $(BUILD_TYPE) | sed 's/^./\u&/')

# phony targets
.PHONY: all configure build run clean help

all: run

configure:
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

build:
	cmake --build $(BUILD_DIR) -j4

run: build
	@clear
	@$(BUILD_DIR)/server

# Convenience aliases
debug:
	@$(MAKE) run BUILD_TYPE=debug

release:
	@$(MAKE) run BUILD_TYPE=release

clean:
	rm -rf build/

help:
	@echo "Usage:"
	@echo "  make run BUILD_TYPE=debug          # default"
	@echo "  make run BUILD_TYPE=release"
	@echo "  make debug                         # build and run debug"
	@echo "  make release                       # build and run release"
	@echo "  make configure BUILD_TYPE=debug    # create build dir"
	@echo "  make clean"

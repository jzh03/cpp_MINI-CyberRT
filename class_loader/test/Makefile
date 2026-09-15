# Keep this legacy entry point, with all output managed by example/Makefile.
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD_DIR ?= $(PROJECT_ROOT)/example/build

.PHONY: all libplugin1.so libplugin2.so clean
all libplugin1.so libplugin2.so:
	$(MAKE) -C "$(PROJECT_ROOT)/example" BUILD_DIR="$(abspath $(BUILD_DIR))" class-loader-plugins

clean:
	$(MAKE) -C "$(PROJECT_ROOT)/example" BUILD_DIR="$(abspath $(BUILD_DIR))" clean-class-loader-plugins

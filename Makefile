# Copyright 2015 Peter Goodman (peter@trailofbits.com), all rights reserved.

.PHONY : all clean

DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

all: $(DIR)/build/libFSlice.so $(DIR)/build/libFSlice.bc
	@echo Built

clean:
	@rm -rf $(DIR)/build
	@echo Cleaned

$(DIR)/build:
	@mkdir $(DIR)/build
	@cd $(DIR)/build ; cmake -G "Unix Makefiles" -DFSLICE_DIR=$(DIR)  ..

$(DIR)/build/libFSlice.so: $(DIR)/build
	@$(MAKE) -C $(DIR)/build all

$(DIR)/build/libFSlice.bc: $(DIR)/runtime/FSlice.cpp
	@$(DIR)/llvm/build/bin/clang++ -std=c++11 -O3 -emit-llvm -c $< -o $@

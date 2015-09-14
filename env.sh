#!/usr/bin/env bash

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

export LLVM_COMPILER=clang
export LLVM_COMPILER_PATH=$DIR/llvm/build/bin/
#export WLLVM_OUTPUT=DEBUG
export CC=$DIR/whole-program-llvm/wllvm
export CXX=$DIR/whole-program-llvm/wllvm++

eval "$@"
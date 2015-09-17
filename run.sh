#!/usr/bin/env bash

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
export LLVM_COMPILER=clang
export LLVM_COMPILER_PATH=$DIR/llvm/build/bin/
export CC=$DIR/whole-program-llvm/wllvm
export CXX=$DIR/whole-program-llvm/wllvm++

$DIR/whole-program-llvm/extract-bc $1
$DIR/llvm/build/bin/opt -load $DIR/build/libFSlice.so -fslice $1.bc -o $1.inst.bc
$DIR/llvm/build/bin/llvm-link -o=$1.linked.bc $DIR/build/libFSlice.bc $1.inst.bc
$DIR/llvm/build/bin/clang++ -c $1.linked.bc -o $1.linked.o
$DIR/llvm/build/bin/clang++ -o $1.exe $1.linked.o $LDFLAGS

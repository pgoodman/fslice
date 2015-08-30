#!/usr/bin/env bash

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

$DIR/llvm/build/bin/opt -load $DIR/build/libFSlice.so -constprop -sccp -scalarrepl -mergereturn -sink -licm -mem2reg -fslice $1
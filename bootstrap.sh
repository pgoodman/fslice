#!/usr/bin/env bash

sudo apt-get update
sudo apt-get install cmake binutils-dev build-essential git

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

if [ ! -d $DIR/llvm ] ; then
    wget http://llvm.org/releases/3.6.2/llvm-3.6.2.src.tar.xz
    wget http://llvm.org/releases/3.6.2/cfe-3.6.2.src.tar.xz
    
    tar xf llvm-3.6.2.src.tar.xz
	tar xf cfe-3.6.2.src.tar.xz
	
	rm llvm-3.6.2.src.tar.xz
	rm cfe-3.6.2.src.tar.xz
	
	mv llvm-3.6.2.src $DIR/llvm
	mv cfe-3.6.2.src $DIR/llvm/tools/clang  
fi

if [ ! -d $DIR/whole-program-llvm ] ; then
    git clone https://github.com/travitch/whole-program-llvm.git
fi

mkdir $DIR/llvm/build
cd $DIR/llvm/build
cmake ../ \
    -DCMAKE_BUILD_TYPE:STRING=Release+Asserts \
    -DLLVM_ENABLE_RTTI:BOOL=ON

make -j`grep -c ^processor /proc/cpuinfo`

cd $DIT
make all

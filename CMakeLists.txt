cmake_minimum_required (VERSION 2.8)

list(APPEND CMAKE_MODULE_PATH "${FSLICE_DIR}/llvm/build/share/llvm/cmake")

include(LLVMConfig)
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})

include_directories(${LLVM_INCLUDE_DIRS})

add_compile_options(-std=c++11)
add_compile_options(-frtti)
add_compile_options(-g3)
add_compile_options(-Wall)
add_compile_options(-Werror)

add_library( FSlice SHARED ${FSLICE_DIR}/plugin/FSlice.cpp )

add_llvm_loadable_module(FSlice ${FSLICE_DIR}/plugin/FSlice.cpp)

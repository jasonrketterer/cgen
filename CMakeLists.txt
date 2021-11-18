cmake_minimum_required(VERSION 3.15)
project(LLVMBitcodeGenerator)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "-fpermissive -g -O0 -DDEBUG")
set(CMAKE_BUILD_TYPE Debug)

set (CMAKE_PREFIX_PATH "~/llvm-12/llvm-project-llvmorg-12.0.0/llvm/cmake-build-llvm-12-build/lib/cmake/llvm")
find_package(LLVM REQUIRED CONFIG)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

add_executable(cgen.exe quadreader.cpp misc.cpp sym.cpp bitcodegen.cpp
        bitcodegen.h misc.h quad.h sym.h)

# Link against LLVM libraries
llvm_map_components_to_libnames(llvm_libs support core irreader native)
target_link_libraries(cgen.exe ${llvm_libs})
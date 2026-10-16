# CI only: p2_check is plain C++ (no JUCE); on the Mac it is compiled straight from tools/p2_check.cpp.
cmake_minimum_required(VERSION 3.22)
project(p2_check LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(p2_check ../../tools/p2_check.cpp)
if(MSVC)
    target_compile_options(p2_check PRIVATE /utf-8 /bigobj)
endif()

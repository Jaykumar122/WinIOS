cmake_minimum_required(VERSION 3.20)

project(WinIOS
    VERSION 0.1.0
    DESCRIPTION "Windows ARM64 emulation research project"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(win_ios
    src/main.cpp
    src/cpu/cpu.cpp
)

target_include_directories(win_ios PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(MSVC)
    target_compile_options(win_ios PRIVATE /W4 /permissive-)
else()
    target_compile_options(win_ios PRIVATE -Wall -Wextra -Wpedantic)
endif()

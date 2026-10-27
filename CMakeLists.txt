cmake_minimum_required(VERSION 3.20)

project(WinIOS
    VERSION 0.1.0
    DESCRIPTION "Windows ARM64 emulation research project"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(winios_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Emulator core, shared by the demo executable and the unit tests.
add_library(winios_core STATIC
    src/cpu/cpu.cpp
    src/memory/memory.cpp
)

target_include_directories(winios_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

winios_set_warnings(winios_core)

add_executable(win_ios
    src/main.cpp
)

target_link_libraries(win_ios PRIVATE winios_core)
winios_set_warnings(win_ios)

include(CTest)

if(BUILD_TESTING)
    add_executable(memory_tests
        tests/unit/memory_tests.cpp
    )
    target_link_libraries(memory_tests PRIVATE winios_core)
    winios_set_warnings(memory_tests)
    add_test(NAME memory_tests COMMAND memory_tests)

    add_executable(cpu_tests
        tests/unit/cpu_tests.cpp
    )
    target_link_libraries(cpu_tests PRIVATE winios_core)
    winios_set_warnings(cpu_tests)
    add_test(NAME cpu_tests COMMAND cpu_tests)

    set_tests_properties(memory_tests cpu_tests PROPERTIES TIMEOUT 30)
endif()

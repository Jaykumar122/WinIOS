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
    src/cpu/mmu.cpp
    src/debugger/debugger.cpp
    src/inferno/inferno_launcher.cpp
    src/loader/binary_loader.cpp
    src/memory/memory.cpp
    src/runtime/guest_runtime.cpp
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

    add_executable(debugger_tests
        tests/unit/debugger_tests.cpp
    )
    target_link_libraries(debugger_tests PRIVATE winios_core)
    winios_set_warnings(debugger_tests)
    add_test(NAME debugger_tests COMMAND debugger_tests)

    add_executable(loader_tests
        tests/unit/loader_tests.cpp
    )
    target_link_libraries(loader_tests PRIVATE winios_core)
    winios_set_warnings(loader_tests)
    add_test(NAME loader_tests COMMAND loader_tests)

    add_executable(runtime_tests
        tests/unit/runtime_tests.cpp
    )
    target_link_libraries(runtime_tests PRIVATE winios_core)
    winios_set_warnings(runtime_tests)
    add_test(NAME runtime_tests COMMAND runtime_tests)

    # Deterministic child process used only by inferno_launcher_tests. It is a
    # fixture, not a test: the launcher's process handling must be covered even
    # when no local Inferno build exists.
    add_executable(launcher_test_child
        tests/support/launcher_test_child.cpp
    )
    winios_set_warnings(launcher_test_child)

    add_executable(inferno_launcher_tests
        tests/unit/inferno_launcher_tests.cpp
    )
    target_link_libraries(inferno_launcher_tests PRIVATE winios_core)
    winios_set_warnings(inferno_launcher_tests)
    target_compile_definitions(inferno_launcher_tests PRIVATE
        WINIOS_TEST_CHILD_EXE="$<TARGET_FILE:launcher_test_child>")
    add_dependencies(inferno_launcher_tests launcher_test_child)
    add_test(NAME inferno_launcher_tests COMMAND inferno_launcher_tests)

    # Opt-in integration checks against a separately built ChefKiss Inferno.
    # Returns 77 (CTest SKIP_RETURN_CODE) when no local build is configured, so
    # the default test run stays independent of an Inferno checkout.
    add_executable(inferno_integration_tests
        tests/integration/inferno_integration_tests.cpp
    )
    target_link_libraries(inferno_integration_tests PRIVATE winios_core)
    target_include_directories(inferno_integration_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit")
    winios_set_warnings(inferno_integration_tests)
    add_test(NAME inferno_integration_tests COMMAND inferno_integration_tests)
    set_tests_properties(inferno_integration_tests PROPERTIES
        SKIP_RETURN_CODE 77
        LABELS "integration"
        TIMEOUT 300)

    set_tests_properties(memory_tests cpu_tests debugger_tests loader_tests runtime_tests
                         PROPERTIES TIMEOUT 30)
    set_tests_properties(inferno_launcher_tests PROPERTIES TIMEOUT 120)
endif()

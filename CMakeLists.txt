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
    src/cpu/sysreg.cpp
    src/cpu/darwin_sysreg_compat.cpp
    src/cpu/debug_architecture.cpp
    src/cpu/debug_exceptions.cpp
    src/cpu/gicv3.cpp
    src/cpu/mmu.cpp
    src/cpu/pmu.cpp
    src/cpu/psci.cpp
    src/cpu/smp_cluster.cpp
    src/cpu/smp_core_state.cpp
    src/cpu/smp_topology.cpp
    src/device/virtual_device.cpp
    src/device/virtual_device_bus.cpp
    src/device/virtual_dma_engine.cpp
    src/device/virtual_dma_device.cpp
    src/device/dma_payload_store.cpp
    src/device/recording_recovery.cpp
    src/reference/independent_reference_model.cpp
    src/cpu/smp_exclusive_monitor.cpp
    src/cpu/smp_shared_memory.cpp
    src/cpu/smp_interrupt_router.cpp
    src/cpu/smp_scheduler.cpp
    src/cpu/virtual_time.cpp
    src/debugger/debugger.cpp
    src/inferno/inferno_launcher.cpp
    src/loader/binary_loader.cpp
    src/memory/memory.cpp
    src/runtime/guest_runtime.cpp
    src/trace/trace_ring_buffer.cpp
    src/trace/trace_serializer.cpp
    src/trace/trace_file_writer.cpp
    src/trace/trace_file_reader.cpp
    src/trace/trace_manager.cpp
    src/trace/trace_analyzer.cpp
    src/trace/trace_analysis_report.cpp
    src/snapshot/wsnap_format.cpp
    src/snapshot/snapshot_serializer.cpp
    src/snapshot/snapshot_validation.cpp
    src/snapshot/snapshot_restore.cpp
    src/snapshot/snapshot_manager.cpp
    src/snapshot/smp_snapshot_format.cpp
    src/snapshot/smp_snapshot_validation.cpp
    src/snapshot/smp_snapshot_manager.cpp
    src/sndl/sndl_format.cpp
    src/sndl/sndl_event_encoder.cpp
    src/sndl/sndl_writer.cpp
    src/sndl/sndl_recorder.cpp
    src/sndl/sndl_source_hooks.cpp
    src/sndl/sndl_validation.cpp
    src/sndl/sndl_v2_format.cpp
    src/sndl/sndl_v2_writer.cpp
    src/sndl/sndl_v2_recorder.cpp
    src/sndl/sndl_v2_validator.cpp
    src/replay/replay_checkpoint.cpp
    src/replay/replay_diagnostics.cpp
    src/replay/replay_divergence.cpp
    src/replay/replay_engine.cpp
    src/replay/replay_event_cursor.cpp
    src/replay/replay_input_router.cpp
    src/replay/replay_memory_report.cpp
    src/replay/replay_profiler.cpp
    src/replay/replay_report.cpp
    src/replay/replay_scheduler.cpp
    src/replay/replay_session.cpp
    src/replay/replay_state_hash.cpp
    src/replay/replay_workload.cpp
    src/replay/smp_replay_reader.cpp
    src/replay/smp_replay_cursor.cpp
    src/replay/smp_replay_coordinator.cpp
)

target_include_directories(winios_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(WIN32)
    # GetProcessMemoryInfo / PROCESS_MEMORY_COUNTERS_EX for Stage 21 memory probes.
    target_link_libraries(winios_core PUBLIC psapi)
endif()

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

    add_executable(threading_tests
        tests/unit/threading_tests.cpp
    )
    target_link_libraries(threading_tests PRIVATE winios_core)
    winios_set_warnings(threading_tests)
    add_test(NAME threading_tests COMMAND threading_tests)

    add_executable(memory_ordering_tests
        tests/unit/memory_ordering_tests.cpp
    )
    target_link_libraries(memory_ordering_tests PRIVATE winios_core)
    winios_set_warnings(memory_ordering_tests)
    add_test(NAME memory_ordering_tests COMMAND memory_ordering_tests)

    add_executable(advanced_mmu_tests
        tests/unit/advanced_mmu_tests.cpp
    )
    target_link_libraries(advanced_mmu_tests PRIVATE winios_core)
    winios_set_warnings(advanced_mmu_tests)
    add_test(NAME advanced_mmu_tests COMMAND advanced_mmu_tests)

    add_executable(gicv3_tests
        tests/unit/gicv3_tests.cpp
    )
    target_link_libraries(gicv3_tests PRIVATE winios_core)
    winios_set_warnings(gicv3_tests)
    add_test(NAME gicv3_tests COMMAND gicv3_tests)

    add_executable(el3_tests
        tests/unit/el3_tests.cpp
    )
    target_link_libraries(el3_tests PRIVATE winios_core)
    winios_set_warnings(el3_tests)
    add_test(NAME el3_tests COMMAND el3_tests)

    add_executable(psci_tests
        tests/unit/psci_tests.cpp
    )
    target_link_libraries(psci_tests PRIVATE winios_core)
    winios_set_warnings(psci_tests)
    add_test(NAME psci_tests COMMAND psci_tests)

    add_executable(pmu_tests
        tests/unit/pmu_tests.cpp
    )
    target_link_libraries(pmu_tests PRIVATE winios_core)
    winios_set_warnings(pmu_tests)
    add_test(NAME pmu_tests COMMAND pmu_tests)

    add_executable(timer_tests
        tests/unit/timer_tests.cpp
    )
    target_link_libraries(timer_tests PRIVATE winios_core)
    winios_set_warnings(timer_tests)
    add_test(NAME timer_tests COMMAND timer_tests)

    add_executable(debug_architecture_tests
        tests/unit/debug_architecture_tests.cpp
    )
    target_link_libraries(debug_architecture_tests PRIVATE winios_core)
    winios_set_warnings(debug_architecture_tests)
    add_test(NAME debug_architecture_tests COMMAND debug_architecture_tests)

    add_executable(debug_sysreg_tests
        tests/unit/debug_sysreg_tests.cpp
    )
    target_link_libraries(debug_sysreg_tests PRIVATE winios_core)
    winios_set_warnings(debug_sysreg_tests)
    add_test(NAME debug_sysreg_tests COMMAND debug_sysreg_tests)

    add_executable(breakpoint_tests
        tests/unit/breakpoint_tests.cpp
    )
    target_link_libraries(breakpoint_tests PRIVATE winios_core)
    winios_set_warnings(breakpoint_tests)
    add_test(NAME breakpoint_tests COMMAND breakpoint_tests)

    add_executable(watchpoint_tests
        tests/unit/watchpoint_tests.cpp
    )
    target_link_libraries(watchpoint_tests PRIVATE winios_core)
    winios_set_warnings(watchpoint_tests)
    add_test(NAME watchpoint_tests COMMAND watchpoint_tests)

    add_executable(debug_exception_tests
        tests/unit/debug_exception_tests.cpp
    )
    target_link_libraries(debug_exception_tests PRIVATE winios_core)
    winios_set_warnings(debug_exception_tests)
    add_test(NAME debug_exception_tests COMMAND debug_exception_tests)

    add_executable(linked_debug_tests
        tests/unit/linked_debug_tests.cpp
    )
    target_link_libraries(linked_debug_tests PRIVATE winios_core)
    winios_set_warnings(linked_debug_tests)
    add_test(NAME linked_debug_tests COMMAND linked_debug_tests)

    add_executable(single_step_tests
        tests/unit/single_step_tests.cpp
    )
    target_link_libraries(single_step_tests PRIVATE winios_core)
    winios_set_warnings(single_step_tests)
    add_test(NAME single_step_tests COMMAND single_step_tests)

    add_executable(debug_integration_tests
        tests/unit/debug_integration_tests.cpp
    )
    target_link_libraries(debug_integration_tests PRIVATE winios_core)
    winios_set_warnings(debug_integration_tests)
    add_test(NAME debug_integration_tests COMMAND debug_integration_tests)

    add_executable(trace_schema_tests
        tests/unit/trace_schema_tests.cpp
    )
    target_link_libraries(trace_schema_tests PRIVATE winios_core)
    winios_set_warnings(trace_schema_tests)
    add_test(NAME trace_schema_tests COMMAND trace_schema_tests)

    add_executable(trace_ring_buffer_tests
        tests/unit/trace_ring_buffer_tests.cpp
    )
    target_link_libraries(trace_ring_buffer_tests PRIVATE winios_core)
    winios_set_warnings(trace_ring_buffer_tests)
    add_test(NAME trace_ring_buffer_tests COMMAND trace_ring_buffer_tests)

    add_executable(trace_ring_buffer_benchmark
        tools/trace_ring_buffer_benchmark.cpp
    )
    target_link_libraries(trace_ring_buffer_benchmark PRIVATE winios_core)
    winios_set_warnings(trace_ring_buffer_benchmark)

    add_executable(trace_serializer_tests
        tests/unit/trace_serializer_tests.cpp
    )
    target_link_libraries(trace_serializer_tests PRIVATE winios_core)
    winios_set_warnings(trace_serializer_tests)
    add_test(NAME trace_serializer_tests COMMAND trace_serializer_tests)

    add_executable(trace_file_writer_tests
        tests/unit/trace_file_writer_tests.cpp
    )
    target_link_libraries(trace_file_writer_tests PRIVATE winios_core)
    winios_set_warnings(trace_file_writer_tests)
    add_test(NAME trace_file_writer_tests COMMAND trace_file_writer_tests)

    add_executable(trace_file_reader_tests
        tests/unit/trace_file_reader_tests.cpp
    )
    target_link_libraries(trace_file_reader_tests PRIVATE winios_core)
    winios_set_warnings(trace_file_reader_tests)
    add_test(NAME trace_file_reader_tests COMMAND trace_file_reader_tests)

    add_executable(trace_file_sink_benchmark
        tools/trace_file_sink_benchmark.cpp
    )
    target_link_libraries(trace_file_sink_benchmark PRIVATE winios_core)
    winios_set_warnings(trace_file_sink_benchmark)

    add_executable(trace_config_tests
        tests/unit/trace_config_tests.cpp
    )
    target_link_libraries(trace_config_tests PRIVATE winios_core)
    winios_set_warnings(trace_config_tests)
    add_test(NAME trace_config_tests COMMAND trace_config_tests)

    add_executable(trace_manager_tests
        tests/unit/trace_manager_tests.cpp
    )
    target_link_libraries(trace_manager_tests PRIVATE winios_core)
    winios_set_warnings(trace_manager_tests)
    add_test(NAME trace_manager_tests COMMAND trace_manager_tests)

    add_executable(trace_pipeline_tests
        tests/integration/trace_pipeline_tests.cpp
    )
    target_link_libraries(trace_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(trace_pipeline_tests)
    add_test(NAME trace_pipeline_tests COMMAND trace_pipeline_tests)

    add_executable(trace_manager_benchmark
        tools/trace_manager_benchmark.cpp
    )
    target_link_libraries(trace_manager_benchmark PRIVATE winios_core)
    winios_set_warnings(trace_manager_benchmark)

    add_executable(cpu_trace_tests
        tests/unit/cpu_trace_tests.cpp
    )
    target_link_libraries(cpu_trace_tests PRIVATE winios_core)
    winios_set_warnings(cpu_trace_tests)
    add_test(NAME cpu_trace_tests COMMAND cpu_trace_tests)

    add_executable(cpu_trace_pipeline_tests
        tests/integration/cpu_trace_pipeline_tests.cpp
    )
    target_link_libraries(cpu_trace_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(cpu_trace_pipeline_tests)
    add_test(NAME cpu_trace_pipeline_tests COMMAND cpu_trace_pipeline_tests)

    add_executable(branch_trace_tests
        tests/unit/branch_trace_tests.cpp
    )
    target_link_libraries(branch_trace_tests PRIVATE winios_core)
    winios_set_warnings(branch_trace_tests)
    add_test(NAME branch_trace_tests COMMAND branch_trace_tests)

    add_executable(branch_trace_pipeline_tests
        tests/integration/branch_trace_pipeline_tests.cpp
    )
    target_link_libraries(branch_trace_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(branch_trace_pipeline_tests)
    add_test(NAME branch_trace_pipeline_tests COMMAND branch_trace_pipeline_tests)

    add_executable(exception_trace_tests
        tests/unit/exception_trace_tests.cpp
    )
    target_link_libraries(exception_trace_tests PRIVATE winios_core)
    winios_set_warnings(exception_trace_tests)
    add_test(NAME exception_trace_tests COMMAND exception_trace_tests)

    add_executable(exception_trace_pipeline_tests
        tests/integration/exception_trace_pipeline_tests.cpp
    )
    target_link_libraries(exception_trace_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(exception_trace_pipeline_tests)
    add_test(NAME exception_trace_pipeline_tests COMMAND exception_trace_pipeline_tests)

    add_executable(sysreg_trace_tests
        tests/unit/sysreg_trace_tests.cpp
    )
    target_link_libraries(sysreg_trace_tests PRIVATE winios_core)
    winios_set_warnings(sysreg_trace_tests)
    add_test(NAME sysreg_trace_tests COMMAND sysreg_trace_tests)

    add_executable(sysreg_trace_pipeline_tests
        tests/integration/sysreg_trace_pipeline_tests.cpp
    )
    target_link_libraries(sysreg_trace_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(sysreg_trace_pipeline_tests)
    add_test(NAME sysreg_trace_pipeline_tests COMMAND sysreg_trace_pipeline_tests)

    add_executable(memory_trace_tests
        tests/unit/memory_trace_tests.cpp
    )
    target_link_libraries(memory_trace_tests PRIVATE winios_core)
    winios_set_warnings(memory_trace_tests)
    add_test(NAME memory_trace_tests COMMAND memory_trace_tests)

    add_executable(memory_trace_pipeline_tests
        tests/integration/memory_trace_pipeline_tests.cpp
    )
    target_link_libraries(memory_trace_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(memory_trace_pipeline_tests)
    add_test(NAME memory_trace_pipeline_tests COMMAND memory_trace_pipeline_tests)

    add_executable(debug_trace_tests
        tests/unit/debug_trace_tests.cpp
    )
    target_link_libraries(debug_trace_tests PRIVATE winios_core)
    winios_set_warnings(debug_trace_tests)
    add_test(NAME debug_trace_tests COMMAND debug_trace_tests)

    add_executable(debug_trace_pipeline_tests
        tests/integration/debug_trace_pipeline_tests.cpp
    )
    target_link_libraries(debug_trace_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(debug_trace_pipeline_tests)
    add_test(NAME debug_trace_pipeline_tests COMMAND debug_trace_pipeline_tests)

    add_executable(interrupt_trace_tests
        tests/unit/interrupt_trace_tests.cpp
    )
    target_link_libraries(interrupt_trace_tests PRIVATE winios_core)
    winios_set_warnings(interrupt_trace_tests)
    add_test(NAME interrupt_trace_tests COMMAND interrupt_trace_tests)

    add_executable(interrupt_trace_pipeline_tests
        tests/integration/interrupt_trace_pipeline_tests.cpp
    )
    target_link_libraries(interrupt_trace_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(interrupt_trace_pipeline_tests)
    add_test(NAME interrupt_trace_pipeline_tests COMMAND interrupt_trace_pipeline_tests)

    add_executable(cpu_lifecycle_trace_tests
        tests/unit/cpu_lifecycle_trace_tests.cpp
    )
    target_link_libraries(cpu_lifecycle_trace_tests PRIVATE winios_core)
    winios_set_warnings(cpu_lifecycle_trace_tests)
    add_test(NAME cpu_lifecycle_trace_tests COMMAND cpu_lifecycle_trace_tests)

    add_executable(context_change_trace_tests
        tests/unit/context_change_trace_tests.cpp
    )
    target_link_libraries(context_change_trace_tests PRIVATE winios_core)
    winios_set_warnings(context_change_trace_tests)
    add_test(NAME context_change_trace_tests COMMAND context_change_trace_tests)

    add_executable(lifecycle_trace_pipeline_tests
        tests/integration/lifecycle_trace_pipeline_tests.cpp
    )
    target_link_libraries(lifecycle_trace_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(lifecycle_trace_pipeline_tests)
    add_test(NAME lifecycle_trace_pipeline_tests COMMAND lifecycle_trace_pipeline_tests)

    add_executable(timer_trace_tests
        tests/unit/timer_trace_tests.cpp
    )
    target_link_libraries(timer_trace_tests PRIVATE winios_core)
    winios_set_warnings(timer_trace_tests)
    add_test(NAME timer_trace_tests COMMAND timer_trace_tests)

    add_executable(timer_trace_pipeline_tests
        tests/integration/timer_trace_pipeline_tests.cpp
    )
    target_link_libraries(timer_trace_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(timer_trace_pipeline_tests)
    add_test(NAME timer_trace_pipeline_tests COMMAND timer_trace_pipeline_tests)

    add_executable(trace_analyzer_tests
        tests/unit/trace_analyzer_tests.cpp
    )
    target_link_libraries(trace_analyzer_tests PRIVATE winios_core)
    winios_set_warnings(trace_analyzer_tests)
    add_test(NAME trace_analyzer_tests COMMAND trace_analyzer_tests)

    add_executable(trace_analysis_pipeline_tests
        tests/integration/trace_analysis_pipeline_tests.cpp
    )
    target_link_libraries(trace_analysis_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(trace_analysis_pipeline_tests)
    add_test(NAME trace_analysis_pipeline_tests COMMAND trace_analysis_pipeline_tests)

    add_executable(cpu_trace_benchmark
        tools/cpu_trace_benchmark.cpp
    )
    target_link_libraries(cpu_trace_benchmark PRIVATE winios_core)
    winios_set_warnings(cpu_trace_benchmark)

    add_executable(trace_analysis_benchmark
        tools/trace_analysis_benchmark.cpp
    )
    target_link_libraries(trace_analysis_benchmark PRIVATE winios_core)
    winios_set_warnings(trace_analysis_benchmark)

    add_executable(concurrency_benchmark
        tools/concurrency_benchmark.cpp
    )
    target_link_libraries(concurrency_benchmark PRIVATE winios_core)
    winios_set_warnings(concurrency_benchmark)

    add_executable(debug_benchmark
        tools/debug_benchmark.cpp
    )
    target_link_libraries(debug_benchmark PRIVATE winios_core)
    winios_set_warnings(debug_benchmark)

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
    add_executable(snapshot_tests
        tests/unit/snapshot_tests.cpp
    )
    target_link_libraries(snapshot_tests PRIVATE winios_core)
    winios_set_warnings(snapshot_tests)
    add_test(NAME snapshot_tests COMMAND snapshot_tests)

    add_executable(snapshot_benchmark
        tests/unit/snapshot_benchmark.cpp
    )
    target_link_libraries(snapshot_benchmark PRIVATE winios_core)
    winios_set_warnings(snapshot_benchmark)

    add_executable(sndl_tests
        tests/unit/sndl_tests.cpp
    )
    target_link_libraries(sndl_tests PRIVATE winios_core)
    winios_set_warnings(sndl_tests)
    add_test(NAME sndl_tests COMMAND sndl_tests)

    add_executable(sndl_benchmark
        tests/unit/sndl_benchmark.cpp
    )
    target_link_libraries(sndl_benchmark PRIVATE winios_core)
    winios_set_warnings(sndl_benchmark)

    add_executable(replay_tests
        tests/unit/replay_tests.cpp
    )
    target_link_libraries(replay_tests PRIVATE winios_core)
    winios_set_warnings(replay_tests)
    add_test(NAME replay_tests COMMAND replay_tests)

    add_executable(replay_benchmark
        tests/unit/replay_benchmark.cpp
    )
    target_link_libraries(replay_benchmark PRIVATE winios_core)
    winios_set_warnings(replay_benchmark)

    add_executable(replay_hardening_tests
        tests/unit/replay_hardening_tests.cpp
    )
    target_link_libraries(replay_hardening_tests PRIVATE winios_core)
    winios_set_warnings(replay_hardening_tests)
    add_test(NAME replay_hardening_tests COMMAND replay_hardening_tests)

    add_executable(replay_hardening_benchmark
        tests/unit/replay_hardening_benchmark.cpp
    )
    target_link_libraries(replay_hardening_benchmark PRIVATE winios_core)
    winios_set_warnings(replay_hardening_benchmark)

    add_executable(replay_scalability_tests
        tests/unit/replay_scalability_tests.cpp
    )
    target_link_libraries(replay_scalability_tests PRIVATE winios_core)
    winios_set_warnings(replay_scalability_tests)
    add_test(NAME replay_scalability_tests COMMAND replay_scalability_tests)

    add_executable(replay_scalability_benchmark
        tests/unit/replay_scalability_benchmark.cpp
    )
    target_link_libraries(replay_scalability_benchmark PRIVATE winios_core)
    winios_set_warnings(replay_scalability_benchmark)

    add_executable(smp_foundation_tests
        tests/unit/smp_foundation_tests.cpp
    )
    target_link_libraries(smp_foundation_tests PRIVATE winios_core)
    winios_set_warnings(smp_foundation_tests)
    add_test(NAME smp_foundation_tests COMMAND smp_foundation_tests)

    add_executable(smp_foundation_benchmark
        tests/unit/smp_foundation_benchmark.cpp
    )
    target_link_libraries(smp_foundation_benchmark PRIVATE winios_core)
    winios_set_warnings(smp_foundation_benchmark)

    add_executable(smp_snapshot_tests
        tests/unit/smp_snapshot_tests.cpp
    )
    target_link_libraries(smp_snapshot_tests PRIVATE winios_core)
    winios_set_warnings(smp_snapshot_tests)
    add_test(NAME smp_snapshot_tests COMMAND smp_snapshot_tests)

    add_executable(smp_snapshot_benchmark
        tests/unit/smp_snapshot_benchmark.cpp
    )
    target_link_libraries(smp_snapshot_benchmark PRIVATE winios_core)
    winios_set_warnings(smp_snapshot_benchmark)

    add_executable(smp_sndl_v2_tests
        tests/unit/smp_sndl_v2_tests.cpp
    )
    target_link_libraries(smp_sndl_v2_tests PRIVATE winios_core)
    winios_set_warnings(smp_sndl_v2_tests)
    add_test(NAME smp_sndl_v2_tests COMMAND smp_sndl_v2_tests)

    add_executable(smp_sndl_v2_benchmark
        tests/unit/smp_sndl_v2_benchmark.cpp
    )
    target_link_libraries(smp_sndl_v2_benchmark PRIVATE winios_core)
    winios_set_warnings(smp_sndl_v2_benchmark)

    add_executable(sndl_v2_hardening_tests
        tests/unit/sndl_v2_hardening_tests.cpp
    )
    target_link_libraries(sndl_v2_hardening_tests PRIVATE winios_core)
    winios_set_warnings(sndl_v2_hardening_tests)
    add_test(NAME sndl_v2_hardening_tests COMMAND sndl_v2_hardening_tests)

    add_executable(sndl_v2_hardening_benchmark
        tests/unit/sndl_v2_hardening_benchmark.cpp
    )
    target_link_libraries(sndl_v2_hardening_benchmark PRIVATE winios_core)
    winios_set_warnings(sndl_v2_hardening_benchmark)

    add_executable(smp_replay_tests
        tests/unit/smp_replay_tests.cpp
    )
    target_link_libraries(smp_replay_tests PRIVATE winios_core)
    winios_set_warnings(smp_replay_tests)
    add_test(NAME smp_replay_tests COMMAND smp_replay_tests)

    add_executable(smp_replay_benchmark
        tests/unit/smp_replay_benchmark.cpp
    )
    target_link_libraries(smp_replay_benchmark PRIVATE winios_core)
    winios_set_warnings(smp_replay_benchmark)

    add_executable(smp_four_core_replay_tests
        tests/unit/smp_four_core_replay_tests.cpp
    )
    target_link_libraries(smp_four_core_replay_tests PRIVATE winios_core)
    winios_set_warnings(smp_four_core_replay_tests)
    add_test(NAME smp_four_core_replay_tests COMMAND smp_four_core_replay_tests)

    add_executable(smp_four_core_replay_benchmark
        tests/unit/smp_four_core_replay_benchmark.cpp
    )
    target_link_libraries(smp_four_core_replay_benchmark PRIVATE winios_core)
    winios_set_warnings(smp_four_core_replay_benchmark)

    add_executable(smp_topology_scalability_tests
        tests/unit/smp_topology_scalability_tests.cpp
    )
    target_link_libraries(smp_topology_scalability_tests PRIVATE winios_core)
    winios_set_warnings(smp_topology_scalability_tests)
    add_test(NAME smp_topology_scalability_tests COMMAND smp_topology_scalability_tests)

    add_executable(smp_eight_core_architecture_tests
        tests/unit/smp_eight_core_architecture_tests.cpp
    )
    target_link_libraries(smp_eight_core_architecture_tests PRIVATE winios_core)
    winios_set_warnings(smp_eight_core_architecture_tests)
    add_test(NAME smp_eight_core_architecture_tests COMMAND smp_eight_core_architecture_tests)

    add_executable(smp_eight_core_replay_tests
        tests/unit/smp_eight_core_replay_tests.cpp
    )
    target_link_libraries(smp_eight_core_replay_tests PRIVATE winios_core)
    winios_set_warnings(smp_eight_core_replay_tests)
    add_test(NAME smp_eight_core_replay_tests COMMAND smp_eight_core_replay_tests)

    add_executable(smp_eight_core_replay_hardening_tests
        tests/unit/smp_eight_core_replay_hardening_tests.cpp
    )
    target_link_libraries(smp_eight_core_replay_hardening_tests PRIVATE winios_core)
    winios_set_warnings(smp_eight_core_replay_hardening_tests)
    add_test(NAME smp_eight_core_replay_hardening_tests COMMAND smp_eight_core_replay_hardening_tests)

    add_executable(smp_long_duration_replay_tests
        tests/unit/smp_long_duration_replay_tests.cpp
    )
    target_link_libraries(smp_long_duration_replay_tests PRIVATE winios_core)
    winios_set_warnings(smp_long_duration_replay_tests)
    add_test(NAME smp_long_duration_replay_tests COMMAND smp_long_duration_replay_tests)

    add_executable(smp_cross_topology_tests
        tests/unit/smp_cross_topology_tests.cpp
    )
    target_link_libraries(smp_cross_topology_tests PRIVATE winios_core)
    winios_set_warnings(smp_cross_topology_tests)
    add_test(NAME smp_cross_topology_tests COMMAND smp_cross_topology_tests)

    add_executable(smp_independent_state_validation_tests
        tests/unit/smp_independent_state_validation_tests.cpp
    )
    target_link_libraries(smp_independent_state_validation_tests PRIVATE winios_core)
    winios_set_warnings(smp_independent_state_validation_tests)
    add_test(NAME smp_independent_state_validation_tests COMMAND smp_independent_state_validation_tests)

    add_executable(smp_event_corruption_stress_tests
        tests/unit/smp_event_corruption_stress_tests.cpp
    )
    target_link_libraries(smp_event_corruption_stress_tests PRIVATE winios_core)
    winios_set_warnings(smp_event_corruption_stress_tests)
    add_test(NAME smp_event_corruption_stress_tests COMMAND smp_event_corruption_stress_tests)

    add_executable(smp_large_snapshot_stress_tests
        tests/unit/smp_large_snapshot_stress_tests.cpp
    )
    target_link_libraries(smp_large_snapshot_stress_tests PRIVATE winios_core)
    winios_set_warnings(smp_large_snapshot_stress_tests)
    add_test(NAME smp_large_snapshot_stress_tests COMMAND smp_large_snapshot_stress_tests)

    add_executable(smp_scheduler_fairness_tests
        tests/unit/smp_scheduler_fairness_tests.cpp
    )
    target_link_libraries(smp_scheduler_fairness_tests PRIVATE winios_core)
    winios_set_warnings(smp_scheduler_fairness_tests)
    add_test(NAME smp_scheduler_fairness_tests COMMAND smp_scheduler_fairness_tests)

    add_executable(smp_interrupt_timer_stress_tests
        tests/unit/smp_interrupt_timer_stress_tests.cpp
    )
    target_link_libraries(smp_interrupt_timer_stress_tests PRIVATE winios_core)
    winios_set_warnings(smp_interrupt_timer_stress_tests)
    add_test(NAME smp_interrupt_timer_stress_tests COMMAND smp_interrupt_timer_stress_tests)

    add_executable(smp_replay_performance_profile
        tests/unit/smp_replay_performance_profile.cpp
    )
    target_link_libraries(smp_replay_performance_profile PRIVATE winios_core)
    winios_set_warnings(smp_replay_performance_profile)
    add_test(NAME smp_replay_performance_profile COMMAND smp_replay_performance_profile)

    add_executable(virtual_device_bus_tests
        tests/unit/virtual_device_bus_tests.cpp
    )
    target_link_libraries(virtual_device_bus_tests PRIVATE winios_core)
    winios_set_warnings(virtual_device_bus_tests)
    add_test(NAME virtual_device_bus_tests COMMAND virtual_device_bus_tests)

    add_executable(virtual_dma_descriptor_tests
        tests/unit/virtual_dma_descriptor_tests.cpp
    )
    target_link_libraries(virtual_dma_descriptor_tests PRIVATE winios_core)
    winios_set_warnings(virtual_dma_descriptor_tests)
    add_test(NAME virtual_dma_descriptor_tests COMMAND virtual_dma_descriptor_tests)

    add_executable(virtual_dma_memory_integrity_tests
        tests/unit/virtual_dma_memory_integrity_tests.cpp
    )
    target_link_libraries(virtual_dma_memory_integrity_tests PRIVATE winios_core)
    winios_set_warnings(virtual_dma_memory_integrity_tests)
    add_test(NAME virtual_dma_memory_integrity_tests COMMAND virtual_dma_memory_integrity_tests)

    add_executable(virtual_dma_snapshot_tests
        tests/unit/virtual_dma_snapshot_tests.cpp
    )
    target_link_libraries(virtual_dma_snapshot_tests PRIVATE winios_core)
    winios_set_warnings(virtual_dma_snapshot_tests)
    add_test(NAME virtual_dma_snapshot_tests COMMAND virtual_dma_snapshot_tests)

    add_executable(virtual_dma_smp_tests
        tests/unit/virtual_dma_smp_tests.cpp
    )
    target_link_libraries(virtual_dma_smp_tests PRIVATE winios_core)
    winios_set_warnings(virtual_dma_smp_tests)
    add_test(NAME virtual_dma_smp_tests COMMAND virtual_dma_smp_tests)

    add_executable(virtual_dma_benchmark
        tests/unit/virtual_dma_benchmark.cpp
    )
    target_link_libraries(virtual_dma_benchmark PRIVATE winios_core)
    winios_set_warnings(virtual_dma_benchmark)

    add_executable(smp_eight_core_replay_benchmark
        tests/unit/smp_eight_core_replay_benchmark.cpp
    )
    target_link_libraries(smp_eight_core_replay_benchmark PRIVATE winios_core)
    winios_set_warnings(smp_eight_core_replay_benchmark)

        add_executable(dma_payload_store_tests
        tests/unit/dma_payload_store_tests.cpp
    )
    target_link_libraries(dma_payload_store_tests PRIVATE winios_core)
    winios_set_warnings(dma_payload_store_tests)
    add_test(NAME dma_payload_store_tests COMMAND dma_payload_store_tests)

    add_executable(dma_recording_replay_tests
        tests/unit/dma_recording_replay_tests.cpp
    )
    target_link_libraries(dma_recording_replay_tests PRIVATE winios_core)
    winios_set_warnings(dma_recording_replay_tests)
    add_test(NAME dma_recording_replay_tests COMMAND dma_recording_replay_tests)

    add_executable(dma_smp_determinism_tests
        tests/unit/dma_smp_determinism_tests.cpp
    )
    target_link_libraries(dma_smp_determinism_tests PRIVATE winios_core)
    winios_set_warnings(dma_smp_determinism_tests)
    add_test(NAME dma_smp_determinism_tests COMMAND dma_smp_determinism_tests)

    add_executable(dma_adversarial_tests
        tests/unit/dma_adversarial_tests.cpp
    )
    target_link_libraries(dma_adversarial_tests PRIVATE winios_core)
    winios_set_warnings(dma_adversarial_tests)
    add_test(NAME dma_adversarial_tests COMMAND dma_adversarial_tests)

    add_executable(dma_benchmark
        tests/unit/dma_benchmark.cpp
    )
    target_link_libraries(dma_benchmark PRIVATE winios_core)
    winios_set_warnings(dma_benchmark)

    add_executable(smp_scalability_benchmark
        tests/unit/smp_scalability_benchmark.cpp
    )
    target_link_libraries(smp_scalability_benchmark PRIVATE winios_core)
    winios_set_warnings(smp_scalability_benchmark)

    add_executable(dma_crash_recovery_tests
        tests/unit/dma_crash_recovery_tests.cpp
    )
    target_link_libraries(dma_crash_recovery_tests PRIVATE winios_core)
    winios_set_warnings(dma_crash_recovery_tests)
    add_test(NAME dma_crash_recovery_tests COMMAND dma_crash_recovery_tests)

    add_executable(dma_replay_checkpoint_tests
        tests/unit/dma_replay_checkpoint_tests.cpp
    )
    target_link_libraries(dma_replay_checkpoint_tests PRIVATE winios_core)
    winios_set_warnings(dma_replay_checkpoint_tests)
    add_test(NAME dma_replay_checkpoint_tests COMMAND dma_replay_checkpoint_tests)

    add_executable(dma_snapshot_continuation_tests
        tests/unit/dma_snapshot_continuation_tests.cpp
    )
    target_link_libraries(dma_snapshot_continuation_tests PRIVATE winios_core)
    winios_set_warnings(dma_snapshot_continuation_tests)
    add_test(NAME dma_snapshot_continuation_tests COMMAND dma_snapshot_continuation_tests)

    add_executable(dma_long_duration_stress_tests
        tests/unit/dma_long_duration_stress_tests.cpp
    )
    target_link_libraries(dma_long_duration_stress_tests PRIVATE winios_core)
    winios_set_warnings(dma_long_duration_stress_tests)
    add_test(NAME dma_long_duration_stress_tests COMMAND dma_long_duration_stress_tests)

    add_executable(differential_validation_tests
        tests/unit/differential_validation_tests.cpp
    )
    target_link_libraries(differential_validation_tests PRIVATE winios_core)
    winios_set_warnings(differential_validation_tests)
    add_test(NAME differential_validation_tests COMMAND differential_validation_tests)

    add_executable(independent_recording_recovery_tests
        tests/unit/independent_recording_recovery_tests.cpp
    )
    target_link_libraries(independent_recording_recovery_tests PRIVATE winios_core)
    winios_set_warnings(independent_recording_recovery_tests)
    add_test(NAME independent_recording_recovery_tests COMMAND independent_recording_recovery_tests)

    add_executable(smp_endurance_certification_tests
        tests/unit/smp_endurance_certification_tests.cpp
    )
    target_link_libraries(smp_endurance_certification_tests PRIVATE winios_core)
    winios_set_warnings(smp_endurance_certification_tests)
    add_test(NAME smp_endurance_certification_tests COMMAND smp_endurance_certification_tests)

    add_executable(dma_recovery_benchmark
        tests/unit/dma_recovery_benchmark.cpp
    )
    target_link_libraries(dma_recovery_benchmark PRIVATE winios_core)
    winios_set_warnings(dma_recovery_benchmark)

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
                         trace_schema_tests trace_ring_buffer_tests
                         trace_serializer_tests trace_file_writer_tests trace_file_reader_tests
                         trace_config_tests trace_manager_tests trace_pipeline_tests
                         cpu_trace_tests cpu_trace_pipeline_tests
                         branch_trace_tests branch_trace_pipeline_tests
                         exception_trace_tests exception_trace_pipeline_tests
                         sysreg_trace_tests sysreg_trace_pipeline_tests
                         memory_trace_tests memory_trace_pipeline_tests
                         debug_trace_tests debug_trace_pipeline_tests
                         interrupt_trace_tests interrupt_trace_pipeline_tests
                         cpu_lifecycle_trace_tests context_change_trace_tests lifecycle_trace_pipeline_tests
                         timer_trace_tests timer_trace_pipeline_tests
                         trace_analyzer_tests trace_analysis_pipeline_tests
                         snapshot_tests sndl_tests replay_tests replay_hardening_tests
                         replay_scalability_tests smp_foundation_tests smp_snapshot_tests smp_sndl_v2_tests sndl_v2_hardening_tests
                         smp_replay_tests smp_four_core_replay_tests smp_topology_scalability_tests smp_eight_core_architecture_tests smp_eight_core_replay_tests smp_eight_core_replay_hardening_tests
                         smp_long_duration_replay_tests smp_cross_topology_tests smp_independent_state_validation_tests smp_event_corruption_stress_tests smp_large_snapshot_stress_tests smp_scheduler_fairness_tests smp_interrupt_timer_stress_tests smp_replay_performance_profile
                         virtual_device_bus_tests virtual_dma_descriptor_tests virtual_dma_memory_integrity_tests virtual_dma_snapshot_tests virtual_dma_smp_tests dma_payload_store_tests dma_recording_replay_tests dma_smp_determinism_tests dma_adversarial_tests dma_crash_recovery_tests dma_replay_checkpoint_tests dma_snapshot_continuation_tests dma_long_duration_stress_tests differential_validation_tests independent_recording_recovery_tests smp_endurance_certification_tests
                         PROPERTIES TIMEOUT 180)
    set_tests_properties(threading_tests PROPERTIES TIMEOUT 120)
    set_tests_properties(inferno_launcher_tests PROPERTIES TIMEOUT 120)
endif()

# Phase 52 Stage 35 Tests
add_executable(architectural_state_integrity_tests tests/unit/architectural_state_integrity_tests.cpp)
target_link_libraries(architectural_state_integrity_tests PRIVATE winios_core)
add_test(NAME ArchitecturalStateIntegrityTests COMMAND architectural_state_integrity_tests)

add_executable(mmu_conformance_tests tests/unit/mmu_conformance_tests.cpp)
target_link_libraries(mmu_conformance_tests PRIVATE winios_core)
add_test(NAME MmuConformanceTests COMMAND mmu_conformance_tests)

add_executable(gic_memory_ordering_tests tests/unit/gic_memory_ordering_tests.cpp)
target_link_libraries(gic_memory_ordering_tests PRIVATE winios_core)
add_test(NAME GicMemoryOrderingTests COMMAND gic_memory_ordering_tests)

# Phase 52 Stage 36 Tests
add_executable(system_register_conformance_tests tests/unit/system_register_conformance_tests.cpp)
target_link_libraries(system_register_conformance_tests PRIVATE winios_core)
add_test(NAME SystemRegisterConformanceTests COMMAND system_register_conformance_tests)

# Phase 52 Stage 37 Tests
add_executable(mmu_cpacr_conformance_tests tests/unit/mmu_cpacr_conformance_tests.cpp)
target_link_libraries(mmu_cpacr_conformance_tests PRIVATE winios_core)
add_test(NAME MmuCpacrConformanceTests COMMAND mmu_cpacr_conformance_tests)

# Phase 52 Stage 38 Tests
add_executable(stage38_interrupt_exception_tests tests/unit/stage38_interrupt_exception_tests.cpp)
target_link_libraries(stage38_interrupt_exception_tests PRIVATE winios_core)
add_test(NAME ArchitecturalExceptionDeliveryTests COMMAND stage38_interrupt_exception_tests --suite 1)
add_test(NAME ExceptionVectorSelectionTests COMMAND stage38_interrupt_exception_tests --suite 2)
add_test(NAME EretStateValidationTests COMMAND stage38_interrupt_exception_tests --suite 3)
add_test(NAME DaifInterruptMaskingTests COMMAND stage38_interrupt_exception_tests --suite 4)
add_test(NAME GicInterruptLifecycleTests COMMAND stage38_interrupt_exception_tests --suite 5)
add_test(NAME GicPriorityAndPreemptionTests COMMAND stage38_interrupt_exception_tests --suite 6)
add_test(NAME GicSystemRegisterTests COMMAND stage38_interrupt_exception_tests --suite 7)
add_test(NAME VirtualTimerConformanceTests COMMAND stage38_interrupt_exception_tests --suite 8)
add_test(NAME TimerPpiIntegrationTests COMMAND stage38_interrupt_exception_tests --suite 9)
add_test(NAME SmpInterruptDeterminismTests COMMAND stage38_interrupt_exception_tests --suite 10)
add_test(NAME ExceptionInterruptReplayTests COMMAND stage38_interrupt_exception_tests --suite 11)
add_test(NAME Stage38SnapshotRestoreTests COMMAND stage38_interrupt_exception_tests --suite 12)
add_test(NAME Stage38ReferenceDifferentialTests COMMAND stage38_interrupt_exception_tests --suite 13)
add_test(NAME Stage38FuzzAndEnduranceTests COMMAND stage38_interrupt_exception_tests --suite 14)

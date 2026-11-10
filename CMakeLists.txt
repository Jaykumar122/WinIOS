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
    src/cpu/pointer_auth.cpp
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
    src/device/virtual_uart_device.cpp
    src/device/virtual_platform.cpp
    src/device/fdt_builder.cpp
    src/device/apple_devicetree.cpp
    src/device/apple_devicetree_handoff.cpp
    src/device/apple_aic.cpp
    src/device/apple_pmgr.cpp
    src/device/apple_display.cpp
    src/device/apple_ans2_storage.cpp
    src/device/apple_hardware_profile.cpp
    src/firmware/virtual_firmware.cpp
    src/firmware/apple_boot_config_manager.cpp
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
    src/loader/elf_loader.cpp
    src/loader/linux_boot_protocol.cpp
    src/loader/guest_memory_map.cpp
    src/loader/linux_image_loader.cpp
    src/loader/initramfs_loader.cpp
    src/loader/rootfs_builder.cpp
    src/loader/macho_loader.cpp
    src/loader/ramdisk_loader.cpp
    src/loader/apple_boot_artifact_inventory.cpp
    src/loader/apple_artifact_admission.cpp
    src/loader/apple_kernel_loader.cpp
    src/loader/apple_kernel_relocator.cpp
    src/loader/userspace_macho_loader.cpp
    src/loader/dyld_engine.cpp
    src/loader/real_artifact_harness.cpp
    src/loader/macho_corpus_builder.cpp
    src/loader/macho_dependency_graph.cpp
    src/loader/shared_library_mapper.cpp
    src/memory/memory.cpp
    src/memory/apple_physical_memory_map.cpp
    src/runtime/guest_runtime.cpp
    src/runtime/guest_syscall_dispatcher.cpp
    src/runtime/elf_session.cpp
    src/runtime/session_manifest.cpp
    src/runtime/artifact_publication.cpp
    src/runtime/session_transaction.cpp
    src/runtime/transaction_recovery.cpp
    src/runtime/execution_checkpoint.cpp
    src/runtime/linux_boot_session.cpp
    src/runtime/linux_boot_milestones.cpp
    src/runtime/linux_syscall_abi.cpp
    src/runtime/userspace_runner.cpp
    src/runtime/linux_boot_snapshot.cpp
    src/runtime/darwin_boot_session.cpp
    src/runtime/darwin_syscall_dispatcher.cpp
    src/runtime/darwin_mach_ipc.cpp
    src/runtime/darwin_bsd_subsystem.cpp
    src/runtime/darwin_userspace_engine.cpp
    src/runtime/darwin_runtime_library.cpp
    src/runtime/launchd_daemon_manager.cpp
    src/runtime/execution_diagnostics.cpp
    src/runtime/springboard_app_supervisor.cpp
    src/runtime/darwin_platform_expert.cpp
    src/runtime/iokit_registry.cpp
    src/fs/apfs_parser.cpp
    src/runtime/darwin_boot_milestones.cpp
    src/runtime/apple_boot_dependency_graph.cpp
    src/runtime/apple_kernel_execution_engine.cpp
    src/runtime/apple_kernel_fault_analyzer.cpp
    src/runtime/apple_xnu_early_boot.cpp
    src/ui/display_window.cpp
    src/graphics/window_server.cpp
    src/app/elf_session_cli.cpp
    src/app/linux_boot_cli.cpp
    src/app/darwin_boot_cli.cpp
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

    add_executable(stage47_elf_tests
        tests/unit/stage47_elf_tests.cpp
    )
    target_link_libraries(stage47_elf_tests PRIVATE winios_core)
    winios_set_warnings(stage47_elf_tests)
    add_test(NAME elf_loader_tests COMMAND stage47_elf_tests loader)
    add_test(NAME elf_execution_tests COMMAND stage47_elf_tests execution)

    add_executable(stage48_elf_hardening_tests tests/unit/stage48_elf_hardening_tests.cpp)
    target_link_libraries(stage48_elf_hardening_tests PRIVATE winios_core)
    winios_set_warnings(stage48_elf_hardening_tests)
    add_test(NAME elf_boundary_hardening_tests COMMAND stage48_elf_hardening_tests loader)
    add_test(NAME elf_runtime_hardening_tests COMMAND stage48_elf_hardening_tests runtime)
    set_tests_properties(elf_boundary_hardening_tests elf_runtime_hardening_tests
                         PROPERTIES TIMEOUT 30)

    add_executable(stage50_elf_session_tests tests/unit/stage50_elf_session_tests.cpp)
    target_link_libraries(stage50_elf_session_tests PRIVATE winios_core)
    winios_set_warnings(stage50_elf_session_tests)
    add_test(NAME elf_session_unit_tests COMMAND stage50_elf_session_tests unit)
    add_test(NAME elf_session_integration_tests COMMAND stage50_elf_session_tests integration)
    set_tests_properties(elf_session_unit_tests elf_session_integration_tests PROPERTIES TIMEOUT 60)

    add_executable(stage51_snapshot_continuation_tests tests/unit/stage51_snapshot_continuation_tests.cpp)
    target_link_libraries(stage51_snapshot_continuation_tests PRIVATE winios_core)
    winios_set_warnings(stage51_snapshot_continuation_tests)
    add_test(NAME elf_continuation_unit_tests COMMAND stage51_snapshot_continuation_tests unit)
    add_test(NAME elf_continuation_integration_tests COMMAND stage51_snapshot_continuation_tests integration)
    set_tests_properties(elf_continuation_unit_tests elf_continuation_integration_tests PROPERTIES TIMEOUT 60)

    add_executable(stage52_session_admission_tests tests/unit/stage52_session_admission_tests.cpp)
    target_link_libraries(stage52_session_admission_tests PRIVATE winios_core)
    winios_set_warnings(stage52_session_admission_tests)
    add_test(NAME elf_admission_unit_tests COMMAND stage52_session_admission_tests unit)
    add_test(NAME elf_admission_integration_tests COMMAND stage52_session_admission_tests integration)
    set_tests_properties(elf_admission_unit_tests elf_admission_integration_tests PROPERTIES TIMEOUT 60)

    add_executable(stage54_publication_tests tests/unit/stage54_publication_tests.cpp)
    target_link_libraries(stage54_publication_tests PRIVATE winios_core)
    winios_set_warnings(stage54_publication_tests)
    add_test(NAME elf_publication_unit_tests COMMAND stage54_publication_tests unit)
    add_test(NAME elf_publication_integration_tests COMMAND stage54_publication_tests integration)
    set_tests_properties(elf_publication_unit_tests elf_publication_integration_tests PROPERTIES TIMEOUT 120)

    add_executable(stage55_transaction_tests tests/unit/stage55_transaction_tests.cpp)
    target_link_libraries(stage55_transaction_tests PRIVATE winios_core)
    winios_set_warnings(stage55_transaction_tests)
    add_test(NAME elf_transaction_tests COMMAND stage55_transaction_tests)
    set_tests_properties(elf_transaction_tests PROPERTIES TIMEOUT 120)

    add_executable(stage56_recovery_tests tests/unit/stage56_recovery_tests.cpp)
    target_link_libraries(stage56_recovery_tests PRIVATE winios_core)
    winios_set_warnings(stage56_recovery_tests)
    add_test(NAME elf_recovery_tests COMMAND stage56_recovery_tests)
    set_tests_properties(elf_recovery_tests PROPERTIES TIMEOUT 120)

    add_executable(stage57_conformance_tests tests/unit/stage57_conformance_tests.cpp)
    target_link_libraries(stage57_conformance_tests PRIVATE winios_core)
    winios_set_warnings(stage57_conformance_tests)
    add_test(NAME elf_conformance_tests COMMAND stage57_conformance_tests)
    set_tests_properties(elf_conformance_tests PROPERTIES TIMEOUT 120)

    add_executable(stage58_integration_tests tests/unit/stage58_integration_tests.cpp)
    target_link_libraries(stage58_integration_tests PRIVATE winios_core)
    winios_set_warnings(stage58_integration_tests)
    add_test(NAME elf_integration_hardening_tests COMMAND stage58_integration_tests)
    set_tests_properties(elf_integration_hardening_tests PROPERTIES TIMEOUT 120)

    add_executable(stage59_audit_tests tests/unit/stage59_audit_tests.cpp)
    target_link_libraries(stage59_audit_tests PRIVATE winios_core)
    winios_set_warnings(stage59_audit_tests)
    add_test(NAME stage59_audit_tests COMMAND stage59_audit_tests)
    set_tests_properties(stage59_audit_tests PROPERTIES TIMEOUT 60)

    add_executable(stage60_mmu_tests tests/unit/stage60_mmu_tests.cpp)
    target_link_libraries(stage60_mmu_tests PRIVATE winios_core)
    winios_set_warnings(stage60_mmu_tests)
    add_test(NAME stage60_mmu_tests COMMAND stage60_mmu_tests)
    set_tests_properties(stage60_mmu_tests PROPERTIES TIMEOUT 60)

    add_executable(stage61_tlb_tests tests/unit/stage61_tlb_tests.cpp)
    target_link_libraries(stage61_tlb_tests PRIVATE winios_core)
    winios_set_warnings(stage61_tlb_tests)
    add_test(NAME stage61_tlb_tests COMMAND stage61_tlb_tests)
    set_tests_properties(stage61_tlb_tests PROPERTIES TIMEOUT 60)

    add_executable(stage62_exception_tests tests/unit/stage62_exception_tests.cpp)
    target_link_libraries(stage62_exception_tests PRIVATE winios_core)
    winios_set_warnings(stage62_exception_tests)
    add_test(NAME stage62_exception_tests COMMAND stage62_exception_tests)
    set_tests_properties(stage62_exception_tests PROPERTIES TIMEOUT 60)

    add_executable(stage63_interrupt_tests tests/unit/stage63_interrupt_tests.cpp)
    target_link_libraries(stage63_interrupt_tests PRIVATE winios_core)
    winios_set_warnings(stage63_interrupt_tests)
    add_test(NAME stage63_interrupt_tests COMMAND stage63_interrupt_tests)
    set_tests_properties(stage63_interrupt_tests PROPERTIES TIMEOUT 60)

    add_executable(stage64_fp_simd_tests tests/unit/stage64_fp_simd_tests.cpp)
    target_link_libraries(stage64_fp_simd_tests PRIVATE winios_core)
    winios_set_warnings(stage64_fp_simd_tests)
    add_test(NAME stage64_fp_simd_tests COMMAND stage64_fp_simd_tests)
    set_tests_properties(stage64_fp_simd_tests PROPERTIES TIMEOUT 60)

    add_executable(stage65_atomic_ordering_tests tests/unit/stage65_atomic_ordering_tests.cpp)
    target_link_libraries(stage65_atomic_ordering_tests PRIVATE winios_core)
    winios_set_warnings(stage65_atomic_ordering_tests)
    add_test(NAME stage65_atomic_ordering_tests COMMAND stage65_atomic_ordering_tests)
    set_tests_properties(stage65_atomic_ordering_tests PROPERTIES TIMEOUT 60)

    add_executable(stage66_virtual_device_tests tests/unit/stage66_virtual_device_tests.cpp)
    target_link_libraries(stage66_virtual_device_tests PRIVATE winios_core)
    winios_set_warnings(stage66_virtual_device_tests)
    add_test(NAME stage66_virtual_device_tests COMMAND stage66_virtual_device_tests)
    set_tests_properties(stage66_virtual_device_tests PROPERTIES TIMEOUT 60)

    add_executable(stage67_platform_topology_tests tests/unit/stage67_platform_topology_tests.cpp)
    target_link_libraries(stage67_platform_topology_tests PRIVATE winios_core)
    winios_set_warnings(stage67_platform_topology_tests)
    add_test(NAME stage67_platform_topology_tests COMMAND stage67_platform_topology_tests)
    set_tests_properties(stage67_platform_topology_tests PROPERTIES TIMEOUT 60)

    add_executable(stage68_smp_runtime_tests tests/unit/stage68_smp_runtime_tests.cpp)
    target_link_libraries(stage68_smp_runtime_tests PRIVATE winios_core)
    winios_set_warnings(stage68_smp_runtime_tests)
    add_test(NAME stage68_smp_runtime_tests COMMAND stage68_smp_runtime_tests)
    set_tests_properties(stage68_smp_runtime_tests PROPERTIES TIMEOUT 60)

    add_executable(stage69_guest_abi_tests tests/unit/stage69_guest_abi_tests.cpp)
    target_link_libraries(stage69_guest_abi_tests PRIVATE winios_core)
    winios_set_warnings(stage69_guest_abi_tests)
    add_test(NAME stage69_guest_abi_tests COMMAND stage69_guest_abi_tests)
    set_tests_properties(stage69_guest_abi_tests PROPERTIES TIMEOUT 60)

    add_executable(stage70_integrated_runtime_tests tests/unit/stage70_integrated_runtime_tests.cpp)
    target_link_libraries(stage70_integrated_runtime_tests PRIVATE winios_core)
    winios_set_warnings(stage70_integrated_runtime_tests)
    add_test(NAME stage70_integrated_runtime_tests COMMAND stage70_integrated_runtime_tests)
    set_tests_properties(stage70_integrated_runtime_tests PROPERTIES TIMEOUT 60)

    add_executable(stage71_performance_tests tests/unit/stage71_performance_tests.cpp)
    target_link_libraries(stage71_performance_tests PRIVATE winios_core)
    winios_set_warnings(stage71_performance_tests)
    add_test(NAME stage71_performance_tests COMMAND stage71_performance_tests)
    set_tests_properties(stage71_performance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage72_release_readiness_tests tests/unit/stage72_release_readiness_tests.cpp)
    target_link_libraries(stage72_release_readiness_tests PRIVATE winios_core)
    winios_set_warnings(stage72_release_readiness_tests)
    add_test(NAME stage72_release_readiness_tests COMMAND stage72_release_readiness_tests)
    set_tests_properties(stage72_release_readiness_tests PROPERTIES TIMEOUT 60)

    add_executable(stage73_firmware_lifecycle_tests tests/unit/stage73_firmware_lifecycle_tests.cpp)
    target_link_libraries(stage73_firmware_lifecycle_tests PRIVATE winios_core)
    winios_set_warnings(stage73_firmware_lifecycle_tests)
    add_test(NAME stage73_firmware_lifecycle_tests COMMAND stage73_firmware_lifecycle_tests)
    set_tests_properties(stage73_firmware_lifecycle_tests PROPERTIES TIMEOUT 60)

    add_executable(stage74_boot_protocol_tests tests/unit/stage74_boot_protocol_tests.cpp)
    target_link_libraries(stage74_boot_protocol_tests PRIVATE winios_core)
    winios_set_warnings(stage74_boot_protocol_tests)
    add_test(NAME stage74_boot_protocol_tests COMMAND stage74_boot_protocol_tests)
    set_tests_properties(stage74_boot_protocol_tests PROPERTIES TIMEOUT 60)

    add_executable(stage75_fdt_tests tests/unit/stage75_fdt_tests.cpp)
    target_link_libraries(stage75_fdt_tests PRIVATE winios_core)
    winios_set_warnings(stage75_fdt_tests)
    add_test(NAME stage75_fdt_tests COMMAND stage75_fdt_tests)
    set_tests_properties(stage75_fdt_tests PROPERTIES TIMEOUT 60)

    add_executable(stage76_gic_timer_integration_tests tests/unit/stage76_gic_timer_integration_tests.cpp)
    target_link_libraries(stage76_gic_timer_integration_tests PRIVATE winios_core)
    winios_set_warnings(stage76_gic_timer_integration_tests)
    add_test(NAME stage76_gic_timer_integration_tests COMMAND stage76_gic_timer_integration_tests)
    set_tests_properties(stage76_gic_timer_integration_tests PROPERTIES TIMEOUT 60)

    add_executable(stage77_uart_earlycon_tests tests/unit/stage77_uart_earlycon_tests.cpp)
    target_link_libraries(stage77_uart_earlycon_tests PRIVATE winios_core)
    winios_set_warnings(stage77_uart_earlycon_tests)
    add_test(NAME stage77_uart_earlycon_tests COMMAND stage77_uart_earlycon_tests)
    set_tests_properties(stage77_uart_earlycon_tests PROPERTIES TIMEOUT 60)

    add_executable(stage78_memory_map_tests tests/unit/stage78_memory_map_tests.cpp)
    target_link_libraries(stage78_memory_map_tests PRIVATE winios_core)
    winios_set_warnings(stage78_memory_map_tests)
    add_test(NAME stage78_memory_map_tests COMMAND stage78_memory_map_tests)
    set_tests_properties(stage78_memory_map_tests PROPERTIES TIMEOUT 60)

    add_executable(stage79_image_loader_tests tests/unit/stage79_image_loader_tests.cpp)
    target_link_libraries(stage79_image_loader_tests PRIVATE winios_core)
    winios_set_warnings(stage79_image_loader_tests)
    add_test(NAME stage79_image_loader_tests COMMAND stage79_image_loader_tests)
    set_tests_properties(stage79_image_loader_tests PROPERTIES TIMEOUT 60)

    add_executable(stage80_initramfs_tests tests/unit/stage80_initramfs_tests.cpp)
    target_link_libraries(stage80_initramfs_tests PRIVATE winios_core)
    winios_set_warnings(stage80_initramfs_tests)
    add_test(NAME stage80_initramfs_tests COMMAND stage80_initramfs_tests)
    set_tests_properties(stage80_initramfs_tests PROPERTIES TIMEOUT 60)

    add_executable(stage81_boot_manager_tests tests/unit/stage81_boot_manager_tests.cpp)
    target_link_libraries(stage81_boot_manager_tests PRIVATE winios_core)
    winios_set_warnings(stage81_boot_manager_tests)
    add_test(NAME stage81_boot_manager_tests COMMAND stage81_boot_manager_tests)
    set_tests_properties(stage81_boot_manager_tests PROPERTIES TIMEOUT 60)

    add_executable(stage82_boot_milestone_tests tests/unit/stage82_boot_milestone_tests.cpp)
    target_link_libraries(stage82_boot_milestone_tests PRIVATE winios_core)
    winios_set_warnings(stage82_boot_milestone_tests)
    add_test(NAME stage82_boot_milestone_tests COMMAND stage82_boot_milestone_tests)
    set_tests_properties(stage82_boot_milestone_tests PROPERTIES TIMEOUT 60)

    add_executable(stage83_userspace_syscall_tests tests/unit/stage83_userspace_syscall_tests.cpp)
    target_link_libraries(stage83_userspace_syscall_tests PRIVATE winios_core)
    winios_set_warnings(stage83_userspace_syscall_tests)
    add_test(NAME stage83_userspace_syscall_tests COMMAND stage83_userspace_syscall_tests)
    set_tests_properties(stage83_userspace_syscall_tests PROPERTIES TIMEOUT 60)

    add_executable(stage84_rootfs_validation_tests tests/unit/stage84_rootfs_validation_tests.cpp)
    target_link_libraries(stage84_rootfs_validation_tests PRIVATE winios_core)
    winios_set_warnings(stage84_rootfs_validation_tests)
    add_test(NAME stage84_rootfs_validation_tests COMMAND stage84_rootfs_validation_tests)
    set_tests_properties(stage84_rootfs_validation_tests PROPERTIES TIMEOUT 60)

    add_executable(stage85_deterministic_boot_tests tests/unit/stage85_deterministic_boot_tests.cpp)
    target_link_libraries(stage85_deterministic_boot_tests PRIVATE winios_core)
    winios_set_warnings(stage85_deterministic_boot_tests)
    add_test(NAME stage85_deterministic_boot_tests COMMAND stage85_deterministic_boot_tests)
    set_tests_properties(stage85_deterministic_boot_tests PROPERTIES TIMEOUT 60)

    add_executable(stage86_readiness_audit_tests tests/unit/stage86_readiness_audit_tests.cpp)
    target_link_libraries(stage86_readiness_audit_tests PRIVATE winios_core)
    winios_set_warnings(stage86_readiness_audit_tests)
    add_test(NAME stage86_readiness_audit_tests COMMAND stage86_readiness_audit_tests)
    set_tests_properties(stage86_readiness_audit_tests PROPERTIES TIMEOUT 60)

    add_executable(macho_loader_tests tests/unit/macho_loader_tests.cpp)
    target_link_libraries(macho_loader_tests PRIVATE winios_core)
    winios_set_warnings(macho_loader_tests)
    add_test(NAME macho_loader_tests COMMAND macho_loader_tests)
    set_tests_properties(macho_loader_tests PROPERTIES TIMEOUT 60)

    add_executable(apple_devicetree_tests tests/unit/apple_devicetree_tests.cpp)
    target_link_libraries(apple_devicetree_tests PRIVATE winios_core)
    winios_set_warnings(apple_devicetree_tests)
    add_test(NAME apple_devicetree_tests COMMAND apple_devicetree_tests)
    set_tests_properties(apple_devicetree_tests PROPERTIES TIMEOUT 60)

    add_executable(apple_aic_tests tests/unit/apple_aic_tests.cpp)
    target_link_libraries(apple_aic_tests PRIVATE winios_core)
    winios_set_warnings(apple_aic_tests)
    add_test(NAME apple_aic_tests COMMAND apple_aic_tests)
    set_tests_properties(apple_aic_tests PROPERTIES TIMEOUT 60)

    add_executable(darwin_boot_session_tests tests/unit/darwin_boot_session_tests.cpp)
    target_link_libraries(darwin_boot_session_tests PRIVATE winios_core)
    winios_set_warnings(darwin_boot_session_tests)
    add_test(NAME darwin_boot_session_tests COMMAND darwin_boot_session_tests)
    set_tests_properties(darwin_boot_session_tests PROPERTIES TIMEOUT 60)

    add_executable(darwin_sysreg_and_trap_tests tests/unit/darwin_sysreg_and_trap_tests.cpp)
    target_link_libraries(darwin_sysreg_and_trap_tests PRIVATE winios_core)
    winios_set_warnings(darwin_sysreg_and_trap_tests)
    add_test(NAME darwin_sysreg_and_trap_tests COMMAND darwin_sysreg_and_trap_tests)
    set_tests_properties(darwin_sysreg_and_trap_tests PROPERTIES TIMEOUT 60)

    add_executable(display_window_tests tests/unit/display_window_tests.cpp)
    target_link_libraries(display_window_tests PRIVATE winios_core)
    winios_set_warnings(display_window_tests)
    add_test(NAME display_window_tests COMMAND display_window_tests)
    set_tests_properties(display_window_tests PROPERTIES TIMEOUT 60)

    add_executable(darwin_boot_cli_tests tests/unit/darwin_boot_cli_tests.cpp)
    target_link_libraries(darwin_boot_cli_tests PRIVATE winios_core)
    winios_set_warnings(darwin_boot_cli_tests)
    add_test(NAME darwin_boot_cli_tests COMMAND darwin_boot_cli_tests)
    set_tests_properties(darwin_boot_cli_tests PROPERTIES TIMEOUT 60)

    add_executable(xnu_mmu_kaslr_tests tests/unit/xnu_mmu_kaslr_tests.cpp)
    target_link_libraries(xnu_mmu_kaslr_tests PRIVATE winios_core)
    winios_set_warnings(xnu_mmu_kaslr_tests)
    add_test(NAME xnu_mmu_kaslr_tests COMMAND xnu_mmu_kaslr_tests)
    set_tests_properties(xnu_mmu_kaslr_tests PROPERTIES TIMEOUT 60)

    add_executable(darwin_platform_probing_tests tests/unit/darwin_platform_probing_tests.cpp)
    target_link_libraries(darwin_platform_probing_tests PRIVATE winios_core)
    winios_set_warnings(darwin_platform_probing_tests)
    add_test(NAME darwin_platform_probing_tests COMMAND darwin_platform_probing_tests)
    set_tests_properties(darwin_platform_probing_tests PROPERTIES TIMEOUT 60)

    add_executable(stage93_native_ios_readiness_audit_tests tests/unit/stage93_native_ios_readiness_audit_tests.cpp)
    target_link_libraries(stage93_native_ios_readiness_audit_tests PRIVATE winios_core)
    winios_set_warnings(stage93_native_ios_readiness_audit_tests)
    add_test(NAME stage93_native_ios_readiness_audit_tests COMMAND stage93_native_ios_readiness_audit_tests)
    set_tests_properties(stage93_native_ios_readiness_audit_tests PROPERTIES TIMEOUT 60)

    add_executable(stage94_hardware_profile_verification_tests tests/unit/stage94_hardware_profile_verification_tests.cpp)
    target_link_libraries(stage94_hardware_profile_verification_tests PRIVATE winios_core)
    winios_set_warnings(stage94_hardware_profile_verification_tests)
    add_test(NAME stage94_hardware_profile_verification_tests COMMAND stage94_hardware_profile_verification_tests)
    set_tests_properties(stage94_hardware_profile_verification_tests PROPERTIES TIMEOUT 60)

    add_executable(stage95_boot_artifact_inventory_tests tests/unit/stage95_boot_artifact_inventory_tests.cpp)
    target_link_libraries(stage95_boot_artifact_inventory_tests PRIVATE winios_core)
    winios_set_warnings(stage95_boot_artifact_inventory_tests)
    add_test(NAME stage95_boot_artifact_inventory_tests COMMAND stage95_boot_artifact_inventory_tests)
    set_tests_properties(stage95_boot_artifact_inventory_tests PROPERTIES TIMEOUT 60)

    add_executable(stage96_artifact_admission_tests tests/unit/stage96_artifact_admission_tests.cpp)
    target_link_libraries(stage96_artifact_admission_tests PRIVATE winios_core)
    winios_set_warnings(stage96_artifact_admission_tests)
    add_test(NAME stage96_artifact_admission_tests COMMAND stage96_artifact_admission_tests)
    set_tests_properties(stage96_artifact_admission_tests PROPERTIES TIMEOUT 60)

    add_executable(stage97_boot_config_manager_tests tests/unit/stage97_boot_config_manager_tests.cpp)
    target_link_libraries(stage97_boot_config_manager_tests PRIVATE winios_core)
    winios_set_warnings(stage97_boot_config_manager_tests)
    add_test(NAME stage97_boot_config_manager_tests COMMAND stage97_boot_config_manager_tests)
    set_tests_properties(stage97_boot_config_manager_tests PROPERTIES TIMEOUT 60)

    add_executable(stage98_boot_dependency_graph_tests tests/unit/stage98_boot_dependency_graph_tests.cpp)
    target_link_libraries(stage98_boot_dependency_graph_tests PRIVATE winios_core)
    winios_set_warnings(stage98_boot_dependency_graph_tests)
    add_test(NAME stage98_boot_dependency_graph_tests COMMAND stage98_boot_dependency_graph_tests)
    set_tests_properties(stage98_boot_dependency_graph_tests PROPERTIES TIMEOUT 60)

    add_executable(stage99_physical_memory_map_tests tests/unit/stage99_physical_memory_map_tests.cpp)
    target_link_libraries(stage99_physical_memory_map_tests PRIVATE winios_core)
    winios_set_warnings(stage99_physical_memory_map_tests)
    add_test(NAME stage99_physical_memory_map_tests COMMAND stage99_physical_memory_map_tests)
    set_tests_properties(stage99_physical_memory_map_tests PROPERTIES TIMEOUT 60)

    add_executable(stage100_kernel_loader_tests tests/unit/stage100_kernel_loader_tests.cpp)
    target_link_libraries(stage100_kernel_loader_tests PRIVATE winios_core)
    winios_set_warnings(stage100_kernel_loader_tests)
    add_test(NAME stage100_kernel_loader_tests COMMAND stage100_kernel_loader_tests)
    set_tests_properties(stage100_kernel_loader_tests PROPERTIES TIMEOUT 60)

    add_executable(stage101_kernel_relocation_tests tests/unit/stage101_kernel_relocation_tests.cpp)
    target_link_libraries(stage101_kernel_relocation_tests PRIVATE winios_core)
    winios_set_warnings(stage101_kernel_relocation_tests)
    add_test(NAME stage101_kernel_relocation_tests COMMAND stage101_kernel_relocation_tests)
    set_tests_properties(stage101_kernel_relocation_tests PROPERTIES TIMEOUT 60)

    add_executable(stage102_kernel_entry_execution_tests tests/unit/stage102_kernel_entry_execution_tests.cpp)
    target_link_libraries(stage102_kernel_entry_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage102_kernel_entry_execution_tests)
    add_test(NAME stage102_kernel_entry_execution_tests COMMAND stage102_kernel_entry_execution_tests)
    set_tests_properties(stage102_kernel_entry_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage103_kernel_fault_analysis_tests tests/unit/stage103_kernel_fault_analysis_tests.cpp)
    target_link_libraries(stage103_kernel_fault_analysis_tests PRIVATE winios_core)
    winios_set_warnings(stage103_kernel_fault_analysis_tests)
    add_test(NAME stage103_kernel_fault_analysis_tests COMMAND stage103_kernel_fault_analysis_tests)
    set_tests_properties(stage103_kernel_fault_analysis_tests PROPERTIES TIMEOUT 60)

    add_executable(stage104_boot_args_abi_tests tests/unit/stage104_boot_args_abi_tests.cpp)
    target_link_libraries(stage104_boot_args_abi_tests PRIVATE winios_core)
    winios_set_warnings(stage104_boot_args_abi_tests)
    add_test(NAME stage104_boot_args_abi_tests COMMAND stage104_boot_args_abi_tests)
    set_tests_properties(stage104_boot_args_abi_tests PROPERTIES TIMEOUT 60)

    add_executable(stage105_devicetree_handoff_tests tests/unit/stage105_devicetree_handoff_tests.cpp)
    target_link_libraries(stage105_devicetree_handoff_tests PRIVATE winios_core)
    winios_set_warnings(stage105_devicetree_handoff_tests)
    add_test(NAME stage105_devicetree_handoff_tests COMMAND stage105_devicetree_handoff_tests)
    set_tests_properties(stage105_devicetree_handoff_tests PROPERTIES TIMEOUT 60)

    add_executable(stage106_xnu_early_boot_tests tests/unit/stage106_xnu_early_boot_tests.cpp)
    target_link_libraries(stage106_xnu_early_boot_tests PRIVATE winios_core)
    winios_set_warnings(stage106_xnu_early_boot_tests)
    add_test(NAME stage106_xnu_early_boot_tests COMMAND stage106_xnu_early_boot_tests)
    set_tests_properties(stage106_xnu_early_boot_tests PROPERTIES TIMEOUT 60)

    add_executable(stage107_mmu_translation_tests tests/unit/stage107_mmu_translation_tests.cpp)
    target_link_libraries(stage107_mmu_translation_tests PRIVATE winios_core)
    winios_set_warnings(stage107_mmu_translation_tests)
    add_test(NAME stage107_mmu_translation_tests COMMAND stage107_mmu_translation_tests)
    set_tests_properties(stage107_mmu_translation_tests PROPERTIES TIMEOUT 60)

    add_executable(stage108_exception_sysreg_tests tests/unit/stage108_exception_sysreg_tests.cpp)
    target_link_libraries(stage108_exception_sysreg_tests PRIVATE winios_core)
    winios_set_warnings(stage108_exception_sysreg_tests)
    add_test(NAME stage108_exception_sysreg_tests COMMAND stage108_exception_sysreg_tests)
    set_tests_properties(stage108_exception_sysreg_tests PROPERTIES TIMEOUT 60)

    add_executable(stage109_aic2_interrupt_tests tests/unit/stage109_aic2_interrupt_tests.cpp)
    target_link_libraries(stage109_aic2_interrupt_tests PRIVATE winios_core)
    winios_set_warnings(stage109_aic2_interrupt_tests)
    add_test(NAME stage109_aic2_interrupt_tests COMMAND stage109_aic2_interrupt_tests)
    set_tests_properties(stage109_aic2_interrupt_tests PROPERTIES TIMEOUT 60)

    add_executable(stage110_timer_clock_tests tests/unit/stage110_timer_clock_tests.cpp)
    target_link_libraries(stage110_timer_clock_tests PRIVATE winios_core)
    winios_set_warnings(stage110_timer_clock_tests)
    add_test(NAME stage110_timer_clock_tests COMMAND stage110_timer_clock_tests)
    set_tests_properties(stage110_timer_clock_tests PROPERTIES TIMEOUT 60)

    add_executable(stage111_smp_boot_tests tests/unit/stage111_smp_boot_tests.cpp)
    target_link_libraries(stage111_smp_boot_tests PRIVATE winios_core)
    winios_set_warnings(stage111_smp_boot_tests)
    add_test(NAME stage111_smp_boot_tests COMMAND stage111_smp_boot_tests)
    set_tests_properties(stage111_smp_boot_tests PROPERTIES TIMEOUT 60)

    add_executable(stage112_kernel_entry_tests tests/unit/stage112_kernel_entry_tests.cpp)
    target_link_libraries(stage112_kernel_entry_tests PRIVATE winios_core)
    winios_set_warnings(stage112_kernel_entry_tests)
    add_test(NAME stage112_kernel_entry_tests COMMAND stage112_kernel_entry_tests)
    set_tests_properties(stage112_kernel_entry_tests PROPERTIES TIMEOUT 60)

    add_executable(stage113_iokit_foundation_tests tests/unit/stage113_iokit_foundation_tests.cpp)
    target_link_libraries(stage113_iokit_foundation_tests PRIVATE winios_core)
    winios_set_warnings(stage113_iokit_foundation_tests)
    add_test(NAME stage113_iokit_foundation_tests COMMAND stage113_iokit_foundation_tests)
    set_tests_properties(stage113_iokit_foundation_tests PROPERTIES TIMEOUT 60)

    add_executable(stage114_platform_expert_pmgr_tests tests/unit/stage114_platform_expert_pmgr_tests.cpp)
    target_link_libraries(stage114_platform_expert_pmgr_tests PRIVATE winios_core)
    winios_set_warnings(stage114_platform_expert_pmgr_tests)
    add_test(NAME stage114_platform_expert_pmgr_tests COMMAND stage114_platform_expert_pmgr_tests)
    set_tests_properties(stage114_platform_expert_pmgr_tests PROPERTIES TIMEOUT 60)

    add_executable(stage115_peripheral_readiness_tests tests/unit/stage115_peripheral_readiness_tests.cpp)
    target_link_libraries(stage115_peripheral_readiness_tests PRIVATE winios_core)
    winios_set_warnings(stage115_peripheral_readiness_tests)
    add_test(NAME stage115_peripheral_readiness_tests COMMAND stage115_peripheral_readiness_tests)
    set_tests_properties(stage115_peripheral_readiness_tests PROPERTIES TIMEOUT 60)

    add_executable(stage116_apfs_parser_tests tests/unit/stage116_apfs_parser_tests.cpp)
    target_link_libraries(stage116_apfs_parser_tests PRIVATE winios_core)
    winios_set_warnings(stage116_apfs_parser_tests)
    add_test(NAME stage116_apfs_parser_tests COMMAND stage116_apfs_parser_tests)
    set_tests_properties(stage116_apfs_parser_tests PROPERTIES TIMEOUT 60)

    add_executable(stage117_rootfs_discovery_tests tests/unit/stage117_rootfs_discovery_tests.cpp)
    target_link_libraries(stage117_rootfs_discovery_tests PRIVATE winios_core)
    winios_set_warnings(stage117_rootfs_discovery_tests)
    add_test(NAME stage117_rootfs_discovery_tests COMMAND stage117_rootfs_discovery_tests)
    set_tests_properties(stage117_rootfs_discovery_tests PROPERTIES TIMEOUT 60)

    add_executable(stage118_ramdisk_volume_tests tests/unit/stage118_ramdisk_volume_tests.cpp)
    target_link_libraries(stage118_ramdisk_volume_tests PRIVATE winios_core)
    winios_set_warnings(stage118_ramdisk_volume_tests)
    add_test(NAME stage118_ramdisk_volume_tests COMMAND stage118_ramdisk_volume_tests)
    set_tests_properties(stage118_ramdisk_volume_tests PROPERTIES TIMEOUT 60)

    add_executable(stage119_storage_io_tests tests/unit/stage119_storage_io_tests.cpp)
    target_link_libraries(stage119_storage_io_tests PRIVATE winios_core)
    winios_set_warnings(stage119_storage_io_tests)
    add_test(NAME stage119_storage_io_tests COMMAND stage119_storage_io_tests)
    set_tests_properties(stage119_storage_io_tests PROPERTIES TIMEOUT 60)

    add_executable(stage120_root_boot_handoff_tests tests/unit/stage120_root_boot_handoff_tests.cpp)
    target_link_libraries(stage120_root_boot_handoff_tests PRIVATE winios_core)
    winios_set_warnings(stage120_root_boot_handoff_tests)
    add_test(NAME stage120_root_boot_handoff_tests COMMAND stage120_root_boot_handoff_tests)
    set_tests_properties(stage120_root_boot_handoff_tests PROPERTIES TIMEOUT 60)

    add_executable(stage121_userspace_macho_tests tests/unit/stage121_userspace_macho_tests.cpp)
    target_link_libraries(stage121_userspace_macho_tests PRIVATE winios_core)
    winios_set_warnings(stage121_userspace_macho_tests)
    add_test(NAME stage121_userspace_macho_tests COMMAND stage121_userspace_macho_tests)
    set_tests_properties(stage121_userspace_macho_tests PROPERTIES TIMEOUT 60)

    add_executable(stage122_arm64e_pac_tests tests/unit/stage122_arm64e_pac_tests.cpp)
    target_link_libraries(stage122_arm64e_pac_tests PRIVATE winios_core)
    winios_set_warnings(stage122_arm64e_pac_tests)
    add_test(NAME stage122_arm64e_pac_tests COMMAND stage122_arm64e_pac_tests)
    set_tests_properties(stage122_arm64e_pac_tests PROPERTIES TIMEOUT 60)

    add_executable(stage123_dyld_dynamic_linking_tests tests/unit/stage123_dyld_dynamic_linking_tests.cpp)
    target_link_libraries(stage123_dyld_dynamic_linking_tests PRIVATE winios_core)
    winios_set_warnings(stage123_dyld_dynamic_linking_tests)
    add_test(NAME stage123_dyld_dynamic_linking_tests COMMAND stage123_dyld_dynamic_linking_tests)
    set_tests_properties(stage123_dyld_dynamic_linking_tests PROPERTIES TIMEOUT 60)

    add_executable(stage124_mach_syscall_ipc_tests tests/unit/stage124_mach_syscall_ipc_tests.cpp)
    target_link_libraries(stage124_mach_syscall_ipc_tests PRIVATE winios_core)
    winios_set_warnings(stage124_mach_syscall_ipc_tests)
    add_test(NAME stage124_mach_syscall_ipc_tests COMMAND stage124_mach_syscall_ipc_tests)
    set_tests_properties(stage124_mach_syscall_ipc_tests PROPERTIES TIMEOUT 60)

    add_executable(stage125_bsd_posix_subsystem_tests tests/unit/stage125_bsd_posix_subsystem_tests.cpp)
    target_link_libraries(stage125_bsd_posix_subsystem_tests PRIVATE winios_core)
    winios_set_warnings(stage125_bsd_posix_subsystem_tests)
    add_test(NAME stage125_bsd_posix_subsystem_tests COMMAND stage125_bsd_posix_subsystem_tests)
    set_tests_properties(stage125_bsd_posix_subsystem_tests PROPERTIES TIMEOUT 60)

    add_executable(stage126_launchd_daemon_tests tests/unit/stage126_launchd_daemon_tests.cpp)
    target_link_libraries(stage126_launchd_daemon_tests PRIVATE winios_core)
    winios_set_warnings(stage126_launchd_daemon_tests)
    add_test(NAME stage126_launchd_daemon_tests COMMAND stage126_launchd_daemon_tests)
    set_tests_properties(stage126_launchd_daemon_tests PROPERTIES TIMEOUT 60)

    add_executable(stage127_windowserver_graphics_tests tests/unit/stage127_windowserver_graphics_tests.cpp)
    target_link_libraries(stage127_windowserver_graphics_tests PRIVATE winios_core)
    winios_set_warnings(stage127_windowserver_graphics_tests)
    add_test(NAME stage127_windowserver_graphics_tests COMMAND stage127_windowserver_graphics_tests)
    set_tests_properties(stage127_windowserver_graphics_tests PROPERTIES TIMEOUT 60)

    add_executable(stage128_springboard_native_app_tests tests/unit/stage128_springboard_native_app_tests.cpp)
    target_link_libraries(stage128_springboard_native_app_tests PRIVATE winios_core)
    winios_set_warnings(stage128_springboard_native_app_tests)
    add_test(NAME stage128_springboard_native_app_tests COMMAND stage128_springboard_native_app_tests)
    set_tests_properties(stage128_springboard_native_app_tests PROPERTIES TIMEOUT 60)

    add_executable(stage130_end_to_end_pipeline_tests tests/unit/stage130_end_to_end_pipeline_tests.cpp)
    target_link_libraries(stage130_end_to_end_pipeline_tests PRIVATE winios_core)
    winios_set_warnings(stage130_end_to_end_pipeline_tests)
    add_test(NAME stage130_end_to_end_pipeline_tests COMMAND stage130_end_to_end_pipeline_tests)
    set_tests_properties(stage130_end_to_end_pipeline_tests PROPERTIES TIMEOUT 60)

    add_executable(stage131_artifact_admission_tests tests/unit/stage131_artifact_admission_tests.cpp)
    target_link_libraries(stage131_artifact_admission_tests PRIVATE winios_core)
    winios_set_warnings(stage131_artifact_admission_tests)
    add_test(NAME stage131_artifact_admission_tests COMMAND stage131_artifact_admission_tests)
    set_tests_properties(stage131_artifact_admission_tests PROPERTIES TIMEOUT 60)

    add_executable(stage132_macho_execution_tests tests/unit/stage132_macho_execution_tests.cpp)
    target_link_libraries(stage132_macho_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage132_macho_execution_tests)
    add_test(NAME stage132_macho_execution_tests COMMAND stage132_macho_execution_tests)
    set_tests_properties(stage132_macho_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage133_dyld_integration_tests tests/unit/stage133_dyld_integration_tests.cpp)
    target_link_libraries(stage133_dyld_integration_tests PRIVATE winios_core)
    winios_set_warnings(stage133_dyld_integration_tests)
    add_test(NAME stage133_dyld_integration_tests COMMAND stage133_dyld_integration_tests)
    set_tests_properties(stage133_dyld_integration_tests PROPERTIES TIMEOUT 60)

    add_executable(stage134_mach_bsd_integration_tests tests/unit/stage134_mach_bsd_integration_tests.cpp)
    target_link_libraries(stage134_mach_bsd_integration_tests PRIVATE winios_core)
    winios_set_warnings(stage134_mach_bsd_integration_tests)
    add_test(NAME stage134_mach_bsd_integration_tests COMMAND stage134_mach_bsd_integration_tests)
    set_tests_properties(stage134_mach_bsd_integration_tests PROPERTIES TIMEOUT 60)

    add_executable(stage135_launchd_execution_gate_tests tests/unit/stage135_launchd_execution_gate_tests.cpp)
    target_link_libraries(stage135_launchd_execution_gate_tests PRIVATE winios_core)
    winios_set_warnings(stage135_launchd_execution_gate_tests)
    add_test(NAME stage135_launchd_execution_gate_tests COMMAND stage135_launchd_execution_gate_tests)
    set_tests_properties(stage135_launchd_execution_gate_tests PROPERTIES TIMEOUT 60)

    add_executable(stage136_darwin_service_startup_tests tests/unit/stage136_darwin_service_startup_tests.cpp)
    target_link_libraries(stage136_darwin_service_startup_tests PRIVATE winios_core)
    winios_set_warnings(stage136_darwin_service_startup_tests)
    add_test(NAME stage136_darwin_service_startup_tests COMMAND stage136_darwin_service_startup_tests)
    set_tests_properties(stage136_darwin_service_startup_tests PROPERTIES TIMEOUT 60)

    add_executable(stage137_graphics_execution_tests tests/unit/stage137_graphics_execution_tests.cpp)
    target_link_libraries(stage137_graphics_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage137_graphics_execution_tests)
    add_test(NAME stage137_graphics_execution_tests COMMAND stage137_graphics_execution_tests)
    set_tests_properties(stage137_graphics_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage138_springboard_execution_tests tests/unit/stage138_springboard_execution_tests.cpp)
    target_link_libraries(stage138_springboard_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage138_springboard_execution_tests)
    add_test(NAME stage138_springboard_execution_tests COMMAND stage138_springboard_execution_tests)
    set_tests_properties(stage138_springboard_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage139_native_app_execution_tests tests/unit/stage139_native_app_execution_tests.cpp)
    target_link_libraries(stage139_native_app_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage139_native_app_execution_tests)
    add_test(NAME stage139_native_app_execution_tests COMMAND stage139_native_app_execution_tests)
    set_tests_properties(stage139_native_app_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage142_el0_conformance_tests tests/unit/stage142_el0_conformance_tests.cpp)
    target_link_libraries(stage142_el0_conformance_tests PRIVATE winios_core)
    winios_set_warnings(stage142_el0_conformance_tests)
    add_test(NAME stage142_el0_conformance_tests COMMAND stage142_el0_conformance_tests)
    set_tests_properties(stage142_el0_conformance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage143_svc_exception_tests tests/unit/stage143_svc_exception_tests.cpp)
    target_link_libraries(stage143_svc_exception_tests PRIVATE winios_core)
    winios_set_warnings(stage143_svc_exception_tests)
    add_test(NAME stage143_svc_exception_tests COMMAND stage143_svc_exception_tests)
    set_tests_properties(stage143_svc_exception_tests PROPERTIES TIMEOUT 60)

    add_executable(stage144_bsd_syscall_abi_tests tests/unit/stage144_bsd_syscall_abi_tests.cpp)
    target_link_libraries(stage144_bsd_syscall_abi_tests PRIVATE winios_core)
    winios_set_warnings(stage144_bsd_syscall_abi_tests)
    add_test(NAME stage144_bsd_syscall_abi_tests COMMAND stage144_bsd_syscall_abi_tests)
    set_tests_properties(stage144_bsd_syscall_abi_tests PROPERTIES TIMEOUT 60)

    add_executable(stage145_mach_ipc_abi_tests tests/unit/stage145_mach_ipc_abi_tests.cpp)
    target_link_libraries(stage145_mach_ipc_abi_tests PRIVATE winios_core)
    winios_set_warnings(stage145_mach_ipc_abi_tests)
    add_test(NAME stage145_mach_ipc_abi_tests COMMAND stage145_mach_ipc_abi_tests)
    set_tests_properties(stage145_mach_ipc_abi_tests PROPERTIES TIMEOUT 60)

    add_executable(stage146_userspace_memory_tests tests/unit/stage146_userspace_memory_tests.cpp)
    target_link_libraries(stage146_userspace_memory_tests PRIVATE winios_core)
    winios_set_warnings(stage146_userspace_memory_tests)
    add_test(NAME stage146_userspace_memory_tests COMMAND stage146_userspace_memory_tests)
    set_tests_properties(stage146_userspace_memory_tests PROPERTIES TIMEOUT 60)

    add_executable(stage147_macho_admission_tests tests/unit/stage147_macho_admission_tests.cpp)
    target_link_libraries(stage147_macho_admission_tests PRIVATE winios_core)
    winios_set_warnings(stage147_macho_admission_tests)
    add_test(NAME stage147_macho_admission_tests COMMAND stage147_macho_admission_tests)
    set_tests_properties(stage147_macho_admission_tests PROPERTIES TIMEOUT 60)

    add_executable(stage148_dyld_compatibility_tests tests/unit/stage148_dyld_compatibility_tests.cpp)
    target_link_libraries(stage148_dyld_compatibility_tests PRIVATE winios_core)
    winios_set_warnings(stage148_dyld_compatibility_tests)
    add_test(NAME stage148_dyld_compatibility_tests COMMAND stage148_dyld_compatibility_tests)
    set_tests_properties(stage148_dyld_compatibility_tests PROPERTIES TIMEOUT 60)

    add_executable(stage149_pac_conformance_tests tests/unit/stage149_pac_conformance_tests.cpp)
    target_link_libraries(stage149_pac_conformance_tests PRIVATE winios_core)
    winios_set_warnings(stage149_pac_conformance_tests)
    add_test(NAME stage149_pac_conformance_tests COMMAND stage149_pac_conformance_tests)
    set_tests_properties(stage149_pac_conformance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage150_process_thread_tests tests/unit/stage150_process_thread_tests.cpp)
    target_link_libraries(stage150_process_thread_tests PRIVATE winios_core)
    winios_set_warnings(stage150_process_thread_tests)
    add_test(NAME stage150_process_thread_tests COMMAND stage150_process_thread_tests)
    set_tests_properties(stage150_process_thread_tests PROPERTIES TIMEOUT 60)

    add_executable(stage151_vfs_integration_tests tests/unit/stage151_vfs_integration_tests.cpp)
    target_link_libraries(stage151_vfs_integration_tests PRIVATE winios_core)
    winios_set_warnings(stage151_vfs_integration_tests)
    add_test(NAME stage151_vfs_integration_tests COMMAND stage151_vfs_integration_tests)
    set_tests_properties(stage151_vfs_integration_tests PROPERTIES TIMEOUT 60)

    add_executable(stage152_service_lifecycle_tests tests/unit/stage152_service_lifecycle_tests.cpp)
    target_link_libraries(stage152_service_lifecycle_tests PRIVATE winios_core)
    winios_set_warnings(stage152_service_lifecycle_tests)
    add_test(NAME stage152_service_lifecycle_tests COMMAND stage152_service_lifecycle_tests)
    set_tests_properties(stage152_service_lifecycle_tests PROPERTIES TIMEOUT 60)

    add_executable(stage153_genuine_artifact_readiness_tests tests/unit/stage153_genuine_artifact_readiness_tests.cpp)
    target_link_libraries(stage153_genuine_artifact_readiness_tests PRIVATE winios_core)
    winios_set_warnings(stage153_genuine_artifact_readiness_tests)
    add_test(NAME stage153_genuine_artifact_readiness_tests COMMAND stage153_genuine_artifact_readiness_tests)
    set_tests_properties(stage153_genuine_artifact_readiness_tests PROPERTIES TIMEOUT 60)

    add_executable(stage154_end_to_end_execution_tests tests/unit/stage154_end_to_end_execution_tests.cpp)
    target_link_libraries(stage154_end_to_end_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage154_end_to_end_execution_tests)
    add_test(NAME stage154_end_to_end_execution_tests COMMAND stage154_end_to_end_execution_tests)
    set_tests_properties(stage154_end_to_end_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage155_campaign_audit_tests tests/unit/stage155_campaign_audit_tests.cpp)
    target_link_libraries(stage155_campaign_audit_tests PRIVATE winios_core)
    winios_set_warnings(stage155_campaign_audit_tests)
    add_test(NAME stage155_campaign_audit_tests COMMAND stage155_campaign_audit_tests)
    set_tests_properties(stage155_campaign_audit_tests PROPERTIES TIMEOUT 60)

    add_executable(stage157_xnu_exception_conformance_tests tests/unit/stage157_xnu_exception_conformance_tests.cpp)
    target_link_libraries(stage157_xnu_exception_conformance_tests PRIVATE winios_core)
    winios_set_warnings(stage157_xnu_exception_conformance_tests)
    add_test(NAME stage157_xnu_exception_conformance_tests COMMAND stage157_xnu_exception_conformance_tests)
    set_tests_properties(stage157_xnu_exception_conformance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage158_bsd_syscall_conformance_tests tests/unit/stage158_bsd_syscall_conformance_tests.cpp)
    target_link_libraries(stage158_bsd_syscall_conformance_tests PRIVATE winios_core)
    winios_set_warnings(stage158_bsd_syscall_conformance_tests)
    add_test(NAME stage158_bsd_syscall_conformance_tests COMMAND stage158_bsd_syscall_conformance_tests)
    set_tests_properties(stage158_bsd_syscall_conformance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage159_mach_ipc_conformance_tests tests/unit/stage159_mach_ipc_conformance_tests.cpp)
    target_link_libraries(stage159_mach_ipc_conformance_tests PRIVATE winios_core)
    winios_set_warnings(stage159_mach_ipc_conformance_tests)
    add_test(NAME stage159_mach_ipc_conformance_tests COMMAND stage159_mach_ipc_conformance_tests)
    set_tests_properties(stage159_mach_ipc_conformance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage160_memory_conformance_tests tests/unit/stage160_memory_conformance_tests.cpp)
    target_link_libraries(stage160_memory_conformance_tests PRIVATE winios_core)
    winios_set_warnings(stage160_memory_conformance_tests)
    add_test(NAME stage160_memory_conformance_tests COMMAND stage160_memory_conformance_tests)
    set_tests_properties(stage160_memory_conformance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage161_arm64e_pac_validation_tests tests/unit/stage161_arm64e_pac_validation_tests.cpp)
    target_link_libraries(stage161_arm64e_pac_validation_tests PRIVATE winios_core)
    winios_set_warnings(stage161_arm64e_pac_validation_tests)
    add_test(NAME stage161_arm64e_pac_validation_tests COMMAND stage161_arm64e_pac_validation_tests)
    set_tests_properties(stage161_arm64e_pac_validation_tests PROPERTIES TIMEOUT 60)

    add_executable(stage162_macho_loader_audit_tests tests/unit/stage162_macho_loader_audit_tests.cpp)
    target_link_libraries(stage162_macho_loader_audit_tests PRIVATE winios_core)
    winios_set_warnings(stage162_macho_loader_audit_tests)
    add_test(NAME stage162_macho_loader_audit_tests COMMAND stage162_macho_loader_audit_tests)
    set_tests_properties(stage162_macho_loader_audit_tests PROPERTIES TIMEOUT 60)

    add_executable(stage163_dyld_compatibility_tests tests/unit/stage163_dyld_compatibility_tests.cpp)
    target_link_libraries(stage163_dyld_compatibility_tests PRIVATE winios_core)
    winios_set_warnings(stage163_dyld_compatibility_tests)
    add_test(NAME stage163_dyld_compatibility_tests COMMAND stage163_dyld_compatibility_tests)
    set_tests_properties(stage163_dyld_compatibility_tests PROPERTIES TIMEOUT 60)

    add_executable(stage164_real_artifact_harness_tests tests/unit/stage164_real_artifact_harness_tests.cpp)
    target_link_libraries(stage164_real_artifact_harness_tests PRIVATE winios_core)
    winios_set_warnings(stage164_real_artifact_harness_tests)
    add_test(NAME stage164_real_artifact_harness_tests COMMAND stage164_real_artifact_harness_tests)
    set_tests_properties(stage164_real_artifact_harness_tests PROPERTIES TIMEOUT 60)

    add_executable(stage165_process_thread_conformance_tests tests/unit/stage165_process_thread_conformance_tests.cpp)
    target_link_libraries(stage165_process_thread_conformance_tests PRIVATE winios_core)
    winios_set_warnings(stage165_process_thread_conformance_tests)
    add_test(NAME stage165_process_thread_conformance_tests COMMAND stage165_process_thread_conformance_tests)
    set_tests_properties(stage165_process_thread_conformance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage166_launchd_bootstrap_conformance_tests tests/unit/stage166_launchd_bootstrap_conformance_tests.cpp)
    target_link_libraries(stage166_launchd_bootstrap_conformance_tests PRIVATE winios_core)
    winios_set_warnings(stage166_launchd_bootstrap_conformance_tests)
    add_test(NAME stage166_launchd_bootstrap_conformance_tests COMMAND stage166_launchd_bootstrap_conformance_tests)
    set_tests_properties(stage166_launchd_bootstrap_conformance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage167_genuine_macho_corpus_tests tests/unit/stage167_genuine_macho_corpus_tests.cpp)
    target_link_libraries(stage167_genuine_macho_corpus_tests PRIVATE winios_core)
    winios_set_warnings(stage167_genuine_macho_corpus_tests)
    add_test(NAME stage167_genuine_macho_corpus_tests COMMAND stage167_genuine_macho_corpus_tests)
    set_tests_properties(stage167_genuine_macho_corpus_tests PROPERTIES TIMEOUT 60)

    add_executable(stage168_execution_diagnostics_tests tests/unit/stage168_execution_diagnostics_tests.cpp)
    target_link_libraries(stage168_execution_diagnostics_tests PRIVATE winios_core)
    winios_set_warnings(stage168_execution_diagnostics_tests)
    add_test(NAME stage168_execution_diagnostics_tests COMMAND stage168_execution_diagnostics_tests)
    set_tests_properties(stage168_execution_diagnostics_tests PROPERTIES TIMEOUT 60)

    add_executable(stage169_end_to_end_conformance_tests tests/unit/stage169_end_to_end_conformance_tests.cpp)
    target_link_libraries(stage169_end_to_end_conformance_tests PRIVATE winios_core)
    winios_set_warnings(stage169_end_to_end_conformance_tests)
    add_test(NAME stage169_end_to_end_conformance_tests COMMAND stage169_end_to_end_conformance_tests)
    set_tests_properties(stage169_end_to_end_conformance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage170_independent_release_audit_tests tests/unit/stage170_independent_release_audit_tests.cpp)
    target_link_libraries(stage170_independent_release_audit_tests PRIVATE winios_core)
    winios_set_warnings(stage170_independent_release_audit_tests)
    add_test(NAME stage170_independent_release_audit_tests COMMAND stage170_independent_release_audit_tests)
    set_tests_properties(stage170_independent_release_audit_tests PROPERTIES TIMEOUT 60)

    add_executable(stage172_macho_corpus_tests tests/unit/stage172_macho_corpus_tests.cpp)
    target_link_libraries(stage172_macho_corpus_tests PRIVATE winios_core)
    winios_set_warnings(stage172_macho_corpus_tests)
    add_test(NAME stage172_macho_corpus_tests COMMAND stage172_macho_corpus_tests)
    set_tests_properties(stage172_macho_corpus_tests PROPERTIES TIMEOUT 60)

    add_executable(stage173_real_entry_execution_tests tests/unit/stage173_real_entry_execution_tests.cpp)
    target_link_libraries(stage173_real_entry_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage173_real_entry_execution_tests)
    add_test(NAME stage173_real_entry_execution_tests COMMAND stage173_real_entry_execution_tests)
    set_tests_properties(stage173_real_entry_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage174_guest_process_environment_tests tests/unit/stage174_guest_process_environment_tests.cpp)
    target_link_libraries(stage174_guest_process_environment_tests PRIVATE winios_core)
    winios_set_warnings(stage174_guest_process_environment_tests)
    add_test(NAME stage174_guest_process_environment_tests COMMAND stage174_guest_process_environment_tests)
    set_tests_properties(stage174_guest_process_environment_tests PROPERTIES TIMEOUT 60)

    add_executable(stage175_dependency_graph_tests tests/unit/stage175_dependency_graph_tests.cpp)
    target_link_libraries(stage175_dependency_graph_tests PRIVATE winios_core)
    winios_set_warnings(stage175_dependency_graph_tests)
    add_test(NAME stage175_dependency_graph_tests COMMAND stage175_dependency_graph_tests)
    set_tests_properties(stage175_dependency_graph_tests PROPERTIES TIMEOUT 60)

    add_executable(stage176_chained_fixups_tests tests/unit/stage176_chained_fixups_tests.cpp)
    target_link_libraries(stage176_chained_fixups_tests PRIVATE winios_core)
    winios_set_warnings(stage176_chained_fixups_tests)
    add_test(NAME stage176_chained_fixups_tests COMMAND stage176_chained_fixups_tests)
    set_tests_properties(stage176_chained_fixups_tests PROPERTIES TIMEOUT 60)

    add_executable(stage177_symbol_binding_tests tests/unit/stage177_symbol_binding_tests.cpp)
    target_link_libraries(stage177_symbol_binding_tests PRIVATE winios_core)
    winios_set_warnings(stage177_symbol_binding_tests)
    add_test(NAME stage177_symbol_binding_tests COMMAND stage177_symbol_binding_tests)
    set_tests_properties(stage177_symbol_binding_tests PROPERTIES TIMEOUT 60)

    add_executable(stage178_shared_library_mapping_tests tests/unit/stage178_shared_library_mapping_tests.cpp)
    target_link_libraries(stage178_shared_library_mapping_tests PRIVATE winios_core)
    winios_set_warnings(stage178_shared_library_mapping_tests)
    add_test(NAME stage178_shared_library_mapping_tests COMMAND stage178_shared_library_mapping_tests)
    set_tests_properties(stage178_shared_library_mapping_tests PROPERTIES TIMEOUT 60)

    add_executable(stage179_minimal_darwin_runtime_tests tests/unit/stage179_minimal_darwin_runtime_tests.cpp)
    target_link_libraries(stage179_minimal_darwin_runtime_tests PRIVATE winios_core)
    winios_set_warnings(stage179_minimal_darwin_runtime_tests)
    add_test(NAME stage179_minimal_darwin_runtime_tests COMMAND stage179_minimal_darwin_runtime_tests)
    set_tests_properties(stage179_minimal_darwin_runtime_tests PROPERTIES TIMEOUT 60)

    add_executable(stage180_bsd_syscall_expansion_tests tests/unit/stage180_bsd_syscall_expansion_tests.cpp)
    target_link_libraries(stage180_bsd_syscall_expansion_tests PRIVATE winios_core)
    winios_set_warnings(stage180_bsd_syscall_expansion_tests)
    add_test(NAME stage180_bsd_syscall_expansion_tests COMMAND stage180_bsd_syscall_expansion_tests)
    set_tests_properties(stage180_bsd_syscall_expansion_tests PROPERTIES TIMEOUT 60)

    add_executable(stage181_mach_ipc_guest_integration_tests tests/unit/stage181_mach_ipc_guest_integration_tests.cpp)
    target_link_libraries(stage181_mach_ipc_guest_integration_tests PRIVATE winios_core)
    winios_set_warnings(stage181_mach_ipc_guest_integration_tests)
    add_test(NAME stage181_mach_ipc_guest_integration_tests COMMAND stage181_mach_ipc_guest_integration_tests)
    set_tests_properties(stage181_mach_ipc_guest_integration_tests PROPERTIES TIMEOUT 60)

    add_executable(stage182_vfs_runtime_integration_tests tests/unit/stage182_vfs_runtime_integration_tests.cpp)
    target_link_libraries(stage182_vfs_runtime_integration_tests PRIVATE winios_core)
    winios_set_warnings(stage182_vfs_runtime_integration_tests)
    add_test(NAME stage182_vfs_runtime_integration_tests COMMAND stage182_vfs_runtime_integration_tests)
    set_tests_properties(stage182_vfs_runtime_integration_tests PROPERTIES TIMEOUT 60)

    add_executable(stage183_process_thread_execution_tests tests/unit/stage183_process_thread_execution_tests.cpp)
    target_link_libraries(stage183_process_thread_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage183_process_thread_execution_tests)
    add_test(NAME stage183_process_thread_execution_tests COMMAND stage183_process_thread_execution_tests)
    set_tests_properties(stage183_process_thread_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage184_real_macho_smoke_tests tests/unit/stage184_real_macho_smoke_tests.cpp)
    target_link_libraries(stage184_real_macho_smoke_tests PRIVATE winios_core)
    winios_set_warnings(stage184_real_macho_smoke_tests)
    add_test(NAME stage184_real_macho_smoke_tests COMMAND stage184_real_macho_smoke_tests)
    set_tests_properties(stage184_real_macho_smoke_tests PROPERTIES TIMEOUT 60)

    add_executable(stage185_real_dependency_execution_tests tests/unit/stage185_real_dependency_execution_tests.cpp)
    target_link_libraries(stage185_real_dependency_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage185_real_dependency_execution_tests)
    add_test(NAME stage185_real_dependency_execution_tests COMMAND stage185_real_dependency_execution_tests)
    set_tests_properties(stage185_real_dependency_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage186_real_file_memory_tests tests/unit/stage186_real_file_memory_tests.cpp)
    target_link_libraries(stage186_real_file_memory_tests PRIVATE winios_core)
    winios_set_warnings(stage186_real_file_memory_tests)
    add_test(NAME stage186_real_file_memory_tests COMMAND stage186_real_file_memory_tests)
    set_tests_properties(stage186_real_file_memory_tests PROPERTIES TIMEOUT 60)

    add_executable(stage187_multi_image_execution_tests tests/unit/stage187_multi_image_execution_tests.cpp)
    target_link_libraries(stage187_multi_image_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage187_multi_image_execution_tests)
    add_test(NAME stage187_multi_image_execution_tests COMMAND stage187_multi_image_execution_tests)
    set_tests_properties(stage187_multi_image_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage188_instruction_execution_evidence_tests tests/unit/stage188_instruction_execution_evidence_tests.cpp)
    target_link_libraries(stage188_instruction_execution_evidence_tests PRIVATE winios_core)
    winios_set_warnings(stage188_instruction_execution_evidence_tests)
    add_test(NAME stage188_instruction_execution_evidence_tests COMMAND stage188_instruction_execution_evidence_tests)
    set_tests_properties(stage188_instruction_execution_evidence_tests PROPERTIES TIMEOUT 60)

    add_executable(stage189_runtime_reliability_tests tests/unit/stage189_runtime_reliability_tests.cpp)
    target_link_libraries(stage189_runtime_reliability_tests PRIVATE winios_core)
    winios_set_warnings(stage189_runtime_reliability_tests)
    add_test(NAME stage189_runtime_reliability_tests COMMAND stage189_runtime_reliability_tests)
    set_tests_properties(stage189_runtime_reliability_tests PROPERTIES TIMEOUT 60)

    add_executable(stage190_independent_release_audit_tests tests/unit/stage190_independent_release_audit_tests.cpp)
    target_link_libraries(stage190_independent_release_audit_tests PRIVATE winios_core)
    winios_set_warnings(stage190_independent_release_audit_tests)
    add_test(NAME stage190_independent_release_audit_tests COMMAND stage190_independent_release_audit_tests)
    set_tests_properties(stage190_independent_release_audit_tests PROPERTIES TIMEOUT 60)

    add_executable(stage199_replay_and_reliability_tests tests/unit/stage199_replay_and_reliability_tests.cpp)
    target_link_libraries(stage199_replay_and_reliability_tests PRIVATE winios_core)
    winios_set_warnings(stage199_replay_and_reliability_tests)
    add_test(NAME stage199_replay_and_reliability_tests COMMAND stage199_replay_and_reliability_tests)
    set_tests_properties(stage199_replay_and_reliability_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_r3_real_artifact_execution_tests tests/unit/stage_r3_real_artifact_execution_tests.cpp)
    target_link_libraries(stage_r3_real_artifact_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage_r3_real_artifact_execution_tests)
    add_test(NAME stage_r3_real_artifact_execution_tests COMMAND stage_r3_real_artifact_execution_tests)
    set_tests_properties(stage_r3_real_artifact_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(adr_adrp_conformance_tests tests/unit/adr_adrp_conformance_tests.cpp)
    target_link_libraries(adr_adrp_conformance_tests PRIVATE winios_core)
    winios_set_warnings(adr_adrp_conformance_tests)
    add_test(NAME adr_adrp_conformance_tests COMMAND adr_adrp_conformance_tests)
    set_tests_properties(adr_adrp_conformance_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_s1_compiler_generated_execution_tests tests/unit/stage_s1_compiler_generated_execution_tests.cpp)
    target_link_libraries(stage_s1_compiler_generated_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage_s1_compiler_generated_execution_tests)
    add_test(NAME stage_s1_compiler_generated_execution_tests COMMAND stage_s1_compiler_generated_execution_tests)
    set_tests_properties(stage_s1_compiler_generated_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_s2_real_dylib_execution_tests tests/unit/stage_s2_real_dylib_execution_tests.cpp)
    target_link_libraries(stage_s2_real_dylib_execution_tests PRIVATE winios_core)
    winios_set_warnings(stage_s2_real_dylib_execution_tests)
    add_test(NAME stage_s2_real_dylib_execution_tests COMMAND stage_s2_real_dylib_execution_tests)
    set_tests_properties(stage_s2_real_dylib_execution_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_t0_stack_protector_tests tests/unit/stage_t0_stack_protector_tests.cpp)
    target_link_libraries(stage_t0_stack_protector_tests PRIVATE winios_core)
    winios_set_warnings(stage_t0_stack_protector_tests)
    add_test(NAME stage_t0_stack_protector_tests COMMAND stage_t0_stack_protector_tests)
    set_tests_properties(stage_t0_stack_protector_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_t1_ios_target_tests tests/unit/stage_t1_ios_target_tests.cpp)
    target_link_libraries(stage_t1_ios_target_tests PRIVATE winios_core)
    winios_set_warnings(stage_t1_ios_target_tests)
    add_test(NAME stage_t1_ios_target_tests COMMAND stage_t1_ios_target_tests)
    set_tests_properties(stage_t1_ios_target_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_t2_three_image_chain_tests tests/unit/stage_t2_three_image_chain_tests.cpp)
    target_link_libraries(stage_t2_three_image_chain_tests PRIVATE winios_core)
    winios_set_warnings(stage_t2_three_image_chain_tests)
    add_test(NAME stage_t2_three_image_chain_tests COMMAND stage_t2_three_image_chain_tests)
    set_tests_properties(stage_t2_three_image_chain_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_t3_initializers_and_tlv_tests tests/unit/stage_t3_initializers_and_tlv_tests.cpp)
    target_link_libraries(stage_t3_initializers_and_tlv_tests PRIVATE winios_core)
    winios_set_warnings(stage_t3_initializers_and_tlv_tests)
    add_test(NAME stage_t3_initializers_and_tlv_tests COMMAND stage_t3_initializers_and_tlv_tests)
    set_tests_properties(stage_t3_initializers_and_tlv_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_t4_arm64e_probe_tests tests/unit/stage_t4_arm64e_probe_tests.cpp)
    target_link_libraries(stage_t4_arm64e_probe_tests PRIVATE winios_core)
    winios_set_warnings(stage_t4_arm64e_probe_tests)
    add_test(NAME stage_t4_arm64e_probe_tests COMMAND stage_t4_arm64e_probe_tests)
    set_tests_properties(stage_t4_arm64e_probe_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_u1_guest_minidyld_tests tests/unit/stage_u1_guest_minidyld_tests.cpp)
    target_link_libraries(stage_u1_guest_minidyld_tests PRIVATE winios_core)
    winios_set_warnings(stage_u1_guest_minidyld_tests)
    add_test(NAME stage_u1_guest_minidyld_tests COMMAND stage_u1_guest_minidyld_tests)
    set_tests_properties(stage_u1_guest_minidyld_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_u2_libsystem_lite_tests tests/unit/stage_u2_libsystem_lite_tests.cpp)
    target_link_libraries(stage_u2_libsystem_lite_tests PRIVATE winios_core)
    winios_set_warnings(stage_u2_libsystem_lite_tests)
    add_test(NAME stage_u2_libsystem_lite_tests COMMAND stage_u2_libsystem_lite_tests)
    set_tests_properties(stage_u2_libsystem_lite_tests PROPERTIES TIMEOUT 60)

    add_executable(stage_u3_real_mach_trap_tests tests/unit/stage_u3_real_mach_trap_tests.cpp)
    target_link_libraries(stage_u3_real_mach_trap_tests PRIVATE winios_core)
    winios_set_warnings(stage_u3_real_mach_trap_tests)
    add_test(NAME stage_u3_real_mach_trap_tests COMMAND stage_u3_real_mach_trap_tests)
    set_tests_properties(stage_u3_real_mach_trap_tests PROPERTIES TIMEOUT 60)

    add_executable(stage53_manifest_tests tests/unit/stage53_manifest_tests.cpp)
    target_link_libraries(stage53_manifest_tests PRIVATE winios_core)
    winios_set_warnings(stage53_manifest_tests)
    add_test(NAME elf_manifest_tests COMMAND stage53_manifest_tests)
    add_test(NAME elf_session_cli_tests COMMAND ${CMAKE_COMMAND}
        "-DAPP=$<TARGET_FILE:win_ios>" "-DFIXTURES=$<TARGET_FILE:stage49_elf_fixtures>"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing/stage53/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/stage53_cli_tests.cmake")
    set_tests_properties(elf_manifest_tests elf_session_cli_tests PROPERTIES TIMEOUT 120)

    add_executable(stage49_elf_fixtures tests/support/stage49_elf_fixtures.cpp)
    winios_set_warnings(stage49_elf_fixtures)
    foreach(suite success failures arguments)
        add_test(NAME elf_cli_${suite}_tests COMMAND ${CMAKE_COMMAND}
            "-DAPP=$<TARGET_FILE:win_ios>"
            "-DFIXTURES=$<TARGET_FILE:stage49_elf_fixtures>"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/Testing/elf_cli/$<CONFIG>/${suite}"
            "-DSUITE=${suite}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/stage49_elf_cli_tests.cmake")
        set_tests_properties(elf_cli_${suite}_tests PROPERTIES TIMEOUT 60)
    endforeach()

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

    add_executable(pmu_reference_tests
        tests/unit/pmu_reference_tests.cpp
    )
    target_link_libraries(pmu_reference_tests PRIVATE winios_core)
    winios_set_warnings(pmu_reference_tests)
    add_test(NAME pmu_reference_tests COMMAND pmu_reference_tests)

    add_executable(pmu_recording_replay_tests
        tests/unit/pmu_recording_replay_tests.cpp
    )
    target_link_libraries(pmu_recording_replay_tests PRIVATE winios_core)
    winios_set_warnings(pmu_recording_replay_tests)
    add_test(NAME pmu_recording_replay_tests COMMAND pmu_recording_replay_tests)

    add_executable(pmu_benchmarks
        tests/unit/pmu_benchmarks.cpp
    )
    target_link_libraries(pmu_benchmarks PRIVATE winios_core)
    winios_set_warnings(pmu_benchmarks)

    add_executable(timer_tests
        tests/unit/timer_tests.cpp
    )
    target_link_libraries(timer_tests PRIVATE winios_core)
    winios_set_warnings(timer_tests)
    add_test(NAME timer_tests COMMAND timer_tests)

    add_executable(stage41_timer_tests
        tests/unit/stage41_timer_tests.cpp
    )
    target_link_libraries(stage41_timer_tests PRIVATE winios_core)
    winios_set_warnings(stage41_timer_tests)
    add_test(NAME generic_timer_register_tests COMMAND stage41_timer_tests)
    add_test(NAME physical_timer_tests COMMAND stage41_timer_tests)
    add_test(NAME virtual_timer_tests COMMAND stage41_timer_tests)
    add_test(NAME timer_privilege_tests COMMAND stage41_timer_tests)
    add_test(NAME timer_gic_integration_tests COMMAND stage41_timer_tests)
    add_test(NAME timer_smp_isolation_tests COMMAND stage41_timer_tests)
    add_test(NAME timer_snapshot_replay_tests COMMAND stage41_timer_tests)
    add_test(NAME timer_reference_tests COMMAND stage41_timer_tests)

    add_executable(generic_timer_benchmarks
        tests/unit/generic_timer_benchmarks.cpp
    )
    target_link_libraries(generic_timer_benchmarks PRIVATE winios_core)
    winios_set_warnings(generic_timer_benchmarks)

    add_executable(stage42_fp_simd_tests
        tests/unit/stage42_fp_simd_tests.cpp
    )
    target_link_libraries(stage42_fp_simd_tests PRIVATE winios_core)
    winios_set_warnings(stage42_fp_simd_tests)
    add_test(NAME fp_simd_register_tests COMMAND stage42_fp_simd_tests)
    add_test(NAME fpcr_fpsr_tests COMMAND stage42_fp_simd_tests)
    add_test(NAME scalar_fp_execution_tests COMMAND stage42_fp_simd_tests)
    add_test(NAME advanced_simd_execution_tests COMMAND stage42_fp_simd_tests)
    add_test(NAME fp_simd_trap_tests COMMAND stage42_fp_simd_tests)
    add_test(NAME fp_simd_smp_tests COMMAND stage42_fp_simd_tests)
    add_test(NAME fp_simd_snapshot_replay_tests COMMAND stage42_fp_simd_tests)
    add_test(NAME fp_simd_reference_tests COMMAND stage42_fp_simd_tests)

    add_executable(stage43_exclusive_tests
        tests/unit/stage43_exclusive_tests.cpp
    )
    target_link_libraries(stage43_exclusive_tests PRIVATE winios_core)
    winios_set_warnings(stage43_exclusive_tests)
    add_test(NAME exclusive_load_store_tests COMMAND stage43_exclusive_tests)
    add_test(NAME exclusive_monitor_tests COMMAND stage43_exclusive_tests)
    add_test(NAME exclusive_pair_tests COMMAND stage43_exclusive_tests)
    add_test(NAME clrex_tests COMMAND stage43_exclusive_tests)
    add_test(NAME acquire_release_tests COMMAND stage43_exclusive_tests)
    add_test(NAME exclusive_smp_tests COMMAND stage43_exclusive_tests)
    add_test(NAME exclusive_snapshot_replay_tests COMMAND stage43_exclusive_tests)
    add_test(NAME exclusive_reference_tests COMMAND stage43_exclusive_tests)

    add_executable(stage44_lse_tests
        tests/unit/stage44_lse_tests.cpp
    )
    target_link_libraries(stage44_lse_tests PRIVATE winios_core)
    winios_set_warnings(stage44_lse_tests)
    add_test(NAME lse_cas_tests COMMAND stage44_lse_tests)
    add_test(NAME lse_swap_tests COMMAND stage44_lse_tests)
    add_test(NAME lse_atomic_arithmetic_tests COMMAND stage44_lse_tests)
    add_test(NAME lse_atomic_logical_tests COMMAND stage44_lse_tests)
    add_test(NAME lse_acquire_release_tests COMMAND stage44_lse_tests)
    add_test(NAME lse_smp_tests COMMAND stage44_lse_tests)
    add_test(NAME lse_memory_fault_tests COMMAND stage44_lse_tests)
    add_test(NAME lse_snapshot_replay_tests COMMAND stage44_lse_tests)
    add_test(NAME lse_reference_tests COMMAND stage44_lse_tests)

    add_executable(stage45_cache_tests
        tests/unit/stage45_cache_tests.cpp
    )
    target_link_libraries(stage45_cache_tests PRIVATE winios_core)
    winios_set_warnings(stage45_cache_tests)
    add_test(NAME cache_register_tests COMMAND stage45_cache_tests)
    add_test(NAME dc_zva_tests COMMAND stage45_cache_tests)
    add_test(NAME dc_maintenance_tests COMMAND stage45_cache_tests)
    add_test(NAME ic_maintenance_tests COMMAND stage45_cache_tests)
    add_test(NAME cache_barrier_tests COMMAND stage45_cache_tests)
    add_test(NAME self_modifying_code_tests COMMAND stage45_cache_tests)
    add_test(NAME cache_smp_dma_tests COMMAND stage45_cache_tests)
    add_test(NAME cache_snapshot_replay_tests COMMAND stage45_cache_tests)
    add_test(NAME cache_reference_tests COMMAND stage45_cache_tests)

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

# Phase 52 Stage 39 Tests
add_executable(stage39_debug_tests tests/unit/stage39_debug_tests.cpp)
target_link_libraries(stage39_debug_tests PRIVATE winios_core)
add_test(NAME DebugRegisterArchitectureTests COMMAND stage39_debug_tests --suite 1)
add_test(NAME HardwareBreakpointMatchingTests COMMAND stage39_debug_tests --suite 2)
add_test(NAME HardwareWatchpointMatchingTests COMMAND stage39_debug_tests --suite 3)
add_test(NAME DebugPrivilegeEnforcementTests COMMAND stage39_debug_tests --suite 4)
add_test(NAME DebugExceptionSyndromeTests COMMAND stage39_debug_tests --suite 5)
add_test(NAME DebugVectorSelectionTests COMMAND stage39_debug_tests --suite 6)
add_test(NAME SingleStepExecutionTests COMMAND stage39_debug_tests --suite 7)
add_test(NAME DebugMmuInteractionTests COMMAND stage39_debug_tests --suite 8)
add_test(NAME DebugSmpIsolationTests COMMAND stage39_debug_tests --suite 9)
add_test(NAME DebugSnapshotReplayTests COMMAND stage39_debug_tests --suite 10)
add_test(NAME DebugReferenceDifferentialTests COMMAND stage39_debug_tests --suite 11)
add_test(NAME Stage39DebugFuzzTests COMMAND stage39_debug_tests --suite 12)
add_test(NAME Stage39DebugEnduranceTests COMMAND stage39_debug_tests --suite 13)

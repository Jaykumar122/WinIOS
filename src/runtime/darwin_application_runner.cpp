#include "runtime/darwin_application_runner.h"
#include "runtime/darwin_process_startup.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace winios::runtime {

DarwinApplicationRunner::DarwinApplicationRunner(const ApplicationConfig& config)
    : config_(config),
      memory_(std::make_unique<Memory>(config.memory_size)),
      cpu_(std::make_unique<CPU>()),
      bsd_(std::make_shared<DarwinBsdSubsystem>()),
      dyld_(std::make_unique<loader::DyldEngine>()),
      runtime_lib_(std::make_unique<DarwinRuntimeLibrary>()),
      thread_runtime_(std::make_unique<DarwinThreadRuntime>()),
      syscall_dispatcher_(std::make_unique<DarwinSyscallDispatcher>()),
      sync_manager_(std::make_unique<DarwinSyncManager>(thread_runtime_.get())) {

    executable_module_name_ = config_.executable_path;
}

void DarwinApplicationRunner::map_image_segments(
    const loader::UserspaceLoadResult& load_res,
    const std::uint8_t* bytes,
    std::size_t size) {

    for (const auto& seg : load_res.segments) {
        if (seg.is_pagezero) {
            continue; // __PAGEZERO is left unmapped
        }

        if (seg.filesize > 0 && seg.fileoff + seg.filesize <= size) {
            std::uint64_t target_addr = (seg.vmaddr < memory_->size())
                                            ? seg.vmaddr
                                            : (seg.vmaddr % memory_->size());

            for (std::size_t i = 0; i < seg.filesize; ++i) {
                if (target_addr + i < memory_->size()) {
                    memory_->write_u8(static_cast<std::size_t>(target_addr + i), bytes[seg.fileoff + i]);
                }
            }
        }
    }
}

bool DarwinApplicationRunner::load_executable(
    const std::uint8_t* macho_bytes,
    std::size_t macho_size,
    std::string& out_error) {

    if (!macho_bytes || macho_size < sizeof(loader::MachOHeader64Raw)) {
        out_error = "Invalid executable Mach-O buffer";
        return false;
    }

    loader::UserspaceLoadResult load_res;
    if (!loader::UserspaceMachOLoader::parse_metadata(macho_bytes, macho_size, load_res)) {
        out_error = "Failed to parse executable Mach-O: " + load_res.error_message;
        return false;
    }

    std::string seg_err;
    if (loader::UserspaceMachOLoader::validate_segments(load_res.segments, macho_size, 0x4000ULL, seg_err) != loader::UserspaceMachOLoadStatus::Success) {
        out_error = "Failed to validate segments: " + seg_err;
        return false;
    }

    map_image_segments(load_res, macho_bytes, macho_size);
    main_entry_point_ = load_res.entry_point;

    loader::DyldLoadedModule mod;
    mod.name = config_.executable_path;
    mod.install_name = config_.executable_path;
    mod.base_address = load_res.segments.empty() ? 0x100000000ULL : load_res.segments[0].vmaddr;
    mod.initializers = load_res.mod_init_funcs;

    for (const auto& dep : load_res.dylibs) {
        mod.dependencies.push_back(dep.path);
    }

    dyld_->register_module(std::move(mod));
    return true;
}

bool DarwinApplicationRunner::load_library(
    const std::string& install_name,
    const std::uint8_t* dylib_bytes,
    std::size_t dylib_size,
    std::string& out_error) {

    if (!dylib_bytes || dylib_size < sizeof(loader::MachOHeader64Raw)) {
        out_error = "Invalid dylib Mach-O buffer";
        return false;
    }

    loader::UserspaceLoadResult load_res;
    if (!loader::UserspaceMachOLoader::parse_metadata(dylib_bytes, dylib_size, load_res)) {
        out_error = "Failed to parse dylib: " + load_res.error_message;
        return false;
    }

    std::string seg_err;
    if (loader::UserspaceMachOLoader::validate_segments(load_res.segments, dylib_size, 0x4000ULL, seg_err) != loader::UserspaceMachOLoadStatus::Success) {
        out_error = "Failed to validate dylib segments: " + seg_err;
        return false;
    }

    map_image_segments(load_res, dylib_bytes, dylib_size);

    loader::DyldLoadedModule mod;
    mod.name = install_name;
    mod.install_name = install_name;
    mod.base_address = load_res.segments.empty() ? 0x180000000ULL : load_res.segments[0].vmaddr;
    mod.initializers = load_res.mod_init_funcs;

    dyld_->register_module(std::move(mod));
    return true;
}

bool DarwinApplicationRunner::link_and_initialize(std::string& out_error) {
    if (config_.enable_libsystem_stubs) {
        if (!runtime_lib_->stage_into_memory(*memory_, out_error)) {
            return false;
        }
        runtime_lib_->register_with_dyld(*dyld_);
    }

    if (config_.run_initializers) {
        std::vector<std::string> init_mods;
        auto inits = dyld_->compute_topological_initializers(executable_module_name_, init_mods);
        initializers_run_ = inits.size();
    }

    is_linked_ = true;
    return true;
}

bool DarwinApplicationRunner::prepare_execution(std::string& out_error) {
    if (!is_linked_) {
        if (!link_and_initialize(out_error)) {
            return false;
        }
    }

    // 1. Create BSD Process and bind to dispatcher
    pid_ = bsd_->create_process(1, config_.executable_path, 501);
    syscall_dispatcher_->bind_bsd_subsystem(bsd_, pid_);

    // 2. Allocate stack with guard page
    constexpr std::size_t kStackSize = 0x80000; // 512 KB
    constexpr std::size_t kGuardSize = 0x4000;  // 16 KB
    if (!thread_runtime_->allocate_thread_stack(*bsd_, pid_, kStackSize, kGuardSize, stack_base_, initial_sp_)) {
        out_error = "Failed to allocate thread stack with guard page";
        return false;
    }

    // 3. Stage Darwin initial stack frame (argc, argv, envp, apple)
    DarwinProcessStartupConfig startup_cfg{};
    startup_cfg.stack_top = initial_sp_;
    startup_cfg.entry_point = main_entry_point_;
    startup_cfg.executable_path = config_.executable_path;
    startup_cfg.argv = config_.argv;
    startup_cfg.envp = config_.envp;
    startup_cfg.populate_registers = false;

    DarwinProcessStartupInfo startup_info{};
    if (!DarwinProcessStartup::setup_process(*cpu_, *memory_, startup_cfg, startup_info, out_error)) {
        return false;
    }
    initial_sp_ = startup_info.initial_sp;

    // 4. Initialize main thread control block (pthread_t)
    std::uint64_t pthread_va = stack_base_ + kGuardSize;
    if (!thread_runtime_->initialize_thread_structure(
            *memory_, pthread_va, tid_, stack_base_ + kGuardSize, kStackSize, 0, 0, pthread_va + 0x200, 0x800)) {
        out_error = "Failed to initialize thread structure";
        return false;
    }

    // 5. Configure CPU state
    cpu_->reset();
    cpu_->set_architectural_exceptions(false);
    cpu_->set_current_el(0);
    cpu_->set_cpacr_el1(0x00300000u); // Enable FP/SIMD (FPEN = 0b11)
    thread_runtime_->bind_cpu_thread(*cpu_, pthread_va, pthread_va + 0x200);
    cpu_->set_stack_pointer(initial_sp_);

    std::uint64_t target_pc = (main_entry_point_ < memory_->size())
                                  ? main_entry_point_
                                  : (main_entry_point_ % memory_->size());
    cpu_->set_program_counter(target_pc);

    is_prepared_ = true;
    return true;
}

ApplicationRunResult DarwinApplicationRunner::run() {
    ApplicationRunResult result{};
    if (!is_prepared_) {
        std::string err;
        if (!prepare_execution(err)) {
            result.termination_reason = "PreparationFailed: " + err;
            return result;
        }
    }

    result.initializers_run = initializers_run_;

    while (result.instructions_retired < config_.max_instructions) {
        if (syscall_dispatcher_->has_exited()) {
            result.terminated = true;
            result.exit_code = syscall_dispatcher_->exit_code();
            result.termination_reason = "SYS_exit";
            result.success = true;
            break;
        }

        std::uint64_t pc = cpu_->get_program_counter();
        if (pc >= memory_->size()) {
            result.terminated = true;
            result.termination_reason = "OutOfBoundsPC";
            break;
        }

        std::uint32_t instr = memory_->read_u32(static_cast<std::size_t>(pc));

        // Intercept Darwin SVC #0x80 syscall trap (0xD4001001) or general SVC
        if ((instr & 0xFFE0001Fu) == 0xD4000001u) {
            std::uint16_t imm = static_cast<std::uint16_t>((instr >> 5u) & 0xFFFFu);
            syscall_dispatcher_->dispatch(*cpu_, *memory_, imm);
            result.syscalls_handled++;
            cpu_->set_program_counter(pc + 4);

            if (syscall_dispatcher_->has_exited()) {
                result.terminated = true;
                result.exit_code = syscall_dispatcher_->exit_code();
                result.termination_reason = "SYS_exit";
                result.success = true;
                break;
            }
            continue;
        }

        try {
            cpu_->step(*memory_);
            result.instructions_retired++;
        } catch (const std::exception& ex) {
            result.terminated = true;
            result.termination_reason = std::string("CPU_Fault: ") + ex.what();
            break;
        }
    }

    if (!result.terminated) {
        result.termination_reason = "MaxInstructionsReached";
    }

    result.stdout_output = syscall_dispatcher_->stdout_buffer();
    return result;
}

ApplicationRunResult DarwinApplicationRunner::run_binary(
    const std::uint8_t* macho_bytes,
    std::size_t macho_size) {

    std::string err;
    if (!load_executable(macho_bytes, macho_size, err)) {
        ApplicationRunResult res{};
        res.termination_reason = "LoadFailed: " + err;
        return res;
    }

    return run();
}

} // namespace winios::runtime

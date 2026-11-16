#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_runtime_library.h"
#include "runtime/darwin_syscall_dispatcher.h"
#include "runtime/darwin_thread_runtime.h"
#include "runtime/darwin_sync_primitives.h"

namespace winios::runtime {

struct ApplicationConfig {
    std::string              executable_path{"/usr/bin/app"};
    std::vector<std::string> argv;
    std::vector<std::string> envp;
    std::size_t              max_instructions{1000000};
    std::size_t              memory_size{0x4000000}; // 64 MB default
    bool                     run_initializers{true};
    bool                     enable_libsystem_stubs{true};
};

struct ApplicationRunResult {
    bool        terminated{false};
    int         exit_code{0};
    std::size_t instructions_retired{0};
    std::size_t syscalls_handled{0};
    std::size_t initializers_run{0};
    std::string stdout_output;
    std::string stderr_output;
    std::string termination_reason; // "SYS_exit", "MaxInstructionsReached", "CPU_Fault"
    bool        success{false};
};

// -----------------------------------------------------------------------------
// DarwinApplicationRunner: End-to-End Darwin ARM64 Application Execution Lifecycle
// -----------------------------------------------------------------------------
class DarwinApplicationRunner {
public:
    explicit DarwinApplicationRunner(const ApplicationConfig& config = {});
    ~DarwinApplicationRunner() = default;

    // Load executable Mach-O binary bytes into memory and register with DyldEngine
    bool load_executable(const std::uint8_t* macho_bytes, std::size_t macho_size, std::string& out_error);

    // Load dependent dynamic library into memory and register with DyldEngine
    bool load_library(const std::string& install_name, const std::uint8_t* dylib_bytes, std::size_t dylib_size, std::string& out_error);

    // Link dynamic dependencies, resolve imports/chained fixups, and run static initializers
    bool link_and_initialize(std::string& out_error);

    // Allocate stack with guard page, stage Darwin arguments, bind thread TCB, and set CPU registers
    bool prepare_execution(std::string& out_error);

    // Step CPU instruction loop until process termination or max instruction limit
    ApplicationRunResult run();

    // End-to-end all-in-one runner helper
    ApplicationRunResult run_binary(const std::uint8_t* macho_bytes, std::size_t macho_size);

    // Subsystem accessors
    [[nodiscard]] Memory& memory() noexcept { return *memory_; }
    [[nodiscard]] CPU& cpu() noexcept { return *cpu_; }
    [[nodiscard]] DarwinBsdSubsystem& bsd() noexcept { return *bsd_; }
    [[nodiscard]] loader::DyldEngine& dyld() noexcept { return *dyld_; }
    [[nodiscard]] DarwinThreadRuntime& thread_runtime() noexcept { return *thread_runtime_; }
    [[nodiscard]] DarwinSyscallDispatcher& syscall_dispatcher() noexcept { return *syscall_dispatcher_; }
    [[nodiscard]] DarwinSyncManager& sync_manager() noexcept { return *sync_manager_; }

private:
    ApplicationConfig                         config_;
    std::unique_ptr<Memory>                   memory_;
    std::unique_ptr<CPU>                      cpu_;
    std::shared_ptr<DarwinBsdSubsystem>       bsd_;
    std::unique_ptr<loader::DyldEngine>       dyld_;
    std::unique_ptr<DarwinRuntimeLibrary>     runtime_lib_;
    std::unique_ptr<DarwinThreadRuntime>      thread_runtime_;
    std::unique_ptr<DarwinSyscallDispatcher>  syscall_dispatcher_;
    std::unique_ptr<DarwinSyncManager>        sync_manager_;

    std::uint32_t pid_{1};
    std::uint64_t tid_{100};
    std::uint64_t main_entry_point_{0};
    std::uint64_t initial_sp_{0};
    std::uint64_t stack_base_{0};
    std::size_t   initializers_run_{0};
    bool          is_prepared_{false};
    bool          is_linked_{false};
    std::string   executable_module_name_;

    void map_image_segments(const loader::UserspaceLoadResult& load_res, const std::uint8_t* bytes, std::size_t size);
};

} // namespace winios::runtime

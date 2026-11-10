#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cpu/cpu.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"

namespace winios::runtime {

class ExecutionDiagnostics;

// -----------------------------------------------------------------------------
// Execution Origin: Strictly identifies whether an action was Host or Guest
// -----------------------------------------------------------------------------
enum class ExecutionOrigin {
    Host,  // Host C++ runtime, loader, or emulator bookkeeping
    Guest  // ARM64 CPU instruction execution, architectural register/MMU update
};

[[nodiscard]] const char* execution_origin_string(ExecutionOrigin origin) noexcept;

// -----------------------------------------------------------------------------
// Execution Stage in the End-to-End Pipeline
// -----------------------------------------------------------------------------
enum class PipelineExecutionStage {
    CpuReset,
    BootArgsStaging,
    DeviceTreeHandoff,
    MmuActivation,
    KernelEntry,
    RootfsHandoff,
    MachOLoading,
    StackSetup,
    UserspaceEntry,
    GuestInstructionStep,
    BsdSyscallTrap,
    MachTrap,
    ProcessExit,
    ExceptionFault
};

[[nodiscard]] const char* pipeline_stage_string(PipelineExecutionStage stage) noexcept;

// -----------------------------------------------------------------------------
// Structured Trace Record for End-to-End Audit
// -----------------------------------------------------------------------------
struct DarwinPipelineTraceEvent {
    ExecutionOrigin        origin{ExecutionOrigin::Host};
    PipelineExecutionStage stage{PipelineExecutionStage::CpuReset};
    std::uint64_t          pc{0};
    std::uint64_t          guest_instruction_count{0};
    std::string            description;
};

// -----------------------------------------------------------------------------
// Configuration for Userspace Mach-O Execution
// -----------------------------------------------------------------------------
struct DarwinPipelineConfig {
    std::size_t max_instructions{100000};
    std::uint64_t vbar_el1{0x00010000ULL};
    std::uint64_t stack_top{0x02000000ULL}; // 32MB default within RAM
    std::string executable_path{"/sbin/launchd"};
    std::vector<std::string> argv;
    std::vector<std::string> envp;
    bool enable_trace{true};
    std::function<void(Memory&)> pre_execution_hook;
};

// -----------------------------------------------------------------------------
// End-to-End Execution Result
// -----------------------------------------------------------------------------
struct DarwinPipelineResult {
    bool                               process_exited{false};
    int                                exit_code{0};
    std::uint32_t                      pid{0};
    std::uint64_t                      guest_instructions_retired{0};
    std::size_t                        host_events_count{0};
    std::size_t                        guest_events_count{0};
    std::size_t                        bsd_syscalls_handled{0};
    std::size_t                        mach_traps_handled{0};
    std::string                        stdout_output;
    std::string                        stderr_output;
    std::string                        error_message;
    std::vector<DarwinPipelineTraceEvent> trace;

    [[nodiscard]] bool is_clean_exit() const noexcept {
        return process_exited && (exit_code == 0) && error_message.empty();
    }
};

// -----------------------------------------------------------------------------
// DarwinUserspaceEngine: Connects Mach-O loader, CPU, Memory, BSD & Mach IPC
// -----------------------------------------------------------------------------
class DarwinUserspaceEngine {
public:
    DarwinUserspaceEngine(
        CPU& cpu,
        Memory& memory,
        DarwinBsdSubsystem& bsd,
        MachIpcSubsystem& ipc);
    ~DarwinUserspaceEngine() = default;

    // Load Mach-O binary bytes into guest memory, construct stack, and run
    DarwinPipelineResult execute_macho(
        const std::uint8_t* macho_bytes,
        std::size_t macho_size,
        const DarwinPipelineConfig& config,
        std::uint32_t pid = 1);

    // Stage U1: Guest-Side Dynamic Linker Execution (minidyld)
    DarwinPipelineResult execute_macho_with_guest_dyld(
        const std::uint8_t* dyld_bytes,
        std::size_t dyld_size,
        const std::uint8_t* macho_bytes,
        std::size_t macho_size,
        const DarwinPipelineConfig& config,
        std::uint32_t pid = 1);

    // Step a single instruction; returns true if execution can continue
    bool step_one(DarwinPipelineResult& result, const DarwinPipelineConfig& config);

    // Direct SVC trap dispatching
    void handle_svc(std::uint16_t svc_imm, DarwinPipelineResult& result);

    // Tracing helper
    void log_event(
        DarwinPipelineResult& result,
        ExecutionOrigin origin,
        PipelineExecutionStage stage,
        std::uint64_t pc,
        std::string description);

    // End-to-End Synthetic Pipeline: Boot -> Rootfs -> Mach-O Userspace -> App
    static DarwinPipelineResult run_complete_end_to_end_pipeline(
        CPU& cpu,
        Memory& memory,
        DarwinBsdSubsystem& bsd,
        MachIpcSubsystem& ipc);

    // Process and Thread Management
    [[nodiscard]] std::uint32_t current_pid() const noexcept { return current_pid_; }
    void set_current_pid(std::uint32_t pid) noexcept { current_pid_ = pid; }
    [[nodiscard]] std::uint64_t current_tid() const noexcept { return current_tid_; }
    void set_current_tid(std::uint64_t tid) noexcept { current_tid_ = tid; }
    bool save_thread_context(std::uint64_t tid);
    bool load_thread_context(std::uint64_t tid);
    bool switch_thread(std::uint64_t target_tid);

    // Diagnostics Hook
    void set_diagnostics(ExecutionDiagnostics* diag) noexcept { diagnostics_ = diag; }
    [[nodiscard]] ExecutionDiagnostics* diagnostics() const noexcept { return diagnostics_; }

private:
    CPU&                   cpu_;
    Memory&                memory_;
    DarwinBsdSubsystem&    bsd_;
    MachIpcSubsystem&      ipc_;
    ExecutionDiagnostics*  diagnostics_{nullptr};
    std::uint32_t          current_pid_{1};
    std::uint64_t          current_tid_{1};

    void handle_bsd_syscall(DarwinPipelineResult& result);
    void handle_mach_trap(DarwinPipelineResult& result);
};

} // namespace winios::runtime

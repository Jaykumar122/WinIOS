#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace winios::runtime {

// -----------------------------------------------------------------------------
// Register Diff Record
// -----------------------------------------------------------------------------
struct RegisterDiff {
    std::uint8_t  reg_index{0};
    std::uint64_t old_value{0};
    std::uint64_t new_value{0};
};

// -----------------------------------------------------------------------------
// Instruction Step Record
// -----------------------------------------------------------------------------
struct InstructionTraceStep {
    std::uint64_t              step_index{0};
    std::uint64_t              pc_before{0};
    std::uint64_t              pc_after{0};
    std::uint32_t              instruction_raw{0};
    std::uint8_t               current_el{0};
    std::uint64_t              sp_value{0};
    std::uint32_t              nzcv{0};
    std::uint64_t              timestamp_ns{0};
    std::vector<RegisterDiff>  register_diffs;
    std::string                disassembly;
};

// -----------------------------------------------------------------------------
// Memory Operation Record
// -----------------------------------------------------------------------------
enum class MemoryOpType {
    Read,
    Write
};

struct MemoryOpRecord {
    MemoryOpType  type{MemoryOpType::Read};
    std::uint64_t address{0};
    std::size_t   size{0};
    std::uint64_t value{0};
};

// -----------------------------------------------------------------------------
// Syscall / Trap Trace Record
// -----------------------------------------------------------------------------
enum class TrapKind {
    BsdSyscall,
    MachTrap
};

struct SyscallTraceRecord {
    TrapKind                    kind{TrapKind::BsdSyscall};
    std::int32_t                number{0};
    std::string                 name;
    std::array<std::uint64_t, 6> arguments{};
    std::uint64_t               return_value{0};
    bool                        is_error{false};
    std::int32_t                errno_code{0};
};

// -----------------------------------------------------------------------------
// Process Lifecycle Record
// -----------------------------------------------------------------------------
struct ProcessLifecycleRecord {
    std::uint32_t pid{0};
    std::string   executable_path;
    bool          exited{false};
    int           exit_code{0};
    std::uint64_t instructions_retired{0};
    std::size_t   memory_reads{0};
    std::size_t   memory_writes{0};
    std::size_t   bsd_syscalls_count{0};
    std::size_t   mach_traps_count{0};
};

// -----------------------------------------------------------------------------
// ExecutionDiagnostics: Complete Runtime Tracer & Conformance Inspector
// -----------------------------------------------------------------------------
class ExecutionDiagnostics {
public:
    ExecutionDiagnostics() = default;
    ~ExecutionDiagnostics() = default;

    // Reset all logged diagnostic events
    void reset();

    // Instruction disassembly
    [[nodiscard]] static std::string disassemble_instruction(std::uint32_t raw_instr, std::uint64_t pc = 0);

    // Log individual instruction step with before/after state
    void log_instruction_step(
        std::uint64_t pc_before,
        std::uint64_t pc_after,
        std::uint32_t instruction_raw,
        std::uint8_t current_el,
        const std::vector<RegisterDiff>& diffs,
        std::string_view disassembly = "",
        std::uint64_t sp_value = 0,
        std::uint32_t nzcv = 0,
        std::uint64_t timestamp_ns = 0);

    // Log memory access
    void log_memory_op(
        MemoryOpType type,
        std::uint64_t address,
        std::size_t size,
        std::uint64_t value);

    // Log BSD syscall or Mach trap invocation
    void log_syscall(
        TrapKind kind,
        std::int32_t number,
        std::string_view name,
        const std::array<std::uint64_t, 6>& args,
        std::uint64_t return_value,
        bool is_error = false,
        std::int32_t errno_code = 0);

    // Update process lifecycle information
    void record_process_start(std::uint32_t pid, std::string_view path);
    void record_process_exit(int exit_code);

    // Diagnostics Inspection Queries
    [[nodiscard]] std::size_t step_count() const noexcept;
    [[nodiscard]] std::size_t memory_op_count() const noexcept;
    [[nodiscard]] std::size_t syscall_count() const noexcept;

    [[nodiscard]] const InstructionTraceStep* get_step(std::size_t index) const;
    [[nodiscard]] const MemoryOpRecord* get_memory_op(std::size_t index) const;
    [[nodiscard]] const SyscallTraceRecord* get_syscall(std::size_t index) const;
    [[nodiscard]] const ProcessLifecycleRecord& lifecycle() const noexcept { return lifecycle_; }

    // Query filters
    [[nodiscard]] std::vector<InstructionTraceStep> find_steps_by_pc(std::uint64_t pc) const;
    [[nodiscard]] std::vector<InstructionTraceStep> find_steps_by_instruction(std::uint32_t mask, std::uint32_t val) const;
    [[nodiscard]] std::vector<InstructionTraceStep> find_steps_by_reg_write(std::uint8_t reg_index) const;
    [[nodiscard]] std::vector<SyscallTraceRecord> find_syscalls_by_number(std::int32_t number) const;

    // Structured Serialization
    [[nodiscard]] std::string export_trace_json() const;
    [[nodiscard]] std::string export_trace_csv() const;
    [[nodiscard]] std::string export_syscalls_csv() const;

    // Human-readable formatting
    [[nodiscard]] std::string format_summary() const;
    [[nodiscard]] std::string format_trace_report(std::size_t max_steps = 100) const;

private:
    [[nodiscard]] std::string format_summary_unlocked() const;

    mutable std::mutex                  lock_;
    std::vector<InstructionTraceStep>   steps_;
    std::vector<MemoryOpRecord>         memory_ops_;
    std::vector<SyscallTraceRecord>     syscalls_;
    ProcessLifecycleRecord              lifecycle_{};
};

} // namespace winios::runtime

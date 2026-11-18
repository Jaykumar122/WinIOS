#pragma once

#include "cpu/cpu.h"
#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace winios::debugger {

struct RegisterSnapshot {
    std::array<uint64_t, 31> x{};
    uint64_t sp{0};
    uint64_t pc{0};
    uint64_t pstate{0};
};

struct InstructionTraceRecord {
    uint64_t pc{0};
    uint32_t opcode{0};
    uint64_t sequence{0};
};

struct SyscallTraceRecord {
    uint64_t sequence{0};
    uint32_t sys_num{0};
    uint64_t arg0{0};
    uint64_t arg1{0};
    int32_t  result{0};
};

struct CrashSnapshot {
    uint32_t                        pid{0};
    std::string                     bundle_id;
    std::string                     fault_reason;
    RegisterSnapshot                registers;
    std::vector<InstructionTraceRecord> recent_instructions;
    std::vector<SyscallTraceRecord> recent_syscalls;
};

class ApplicationDebugger {
public:
    static constexpr size_t kMaxTraceCapacity = 64;
    static constexpr size_t kMaxSyscallTraceCapacity = 32;

    ApplicationDebugger() = default;
    ~ApplicationDebugger() = default;

    ApplicationDebugger(const ApplicationDebugger&) = delete;
    ApplicationDebugger& operator=(const ApplicationDebugger&) = delete;

    bool attach(uint32_t target_pid, bool is_authorized) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_authorized) {
            return false; // Unauthorized debugger attachment rejected
        }
        attached_pid_ = target_pid;
        is_attached_ = true;
        return true;
    }

    void detach() {
        std::lock_guard<std::mutex> lock(mutex_);
        is_attached_ = false;
        attached_pid_ = 0;
    }

    [[nodiscard]] bool is_attached() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_attached_;
    }

    [[nodiscard]] uint32_t attached_pid() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return attached_pid_;
    }

    void set_instruction_budget(uint64_t budget) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        instruction_budget_ = budget;
        instructions_executed_ = 0;
    }

    bool record_instruction(uint64_t pc, uint32_t opcode) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++instructions_executed_;
        InstructionTraceRecord rec{pc, opcode, ++sequence_counter_};
        instruction_ring_.push_back(rec);
        while (instruction_ring_.size() > kMaxTraceCapacity) {
            instruction_ring_.pop_front();
        }

        if (instruction_budget_ > 0 && instructions_executed_ >= instruction_budget_) {
            budget_exhausted_ = true;
            return false; // Budget exhausted
        }
        return true;
    }

    void record_syscall(uint32_t sys_num, uint64_t arg0, uint64_t arg1, int32_t result) {
        std::lock_guard<std::mutex> lock(mutex_);
        SyscallTraceRecord rec{++sequence_counter_, sys_num, arg0, arg1, result};
        syscall_ring_.push_back(rec);
        while (syscall_ring_.size() > kMaxSyscallTraceCapacity) {
            syscall_ring_.pop_front();
        }
    }

    [[nodiscard]] bool is_budget_exhausted() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return budget_exhausted_;
    }

    [[nodiscard]] RegisterSnapshot capture_registers(const CPU& cpu) const {
        RegisterSnapshot snap{};
        for (size_t i = 0; i < 31; ++i) {
            snap.x[i] = cpu.read_register(static_cast<uint32_t>(i));
        }
        snap.sp = cpu.get_stack_pointer();
        snap.pc = cpu.get_program_counter();
        snap.pstate = cpu.get_nzcv();
        return snap;
    }

    [[nodiscard]] CrashSnapshot capture_crash_snapshot(uint32_t pid,
                                                      const std::string& bundle_id,
                                                      const CPU& cpu,
                                                      const std::string& fault_reason) const {
        std::lock_guard<std::mutex> lock(mutex_);
        CrashSnapshot crash{};
        crash.pid = pid;
        crash.bundle_id = bundle_id;
        crash.fault_reason = fault_reason;
        crash.registers = capture_registers(cpu);
        crash.recent_instructions.assign(instruction_ring_.begin(), instruction_ring_.end());
        crash.recent_syscalls.assign(syscall_ring_.begin(), syscall_ring_.end());
        return crash;
    }

    [[nodiscard]] CrashSnapshot capture_crash_snapshot_with_regs(uint32_t pid,
                                                                const std::string& bundle_id,
                                                                const std::string& fault_reason,
                                                                const RegisterSnapshot& regs) const {
        std::lock_guard<std::mutex> lock(mutex_);
        CrashSnapshot crash{};
        crash.pid = pid;
        crash.bundle_id = bundle_id;
        crash.fault_reason = fault_reason;
        crash.registers = regs;
        crash.recent_instructions.assign(instruction_ring_.begin(), instruction_ring_.end());
        crash.recent_syscalls.assign(syscall_ring_.begin(), syscall_ring_.end());
        return crash;
    }

private:
    mutable std::mutex                   mutex_;
    bool                                 is_attached_{false};
    uint32_t                             attached_pid_{0};
    uint64_t                             instruction_budget_{0};
    uint64_t                             instructions_executed_{0};
    bool                                 budget_exhausted_{false};
    uint64_t                             sequence_counter_{0};
    std::deque<InstructionTraceRecord>   instruction_ring_;
    std::deque<SyscallTraceRecord>       syscall_ring_;
};

} // namespace winios::debugger

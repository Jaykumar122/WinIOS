#pragma once

#include "debugger/application_debugger.h"
#include "loader/app_launcher.h"
#include "kernel/process_identity.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <sstream>
#include <iomanip>

namespace winios::debugger {

enum class DebugStopReason {
    None,
    StepCompleted,
    BreakpointHit,
    BudgetExhausted,
    Fault,
    Terminated
};

inline const char* debug_stop_reason_to_string(DebugStopReason r) noexcept {
    switch (r) {
    case DebugStopReason::None: return "None";
    case DebugStopReason::StepCompleted: return "StepCompleted";
    case DebugStopReason::BreakpointHit: return "BreakpointHit";
    case DebugStopReason::BudgetExhausted: return "BudgetExhausted";
    case DebugStopReason::Fault: return "Fault";
    case DebugStopReason::Terminated: return "Terminated";
    default: return "Unknown";
    }
}

class DebuggerWorkflowController {
public:
    DebuggerWorkflowController(std::shared_ptr<loader::AppLaunchPipeline> launcher,
                               std::shared_ptr<kernel::ProcessIdentityManager> identity_mgr,
                               std::shared_ptr<ApplicationDebugger> debugger)
        : launcher_(std::move(launcher)),
          identity_mgr_(std::move(identity_mgr)),
          debugger_(std::move(debugger)) {
        if (!debugger_) {
            debugger_ = std::make_shared<ApplicationDebugger>();
        }
    }

    ~DebuggerWorkflowController() = default;

    // Launch application directly into debugging session
    loader::LaunchResult launch_under_debugger(const std::string& bundle_id, uint64_t instruction_budget = 0) {
        if (!launcher_) {
            loader::LaunchResult r{};
            r.status = loader::LaunchStatus::ProcessAllocationFailed;
            r.rejection_reason = "No launcher configured";
            return r;
        }

        auto res = launcher_->launch(bundle_id);
        if (res.status == loader::LaunchStatus::Success) {
            bool attached = debugger_->attach(res.handle.pid, true);
            if (!attached) {
                launcher_->terminate(res.handle);
                res.status = loader::LaunchStatus::ProcessAllocationFailed;
                res.rejection_reason = "Debugger attachment unauthorized or failed";
                return res;
            }
            if (instruction_budget > 0) {
                debugger_->set_instruction_budget(instruction_budget);
            }
            active_handle_ = res.handle;
            active_bundle_id_ = bundle_id;
        }
        return res;
    }

    bool attach_running(kernel::ProcessHandle handle, const std::string& bundle_id, bool is_authorized) {
        if (!identity_mgr_ || !identity_mgr_->is_valid_handle(handle)) {
            return false;
        }
        if (!debugger_->attach(handle.pid, is_authorized)) {
            return false;
        }
        active_handle_ = handle;
        active_bundle_id_ = bundle_id;
        return true;
    }

    void detach() {
        debugger_->detach();
        active_handle_ = kernel::ProcessHandle{};
        active_bundle_id_.clear();
    }

    // Breakpoint management
    void add_breakpoint(uint64_t pc) {
        breakpoints_.insert(pc);
    }

    void remove_breakpoint(uint64_t pc) {
        breakpoints_.erase(pc);
    }

    bool has_breakpoint(uint64_t pc) const noexcept {
        return breakpoints_.find(pc) != breakpoints_.end();
    }

    void clear_breakpoints() {
        breakpoints_.clear();
    }

    // Single instruction step on live CPU & Memory
    DebugStopReason step(CPU& cpu, Memory& mem) {
        if (!debugger_->is_attached()) {
            return DebugStopReason::None;
        }

        uint64_t pc = cpu.get_program_counter();
        if (has_breakpoint(pc)) {
            return DebugStopReason::BreakpointHit;
        }

        uint32_t opcode = 0;
        try {
            opcode = mem.read_u32(static_cast<size_t>(pc));
        } catch (...) {
            return DebugStopReason::Fault;
        }

        if (!debugger_->record_instruction(pc, opcode)) {
            return DebugStopReason::BudgetExhausted;
        }

        cpu.step(mem);

        uint64_t next_pc = cpu.get_program_counter();
        if (has_breakpoint(next_pc)) {
            return DebugStopReason::BreakpointHit;
        }

        return DebugStopReason::StepCompleted;
    }

    // Continuous execution until breakpoint or budget exhausted
    DebugStopReason continue_execution(CPU& cpu, Memory& mem, uint64_t max_steps = 1000000) {
        for (uint64_t i = 0; i < max_steps; ++i) {
            DebugStopReason reason = step(cpu, mem);
            if (reason != DebugStopReason::StepCompleted) {
                return reason;
            }
        }
        return DebugStopReason::BudgetExhausted;
    }

    // Direct recording step (used in lightweight or mocked workflows)
    DebugStopReason record_step(uint64_t current_pc, uint32_t opcode, uint64_t next_pc) {
        if (!debugger_->is_attached()) {
            return DebugStopReason::None;
        }
        if (has_breakpoint(current_pc)) {
            return DebugStopReason::BreakpointHit;
        }
        if (!debugger_->record_instruction(current_pc, opcode)) {
            return DebugStopReason::BudgetExhausted;
        }
        if (has_breakpoint(next_pc)) {
            return DebugStopReason::BreakpointHit;
        }
        return DebugStopReason::StepCompleted;
    }

    // Register & post-mortem analysis
    RegisterSnapshot get_registers(const CPU& cpu) const {
        return debugger_->capture_registers(cpu);
    }

    CrashSnapshot analyze_crash(const std::string& fault_reason, const CPU& cpu) const {
        return debugger_->capture_crash_snapshot(active_handle_.pid, active_bundle_id_, cpu, fault_reason);
    }

    CrashSnapshot analyze_crash_with_regs(const std::string& fault_reason, const RegisterSnapshot& regs) const {
        return debugger_->capture_crash_snapshot_with_regs(active_handle_.pid, active_bundle_id_, fault_reason, regs);
    }

    static std::string format_crash_report(const CrashSnapshot& crash) {
        std::ostringstream ss;
        ss << "=== WINIOS CRASH POST-MORTEM REPORT ===\n"
           << "Target PID:     " << crash.pid << "\n"
           << "Bundle ID:      " << crash.bundle_id << "\n"
           << "Fault Reason:   " << crash.fault_reason << "\n\n"
           << "--- Registers ---\n"
           << "PC:  0x" << std::hex << std::setfill('0') << std::setw(16) << crash.registers.pc << "\n"
           << "SP:  0x" << std::hex << std::setfill('0') << std::setw(16) << crash.registers.sp << "\n"
           << "PSTATE: 0x" << std::hex << crash.registers.pstate << "\n";
        for (size_t i = 0; i < 8; ++i) {
            ss << "X" << i << ": 0x" << std::hex << std::setfill('0') << std::setw(16) << crash.registers.x[i] << "  ";
        }
        ss << "\n\n--- Instruction Trace (" << crash.recent_instructions.size() << ") ---\n";
        for (const auto& it : crash.recent_instructions) {
            ss << "  [Seq " << std::dec << it.sequence << "] PC=0x" << std::hex << it.pc
               << " Opcode=0x" << std::hex << std::setfill('0') << std::setw(8) << it.opcode << "\n";
        }
        ss << "\n--- Syscall Trace (" << crash.recent_syscalls.size() << ") ---\n";
        for (const auto& st : crash.recent_syscalls) {
            ss << "  [Seq " << std::dec << st.sequence << "] Syscall " << st.sys_num
               << " (Arg0=0x" << std::hex << st.arg0 << ", Arg1=0x" << st.arg1
               << ") -> Result=" << std::dec << st.result << "\n";
        }
        ss << "=======================================\n";
        return ss.str();
    }

    [[nodiscard]] const std::shared_ptr<ApplicationDebugger>& debugger() const noexcept {
        return debugger_;
    }

    [[nodiscard]] kernel::ProcessHandle active_handle() const noexcept {
        return active_handle_;
    }

private:
    std::shared_ptr<loader::AppLaunchPipeline>      launcher_;
    std::shared_ptr<kernel::ProcessIdentityManager> identity_mgr_;
    std::shared_ptr<ApplicationDebugger>           debugger_;
    kernel::ProcessHandle                          active_handle_{};
    std::string                                    active_bundle_id_;
    std::unordered_set<uint64_t>                   breakpoints_;
};

} // namespace winios::debugger

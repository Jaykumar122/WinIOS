#include "runtime/execution_diagnostics.h"

#include <iomanip>
#include <sstream>
#include <algorithm>

namespace winios::runtime {

void ExecutionDiagnostics::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    steps_.clear();
    memory_ops_.clear();
    syscalls_.clear();
    memory_faults_.clear();
    mach_ipc_ops_.clear();
    thread_transitions_.clear();
    kernel_boot_events_.clear();
    image_regions_.clear();
    lifecycle_ = ProcessLifecycleRecord{};
}

void ExecutionDiagnostics::register_image_region(std::string_view name, std::uint64_t base_va, std::size_t size) {
    std::lock_guard<std::mutex> guard(lock_);
    image_regions_.push_back({std::string(name), base_va, size});
}

std::string ExecutionDiagnostics::resolve_image_name(std::uint64_t vaddr) const {
    std::lock_guard<std::mutex> guard(lock_);
    for (const auto& r : image_regions_) {
        if (vaddr >= r.base_va && vaddr < r.base_va + r.size) {
            return r.name;
        }
    }
    return "unknown";
}

void ExecutionDiagnostics::log_instruction_step(
    std::uint64_t pc_before,
    std::uint64_t pc_after,
    std::uint32_t instruction_raw,
    std::uint8_t current_el,
    const std::vector<RegisterDiff>& diffs,
    std::string_view disassembly,
    std::uint64_t sp_value,
    std::uint32_t nzcv,
    std::uint64_t timestamp_ns,
    std::uint32_t pid,
    std::uint64_t tid) {

    if (!enabled_) return;

    std::lock_guard<std::mutex> guard(lock_);
    if (steps_.size() >= max_capacity_) {
        steps_.erase(steps_.begin());
    }

    InstructionTraceStep step{};
    step.step_index = lifecycle_.instructions_retired;
    step.pid = pid;
    step.tid = tid;
    step.pc_before = pc_before;
    step.pc_after = pc_after;
    step.instruction_raw = instruction_raw;
    step.current_el = current_el;
    step.sp_value = sp_value;
    step.nzcv = nzcv;
    step.timestamp_ns = timestamp_ns;
    step.register_diffs = diffs;
    step.disassembly = disassembly.empty() ? disassemble_instruction(instruction_raw, pc_before) : std::string(disassembly);

    for (const auto& r : image_regions_) {
        if (pc_before >= r.base_va && pc_before < r.base_va + r.size) {
            step.image_name = r.name;
            break;
        }
    }

    steps_.push_back(std::move(step));
    lifecycle_.instructions_retired++;
}

void ExecutionDiagnostics::log_memory_op(
    MemoryOpType type,
    std::uint64_t address,
    std::size_t size,
    std::uint64_t value,
    std::uint32_t pid,
    std::uint64_t tid) {

    if (!enabled_) return;

    std::lock_guard<std::mutex> guard(lock_);
    if (memory_ops_.size() >= max_capacity_) {
        memory_ops_.erase(memory_ops_.begin());
    }

    MemoryOpRecord rec{};
    rec.type = type;
    rec.pid = pid;
    rec.tid = tid;
    rec.address = address;
    rec.size = size;
    rec.value = value;

    memory_ops_.push_back(rec);
    if (type == MemoryOpType::Read) {
        lifecycle_.memory_reads++;
    } else {
        lifecycle_.memory_writes++;
    }
}

void ExecutionDiagnostics::log_syscall(
    TrapKind kind,
    std::int32_t number,
    std::string_view name,
    const std::array<std::uint64_t, 6>& args,
    std::uint64_t return_value,
    bool is_error,
    std::int32_t errno_code,
    std::uint32_t pid,
    std::uint64_t tid) {

    if (!enabled_) return;

    std::lock_guard<std::mutex> guard(lock_);
    if (syscalls_.size() >= max_capacity_) {
        syscalls_.erase(syscalls_.begin());
    }

    SyscallTraceRecord rec{};
    rec.kind = kind;
    rec.pid = pid;
    rec.tid = tid;
    rec.number = number;
    rec.name = std::string(name);
    rec.arguments = args;
    rec.return_value = return_value;
    rec.is_error = is_error;
    rec.errno_code = errno_code;

    syscalls_.push_back(std::move(rec));
    if (kind == TrapKind::BsdSyscall) {
        lifecycle_.bsd_syscalls_count++;
    } else {
        lifecycle_.mach_traps_count++;
    }
}

void ExecutionDiagnostics::log_memory_fault(
    std::uint32_t pid,
    std::uint64_t tid,
    std::uint64_t fault_va,
    std::uint32_t fsc,
    bool is_write,
    bool is_exec,
    std::string_view desc) {

    if (!enabled_) return;

    std::lock_guard<std::mutex> guard(lock_);
    if (memory_faults_.size() >= max_capacity_) {
        memory_faults_.erase(memory_faults_.begin());
    }

    MemoryFaultRecord rec{};
    rec.pid = pid;
    rec.tid = tid;
    rec.fault_va = fault_va;
    rec.fsc = fsc;
    rec.is_write = is_write;
    rec.is_exec = is_exec;
    rec.description = std::string(desc);

    for (const auto& r : image_regions_) {
        if (fault_va >= r.base_va && fault_va < r.base_va + r.size) {
            rec.image_name = r.name;
            break;
        }
    }
    if (rec.image_name.empty()) {
        rec.image_name = "unknown";
    }

    memory_faults_.push_back(std::move(rec));
    lifecycle_.memory_faults_count++;
}

void ExecutionDiagnostics::log_mach_ipc_op(
    std::uint32_t pid,
    std::uint64_t tid,
    std::uint32_t port_name,
    std::uint32_t msgh_id,
    std::size_t msg_size,
    std::string_view op_name,
    bool is_send,
    std::string_view status) {

    if (!enabled_) return;

    std::lock_guard<std::mutex> guard(lock_);
    if (mach_ipc_ops_.size() >= max_capacity_) {
        mach_ipc_ops_.erase(mach_ipc_ops_.begin());
    }

    MachIpcTraceRecord rec{};
    rec.pid = pid;
    rec.tid = tid;
    rec.port_name = port_name;
    rec.msgh_id = msgh_id;
    rec.msg_size = msg_size;
    rec.op_name = std::string(op_name);
    rec.is_send = is_send;
    rec.status = std::string(status);

    mach_ipc_ops_.push_back(std::move(rec));
    lifecycle_.mach_ipc_ops_count++;
}

void ExecutionDiagnostics::log_thread_transition(
    std::uint32_t pid,
    std::uint64_t tid,
    std::string_view from_state,
    std::string_view to_state,
    std::string_view reason) {

    if (!enabled_) return;

    std::lock_guard<std::mutex> guard(lock_);
    if (thread_transitions_.size() >= max_capacity_) {
        thread_transitions_.erase(thread_transitions_.begin());
    }

    ThreadTransitionRecord rec{};
    rec.pid = pid;
    rec.tid = tid;
    rec.from_state = std::string(from_state);
    rec.to_state = std::string(to_state);
    rec.reason = std::string(reason);

    thread_transitions_.push_back(std::move(rec));
    lifecycle_.thread_transitions_count++;
}

void ExecutionDiagnostics::record_process_start(std::uint32_t pid, std::string_view path) {
    std::lock_guard<std::mutex> guard(lock_);
    lifecycle_.pid = pid;
    lifecycle_.executable_path = std::string(path);
    lifecycle_.exited = false;
    lifecycle_.exit_code = 0;
}

void ExecutionDiagnostics::record_process_exit(int exit_code) {
    std::lock_guard<std::mutex> guard(lock_);
    lifecycle_.exited = true;
    lifecycle_.exit_code = exit_code;
}

std::size_t ExecutionDiagnostics::step_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return steps_.size();
}

std::size_t ExecutionDiagnostics::memory_op_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return memory_ops_.size();
}

std::size_t ExecutionDiagnostics::syscall_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return syscalls_.size();
}

std::size_t ExecutionDiagnostics::memory_fault_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return memory_faults_.size();
}

std::size_t ExecutionDiagnostics::mach_ipc_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return mach_ipc_ops_.size();
}

std::size_t ExecutionDiagnostics::thread_transition_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return thread_transitions_.size();
}

const InstructionTraceStep* ExecutionDiagnostics::get_step(std::size_t index) const {
    std::lock_guard<std::mutex> guard(lock_);
    if (index >= steps_.size()) return nullptr;
    return &steps_[index];
}

const MemoryOpRecord* ExecutionDiagnostics::get_memory_op(std::size_t index) const {
    std::lock_guard<std::mutex> guard(lock_);
    if (index >= memory_ops_.size()) return nullptr;
    return &memory_ops_[index];
}

const SyscallTraceRecord* ExecutionDiagnostics::get_syscall(std::size_t index) const {
    std::lock_guard<std::mutex> guard(lock_);
    if (index >= syscalls_.size()) return nullptr;
    return &syscalls_[index];
}

const MemoryFaultRecord* ExecutionDiagnostics::get_memory_fault(std::size_t index) const {
    std::lock_guard<std::mutex> guard(lock_);
    if (index >= memory_faults_.size()) return nullptr;
    return &memory_faults_[index];
}

const MachIpcTraceRecord* ExecutionDiagnostics::get_mach_ipc_op(std::size_t index) const {
    std::lock_guard<std::mutex> guard(lock_);
    if (index >= mach_ipc_ops_.size()) return nullptr;
    return &mach_ipc_ops_[index];
}

const ThreadTransitionRecord* ExecutionDiagnostics::get_thread_transition(std::size_t index) const {
    std::lock_guard<std::mutex> guard(lock_);
    if (index >= thread_transitions_.size()) return nullptr;
    return &thread_transitions_[index];
}

std::string ExecutionDiagnostics::format_summary_unlocked() const {
    std::ostringstream oss;
    oss << "=== WinIOS Execution Diagnostics Summary ===\n"
        << "PID: " << lifecycle_.pid << "\n"
        << "Executable: " << lifecycle_.executable_path << "\n"
        << "Status: " << (lifecycle_.exited ? "Exited" : "Running") << "\n"
        << "Exit Code: " << lifecycle_.exit_code << "\n"
        << "Instructions Retired: " << lifecycle_.instructions_retired << "\n"
        << "Memory Ops: " << (lifecycle_.memory_reads + lifecycle_.memory_writes)
        << " (" << lifecycle_.memory_reads << " reads, " << lifecycle_.memory_writes << " writes)\n"
        << "Syscalls Handled: " << (lifecycle_.bsd_syscalls_count + lifecycle_.mach_traps_count)
        << " (" << lifecycle_.bsd_syscalls_count << " BSD, " << lifecycle_.mach_traps_count << " Mach)\n"
        << "Memory Faults: " << lifecycle_.memory_faults_count << "\n"
        << "Mach IPC Ops: " << lifecycle_.mach_ipc_ops_count << "\n"
        << "Thread Transitions: " << lifecycle_.thread_transitions_count << "\n";
    return oss.str();
}

std::string ExecutionDiagnostics::format_summary() const {
    std::lock_guard<std::mutex> guard(lock_);
    return format_summary_unlocked();
}

std::string ExecutionDiagnostics::format_trace_report(std::size_t max_steps) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::ostringstream oss;
    oss << format_summary_unlocked() << "\n--- Instruction Trace (First "
        << std::min(max_steps, steps_.size()) << " steps) ---\n";

    const std::size_t limit = std::min(max_steps, steps_.size());
    for (std::size_t i = 0; i < limit; ++i) {
        const auto& s = steps_[i];
        oss << "[" << std::setw(5) << s.step_index << "] "
            << "0x" << std::hex << std::setw(16) << std::setfill('0') << s.pc_before
            << " | Raw: 0x" << std::setw(8) << std::setfill('0') << s.instruction_raw
            << " | EL" << static_cast<int>(s.current_el);

        if (!s.disassembly.empty()) {
            oss << " | " << s.disassembly;
        }

        if (!s.register_diffs.empty()) {
            oss << " | Diffs: ";
            for (const auto& d : s.register_diffs) {
                oss << "X" << static_cast<int>(d.reg_index) << "=0x" << std::hex << d.new_value << " ";
            }
        }
        oss << "\n";
    }

    return oss.str();
}

std::string ExecutionDiagnostics::format_crash_diagnostic_report(
    std::uint32_t pid,
    std::uint64_t tid,
    std::uint64_t fault_address,
    std::string_view failure_reason) const {

    std::lock_guard<std::mutex> guard(lock_);
    std::ostringstream oss;
    oss << "========================================================\n"
        << "  WINIOS CRASH DIAGNOSTIC REPORT (DARWIN ARM64)\n"
        << "========================================================\n"
        << "Process ID:    " << pid << "\n"
        << "Thread ID:     " << tid << "\n"
        << "Fault Address: 0x" << std::hex << fault_address << std::dec << "\n"
        << "Failure Reason: " << failure_reason << "\n";

    std::string matched_img = "unknown";
    for (const auto& r : image_regions_) {
        if (fault_address >= r.base_va && fault_address < r.base_va + r.size) {
            matched_img = r.name + " (+0x" + std::to_string(fault_address - r.base_va) + ")";
            break;
        }
    }
    oss << "Fault Image:   " << matched_img << "\n\n";

    oss << "--- Recent Instruction Trace (Last " << std::min<std::size_t>(steps_.size(), 10) << ") ---\n";
    std::size_t start_idx = (steps_.size() > 10) ? (steps_.size() - 10) : 0;
    for (std::size_t i = start_idx; i < steps_.size(); ++i) {
        const auto& s = steps_[i];
        oss << "[" << s.step_index << "] PC=0x" << std::hex << s.pc_before << " (0x" << s.instruction_raw << ") "
            << s.disassembly << std::dec << "\n";
    }

    oss << "\n--- Last Syscall ---\n";
    if (!syscalls_.empty()) {
        const auto& sc = syscalls_.back();
        oss << (sc.kind == TrapKind::BsdSyscall ? "BSD " : "Mach ")
            << sc.name << " (" << sc.number << ") ret=0x" << std::hex << sc.return_value
            << (sc.is_error ? " [ERROR]" : " [OK]") << std::dec << "\n";
    } else {
        oss << "(None)\n";
    }

    oss << "\n--- Memory Faults Logged (" << memory_faults_.size() << ") ---\n";
    for (const auto& f : memory_faults_) {
        oss << "Fault at 0x" << std::hex << f.fault_va << " (" << (f.is_write ? "W" : "R")
            << (f.is_exec ? "X" : "") << ") in " << f.image_name << ": " << f.description << std::dec << "\n";
    }

    oss << "\n--- Thread Transitions (" << thread_transitions_.size() << ") ---\n";
    for (const auto& tt : thread_transitions_) {
        oss << "TID " << tt.tid << ": " << tt.from_state << " -> " << tt.to_state << " (" << tt.reason << ")\n";
    }

    oss << "========================================================\n";
    return oss.str();
}

std::vector<InstructionTraceStep> ExecutionDiagnostics::find_steps_by_pc(std::uint64_t pc) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<InstructionTraceStep> result;
    for (const auto& s : steps_) {
        if (s.pc_before == pc) result.push_back(s);
    }
    return result;
}

std::vector<InstructionTraceStep> ExecutionDiagnostics::find_steps_by_instruction(std::uint32_t mask, std::uint32_t val) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<InstructionTraceStep> result;
    for (const auto& s : steps_) {
        if ((s.instruction_raw & mask) == val) result.push_back(s);
    }
    return result;
}

std::vector<InstructionTraceStep> ExecutionDiagnostics::find_steps_by_reg_write(std::uint8_t reg_index) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<InstructionTraceStep> result;
    for (const auto& s : steps_) {
        for (const auto& d : s.register_diffs) {
            if (d.reg_index == reg_index) {
                result.push_back(s);
                break;
            }
        }
    }
    return result;
}

std::vector<SyscallTraceRecord> ExecutionDiagnostics::find_syscalls_by_number(std::int32_t number) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<SyscallTraceRecord> result;
    for (const auto& sc : syscalls_) {
        if (sc.number == number) result.push_back(sc);
    }
    return result;
}

std::string ExecutionDiagnostics::export_trace_json() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::ostringstream oss;
    oss << "{\n"
        << "  \"pid\": " << lifecycle_.pid << ",\n"
        << "  \"executable\": \"" << lifecycle_.executable_path << "\",\n"
        << "  \"lifecycle\": {\n"
        << "    \"pid\": " << lifecycle_.pid << ",\n"
        << "    \"executable_path\": \"" << lifecycle_.executable_path << "\",\n"
        << "    \"exited\": " << (lifecycle_.exited ? "true" : "false") << ",\n"
        << "    \"exit_code\": " << lifecycle_.exit_code << ",\n"
        << "    \"instructions_retired\": " << lifecycle_.instructions_retired << ",\n"
        << "    \"memory_reads\": " << lifecycle_.memory_reads << ",\n"
        << "    \"memory_writes\": " << lifecycle_.memory_writes << ",\n"
        << "    \"bsd_syscalls_count\": " << lifecycle_.bsd_syscalls_count << ",\n"
        << "    \"mach_traps_count\": " << lifecycle_.mach_traps_count << "\n"
        << "  },\n";

    oss << "  \"instructions_retired\": " << lifecycle_.instructions_retired << ",\n"
        << "  \"steps\": [\n";

    for (std::size_t i = 0; i < steps_.size(); ++i) {
        const auto& s = steps_[i];
        oss << "    {\n"
            << "      \"step\": " << s.step_index << ",\n"
            << "      \"pc_before\": \"0x" << std::hex << s.pc_before << "\",\n"
            << "      \"pc_after\": \"0x" << s.pc_after << "\",\n"
            << "      \"instruction_raw\": \"0x" << s.instruction_raw << "\",\n"
            << "      \"raw\": \"0x" << s.instruction_raw << "\",\n"
            << "      \"disassembly\": \"" << s.disassembly << "\",\n"
            << "      \"disasm\": \"" << s.disassembly << "\"\n"
            << "    }" << (i + 1 < steps_.size() ? "," : "") << "\n";
    }
    oss << "  ],\n"
        << "  \"instructions\": [\n";

    for (std::size_t i = 0; i < steps_.size(); ++i) {
        const auto& s = steps_[i];
        oss << "    {\n"
            << "      \"step\": " << s.step_index << ",\n"
            << "      \"pc_before\": \"0x" << std::hex << s.pc_before << "\",\n"
            << "      \"pc_after\": \"0x" << s.pc_after << "\",\n"
            << "      \"instruction_raw\": \"0x" << s.instruction_raw << "\",\n"
            << "      \"disassembly\": \"" << s.disassembly << "\",\n"
            << "      \"current_el\": " << std::dec << static_cast<int>(s.current_el) << ",\n"
            << "      \"sp\": \"0x" << std::hex << s.sp_value << "\",\n"
            << "      \"nzcv\": " << std::dec << s.nzcv << ",\n"
            << "      \"diffs\": [";
        for (std::size_t d = 0; d < s.register_diffs.size(); ++d) {
            const auto& diff = s.register_diffs[d];
            oss << "{\"reg\":" << static_cast<int>(diff.reg_index)
                << ",\"old\":\"0x" << std::hex << diff.old_value
                << "\",\"new\":\"0x" << diff.new_value << "\"}";
            if (d + 1 < s.register_diffs.size()) oss << ",";
        }
        oss << "]\n"
            << "    }" << (i + 1 < steps_.size() ? "," : "") << "\n";
    }
    oss << "  ],\n";

    oss << "  \"syscalls\": [\n";
    for (std::size_t i = 0; i < syscalls_.size(); ++i) {
        const auto& sc = syscalls_[i];
        oss << "    {\n"
            << "      \"kind\": \"" << (sc.kind == TrapKind::BsdSyscall ? "BsdSyscall" : "MachTrap") << "\",\n"
            << "      \"number\": " << std::dec << sc.number << ",\n"
            << "      \"name\": \"" << sc.name << "\",\n"
            << "      \"return_value\": \"0x" << std::hex << sc.return_value << "\",\n"
            << "      \"is_error\": " << (sc.is_error ? "true" : "false") << ",\n"
            << "      \"errno\": " << std::dec << sc.errno_code << "\n"
            << "    }" << (i + 1 < syscalls_.size() ? "," : "") << "\n";
    }
    oss << "  ]\n"
        << "}\n";

    return oss.str();
}

std::string ExecutionDiagnostics::export_trace_csv() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::ostringstream oss;
    oss << "step,pc_before,pc_after,instruction_raw,current_el,sp,nzcv,disassembly,register_diffs\n";
    for (const auto& s : steps_) {
        oss << s.step_index << ","
            << "0x" << std::hex << s.pc_before << ","
            << "0x" << s.pc_after << ","
            << "0x" << s.instruction_raw << ","
            << std::dec << static_cast<int>(s.current_el) << ","
            << "0x" << std::hex << s.sp_value << ","
            << std::dec << s.nzcv << ","
            << "\"" << s.disassembly << "\",\"";
        for (std::size_t d = 0; d < s.register_diffs.size(); ++d) {
            const auto& diff = s.register_diffs[d];
            oss << "X" << static_cast<int>(diff.reg_index) << "=0x" << std::hex << diff.new_value;
            if (d + 1 < s.register_diffs.size()) oss << " ";
        }
        oss << "\"\n";
    }
    return oss.str();
}

std::string ExecutionDiagnostics::export_syscalls_csv() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::ostringstream oss;
    oss << "kind,number,name,arg0,arg1,arg2,arg3,arg4,arg5,return_value,is_error,errno_code\n";
    for (const auto& sc : syscalls_) {
        oss << (sc.kind == TrapKind::BsdSyscall ? "BSD" : "MACH") << ","
            << sc.number << ","
            << "\"" << sc.name << "\",";
        for (int a = 0; a < 6; ++a) {
            oss << "0x" << std::hex << sc.arguments[a] << ",";
        }
        oss << "0x" << std::hex << sc.return_value << ","
            << (sc.is_error ? 1 : 0) << ","
            << std::dec << sc.errno_code << "\n";
    }
    return oss.str();
}

std::string ExecutionDiagnostics::disassemble_instruction(std::uint32_t raw_instr, std::uint64_t pc) {
    if (raw_instr == 0xD503201Fu) {
        return "nop";
    }
    if (raw_instr == 0xD65F03C0u) {
        return "ret";
    }

    // SVC: 1101 0100 000 imm16 000 01
    if ((raw_instr & 0xFFE0001Fu) == 0xD4000001u) {
        std::uint16_t imm = static_cast<std::uint16_t>((raw_instr >> 5u) & 0xFFFFu);
        std::ostringstream ss;
        ss << "svc #0x" << std::hex << imm;
        return ss.str();
    }

    // MOVZ 64-bit: 1101 0010 100 hw imm16 Rd
    if ((raw_instr & 0xFF800000u) == 0xD2800000u) {
        std::uint32_t rd = raw_instr & 0x1Fu;
        std::uint32_t imm16 = (raw_instr >> 5u) & 0xFFFFu;
        std::uint32_t hw = (raw_instr >> 21u) & 0x3u;
        std::ostringstream ss;
        ss << "movz x" << rd << ", #0x" << std::hex << imm16;
        if (hw > 0) ss << ", lsl #" << std::dec << (hw * 16);
        return ss.str();
    }

    // MOVN 64-bit: 1001 0010 100 hw imm16 Rd
    if ((raw_instr & 0xFF800000u) == 0x92800000u) {
        std::uint32_t rd = raw_instr & 0x1Fu;
        std::uint32_t imm16 = (raw_instr >> 5u) & 0xFFFFu;
        std::ostringstream ss;
        ss << "movn x" << rd << ", #0x" << std::hex << imm16;
        return ss.str();
    }

    // MOVK 64-bit: 1111 0010 100 hw imm16 Rd
    if ((raw_instr & 0xFF800000u) == 0xF2800000u) {
        std::uint32_t rd = raw_instr & 0x1Fu;
        std::uint32_t imm16 = (raw_instr >> 5u) & 0xFFFFu;
        std::uint32_t hw = (raw_instr >> 21u) & 0x3u;
        std::ostringstream ss;
        ss << "movk x" << rd << ", #0x" << std::hex << imm16 << ", lsl #" << std::dec << (hw * 16);
        return ss.str();
    }

    // MOV (register / ORR): 1010 1010 000 Rm 0000 0011 111 Rd
    if ((raw_instr & 0xFFE0FFE0u) == 0xAA0003E0u) {
        std::uint32_t rd = raw_instr & 0x1Fu;
        std::uint32_t rm = (raw_instr >> 16u) & 0x1Fu;
        std::ostringstream ss;
        ss << "mov x" << rd << ", x" << rm;
        return ss.str();
    }

    // ADD shifted register: 1000 1011 000 Rm imm6 Rn Rd
    if ((raw_instr & 0xFF200000u) == 0x8B000000u) {
        std::uint32_t rd = raw_instr & 0x1Fu;
        std::uint32_t rn = (raw_instr >> 5u) & 0x1Fu;
        std::uint32_t rm = (raw_instr >> 16u) & 0x1Fu;
        std::ostringstream ss;
        ss << "add x" << rd << ", x" << rn << ", x" << rm;
        return ss.str();
    }

    // SUB shifted register: 1100 1011 000 Rm imm6 Rn Rd
    if ((raw_instr & 0xFF200000u) == 0xCB000000u) {
        std::uint32_t rd = raw_instr & 0x1Fu;
        std::uint32_t rn = (raw_instr >> 5u) & 0x1Fu;
        std::uint32_t rm = (raw_instr >> 16u) & 0x1Fu;
        std::ostringstream ss;
        ss << "sub x" << rd << ", x" << rn << ", x" << rm;
        return ss.str();
    }

    // CMP / SUBS immediate 64-bit: 1111 0001 00 sh imm12 Rn Rd
    if ((raw_instr & 0xFF800000u) == 0xF1000000u) {
        std::uint32_t rd = raw_instr & 0x1Fu;
        std::uint32_t rn = (raw_instr >> 5u) & 0x1Fu;
        std::uint32_t imm12 = (raw_instr >> 10u) & 0xFFFu;
        std::ostringstream ss;
        if (rd == 31) {
            ss << "cmp x" << rn << ", #0x" << std::hex << imm12;
        } else {
            ss << "subs x" << rd << ", x" << rn << ", #0x" << std::hex << imm12;
        }
        return ss.str();
    }

    // LDR 64-bit literal: 0101 1000 imm19 Rt
    if ((raw_instr & 0xFF000000u) == 0x58000000u) {
        std::uint32_t rt = raw_instr & 0x1Fu;
        std::int32_t imm19 = static_cast<std::int32_t>((raw_instr >> 5u) & 0x7FFFFu);
        if (imm19 & 0x40000) imm19 |= ~0x7FFFF;
        std::int64_t offset = static_cast<std::int64_t>(imm19) * 4;
        std::ostringstream ss;
        ss << "ldr x" << rt << ", [pc, #" << std::dec << offset << "]";
        return ss.str();
    }

    // LDR 32-bit unsigned offset: 1011 1001 01 imm12 Rn Rt
    if ((raw_instr & 0xFFC00000u) == 0xB9400000u) {
        std::uint32_t rt = raw_instr & 0x1Fu;
        std::uint32_t rn = (raw_instr >> 5u) & 0x1Fu;
        std::uint32_t imm12 = (raw_instr >> 10u) & 0xFFFu;
        std::ostringstream ss;
        ss << "ldr w" << rt << ", [x" << rn << ", #" << std::dec << (imm12 * 4) << "]";
        return ss.str();
    }

    // STR 32-bit unsigned offset: 1011 1001 00 imm12 Rn Rt
    if ((raw_instr & 0xFFC00000u) == 0xB9000000u) {
        std::uint32_t rt = raw_instr & 0x1Fu;
        std::uint32_t rn = (raw_instr >> 5u) & 0x1Fu;
        std::uint32_t imm12 = (raw_instr >> 10u) & 0xFFFu;
        std::ostringstream ss;
        ss << "str w" << rt << ", [x" << rn << ", #" << std::dec << (imm12 * 4) << "]";
        return ss.str();
    }

    // LDR 64-bit unsigned offset: 1111 1001 01 imm12 Rn Rt
    if ((raw_instr & 0xFFC00000u) == 0xF9400000u) {
        std::uint32_t rt = raw_instr & 0x1Fu;
        std::uint32_t rn = (raw_instr >> 5u) & 0x1Fu;
        std::uint32_t imm12 = (raw_instr >> 10u) & 0xFFFu;
        std::ostringstream ss;
        ss << "ldr x" << rt << ", [x" << rn << ", #" << std::dec << (imm12 * 8) << "]";
        return ss.str();
    }

    // STR 64-bit unsigned offset: 1111 1001 00 imm12 Rn Rt
    if ((raw_instr & 0xFFC00000u) == 0xF9000000u) {
        std::uint32_t rt = raw_instr & 0x1Fu;
        std::uint32_t rn = (raw_instr >> 5u) & 0x1Fu;
        std::uint32_t imm12 = (raw_instr >> 10u) & 0xFFFu;
        std::ostringstream ss;
        ss << "str x" << rt << ", [x" << rn << ", #" << std::dec << (imm12 * 8) << "]";
        return ss.str();
    }

    // BLR: 1101 0110 0011 1111 0000 00 Rn 00000
    if ((raw_instr & 0xFFFFFC1Fu) == 0xD63F0000u) {
        std::uint32_t rn = (raw_instr >> 5u) & 0x1Fu;
        std::ostringstream ss;
        ss << "blr x" << rn;
        return ss.str();
    }

    // BR: 1101 0110 0001 1111 0000 00 Rn 00000
    if ((raw_instr & 0xFFFFFC1Fu) == 0xD61F0000u) {
        std::uint32_t rn = (raw_instr >> 5u) & 0x1Fu;
        std::ostringstream ss;
        ss << "br x" << rn;
        return ss.str();
    }

    // B unconditional: 0001 01 imm26
    if ((raw_instr & 0xFC000000u) == 0x14000000u) {
        std::int32_t imm26 = static_cast<std::int32_t>(raw_instr & 0x03FFFFFFu);
        if (imm26 & 0x02000000) imm26 |= ~0x03FFFFFF;
        std::int64_t target = static_cast<std::int64_t>(pc) + (static_cast<std::int64_t>(imm26) * 4);
        std::ostringstream ss;
        ss << "b 0x" << std::hex << target;
        return ss.str();
    }

    // BL: 1001 01 imm26
    if ((raw_instr & 0xFC000000u) == 0x94000000u) {
        std::int32_t imm26 = static_cast<std::int32_t>(raw_instr & 0x03FFFFFFu);
        if (imm26 & 0x02000000) imm26 |= ~0x03FFFFFF;
        std::int64_t target = static_cast<std::int64_t>(pc) + (static_cast<std::int64_t>(imm26) * 4);
        std::ostringstream ss;
        ss << "bl 0x" << std::hex << target;
        return ss.str();
    }

    // B.cond: 0101 0100 imm19 0 cond
    if ((raw_instr & 0xFF000010u) == 0x54000000u) {
        std::uint32_t cond = raw_instr & 0xFu;
        std::int32_t imm19 = static_cast<std::int32_t>((raw_instr >> 5u) & 0x7FFFFu);
        if (imm19 & 0x40000) imm19 |= ~0x7FFFF;
        std::int64_t target = static_cast<std::int64_t>(pc) + (static_cast<std::int64_t>(imm19) * 4);
        const char* cond_names[] = {
            "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
            "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"
        };
        std::ostringstream ss;
        ss << "b." << cond_names[cond] << " 0x" << std::hex << target;
        return ss.str();
    }

    // CBZ 64-bit: 1011 0100 imm19 Rt
    if ((raw_instr & 0xFF000000u) == 0xB4000000u) {
        std::uint32_t rt = raw_instr & 0x1Fu;
        std::int32_t imm19 = static_cast<std::int32_t>((raw_instr >> 5u) & 0x7FFFFu);
        if (imm19 & 0x40000) imm19 |= ~0x7FFFF;
        std::int64_t target = static_cast<std::int64_t>(pc) + (static_cast<std::int64_t>(imm19) * 4);
        std::ostringstream ss;
        ss << "cbz x" << rt << ", 0x" << std::hex << target;
        return ss.str();
    }

    // Fallback:
    std::ostringstream ss;
    ss << "instr 0x" << std::hex << std::setw(8) << std::setfill('0') << raw_instr;
    return ss.str();
}

void ExecutionDiagnostics::log_kernel_boot_event(const KernelBootTimelineEvent& event) {
    if (!enabled_) return;
    std::lock_guard<std::mutex> guard(lock_);
    if (kernel_boot_events_.size() >= max_capacity_) {
        kernel_boot_events_.erase(kernel_boot_events_.begin());
    }
    kernel_boot_events_.push_back(event);
}

void ExecutionDiagnostics::log_kernel_boot_phase(
    std::string_view phase,
    std::uint32_t core_id,
    std::uint64_t pc,
    std::uint64_t sp,
    std::uint64_t instructions_retired,
    std::string_view details) {

    KernelBootTimelineEvent ev{};
    ev.phase_name = std::string(phase);
    ev.timestamp_ns = 0;
    ev.core_id = core_id;
    ev.pc = pc;
    ev.sp = sp;
    ev.instructions_retired = instructions_retired;
    ev.details = std::string(details);

    log_kernel_boot_event(ev);
}

std::size_t ExecutionDiagnostics::kernel_boot_event_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return kernel_boot_events_.size();
}

const KernelBootTimelineEvent* ExecutionDiagnostics::get_kernel_boot_event(std::size_t index) const {
    std::lock_guard<std::mutex> guard(lock_);
    if (index >= kernel_boot_events_.size()) return nullptr;
    return &kernel_boot_events_[index];
}

std::vector<KernelBootTimelineEvent> ExecutionDiagnostics::get_kernel_boot_timeline() const {
    std::lock_guard<std::mutex> guard(lock_);
    return kernel_boot_events_;
}

std::string ExecutionDiagnostics::export_kernel_boot_timeline_json() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::ostringstream oss;
    oss << "{\n"
        << "  \"schema_version\": \"205.21\",\n"
        << "  \"target_device\": \"iPhone15,4\",\n"
        << "  \"soc\": \"Apple A16 Bionic (T8120)\",\n"
        << "  \"total_events\": " << kernel_boot_events_.size() << ",\n"
        << "  \"events\": [\n";

    for (std::size_t i = 0; i < kernel_boot_events_.size(); ++i) {
        const auto& ev = kernel_boot_events_[i];
        oss << "    {\n"
            << "      \"phase\": \"" << ev.phase_name << "\",\n"
            << "      \"core_id\": " << ev.core_id << ",\n"
            << "      \"pc\": \"0x" << std::hex << ev.pc << std::dec << "\",\n"
            << "      \"sp\": \"0x" << std::hex << ev.sp << std::dec << "\",\n"
            << "      \"instructions_retired\": " << ev.instructions_retired << ",\n"
            << "      \"details\": \"" << ev.details << "\",\n"
            << "      \"timestamp_ns\": " << ev.timestamp_ns;

        if (!ev.register_diffs.empty()) {
            oss << ",\n      \"register_diffs\": [\n";
            for (std::size_t r = 0; r < ev.register_diffs.size(); ++r) {
                const auto& diff = ev.register_diffs[r];
                oss << "        {\"reg\": " << static_cast<unsigned>(diff.reg_index)
                    << ", \"before\": \"0x" << std::hex << diff.old_value << std::dec
                    << "\", \"after\": \"0x" << std::hex << diff.new_value << std::dec << "\"}";
                if (r + 1 < ev.register_diffs.size()) oss << ",";
                oss << "\n";
            }
            oss << "      ]\n";
        } else {
            oss << "\n";
        }
        oss << "    }";
        if (i + 1 < kernel_boot_events_.size()) oss << ",";
        oss << "\n";
    }

    oss << "  ]\n}\n";
    return oss.str();
}

} // namespace winios::runtime

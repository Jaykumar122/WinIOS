#include "kernel/guest_trace_logger.h"

#include <iomanip>
#include <sstream>

namespace winios::kernel {

GuestTraceLogger& GuestTraceLogger::instance() {
    static GuestTraceLogger s_instance;
    return s_instance;
}

void GuestTraceLogger::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    instructions_.clear();
    syscalls_.clear();
    inputs_.clear();
    framebuffers_.clear();
    boundaries_.clear();
    resources_.clear();
    fb_sequence_ = 0;
    boundary_sequence_ = 0;
    resource_sequence_ = 0;
}

void GuestTraceLogger::log_instruction(std::uint32_t pid, const std::string& name, std::uint64_t pc,
                                       std::uint32_t opcode, std::uint64_t retired_count,
                                       std::uint32_t tid, std::uint32_t el) {
    std::lock_guard<std::mutex> lock(mutex_);
    InstructionTraceRecord r;
    r.pid = pid;
    r.tid = tid;
    r.process_name = name;
    r.pc = pc;
    r.opcode = opcode;
    r.instruction_index = retired_count;
    r.exception_level = el;
    r.host_time_ns = current_host_time_ns();
    instructions_.push_back(std::move(r));
}

void GuestTraceLogger::log_syscall(std::uint32_t pid, const std::string& name, std::uint64_t num, const std::string& sname,
                                   const std::uint64_t args[6], std::uint32_t svc_imm, std::int64_t ret, std::int32_t err,
                                   const std::string& details, const std::string& abi, std::uint32_t tid) {
    std::lock_guard<std::mutex> lock(mutex_);
    SyscallTraceRecord r;
    r.pid = pid;
    r.tid = tid;
    r.process_name = name;
    r.syscall_num = num;
    r.syscall_name = sname;
    r.abi_identifier = abi;
    for (int i = 0; i < 6; ++i) {
        r.args[i] = args ? args[i] : 0;
    }
    r.svc_immediate = svc_imm;
    r.return_value = ret;
    r.error_code = err;
    r.exception_level = 1; // Intercepted at EL1 supervisor
    r.details = details;
    r.host_time_ns = current_host_time_ns();
    syscalls_.push_back(std::move(r));
}

void GuestTraceLogger::log_input(std::uint32_t pid, std::uint32_t evt_type, std::int32_t rx, std::int32_t ry,
                                 std::int32_t wx, std::int32_t wy, std::uint32_t wid, std::uint64_t timestamp,
                                 const std::string& origin, std::uint32_t tid) {
    std::lock_guard<std::mutex> lock(mutex_);
    InputTraceRecord r;
    r.pid = pid;
    r.tid = tid;
    r.event_type = evt_type;
    r.input_origin = origin;
    r.raw_x = rx;
    r.raw_y = ry;
    r.window_x = wx;
    r.window_y = wy;
    r.window_id = wid;
    r.timestamp_us = timestamp;
    r.host_time_ns = current_host_time_ns();
    inputs_.push_back(std::move(r));
}

void GuestTraceLogger::log_framebuffer(std::uint32_t surf_id, std::uint32_t wid, std::uint32_t pid,
                                      const std::string& op, std::uint32_t dx, std::uint32_t dy, std::uint32_t dw, std::uint32_t dh,
                                      const std::string& shot_file, const std::string& shot_hash, std::uint64_t pres_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++fb_sequence_;
    FramebufferTraceRecord r;
    r.sequence = fb_sequence_;
    r.surface_id = surf_id;
    r.window_id = wid;
    r.pid = pid;
    r.operation = op;
    r.damage_x = dx;
    r.damage_y = dy;
    r.damage_w = dw;
    r.damage_h = dh;
    r.screenshot_file = shot_file;
    r.screenshot_sha256 = shot_hash;
    r.presentation_id = pres_id;
    r.host_time_ns = current_host_time_ns();
    framebuffers_.push_back(std::move(r));
}

void GuestTraceLogger::log_boundary_transition(std::uint32_t pid, std::uint32_t tid, ExecutionDomain from_domain,
                                              ExecutionDomain to_domain, const std::string& reason, std::uint64_t pc, std::uint32_t el) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++boundary_sequence_;
    BoundaryTransitionRecord r;
    r.sequence = boundary_sequence_;
    r.pid = pid;
    r.tid = tid;
    r.from_domain = from_domain;
    r.to_domain = to_domain;
    r.reason = reason;
    r.pc = pc;
    r.exception_level = el;
    r.host_time_ns = current_host_time_ns();
    boundaries_.push_back(std::move(r));
}

void GuestTraceLogger::log_resource_ownership(std::uint32_t owner_pid, const std::string& res_type, std::uint32_t res_id, const std::string& action) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++resource_sequence_;
    ResourceOwnershipRecord r;
    r.sequence = resource_sequence_;
    r.owner_pid = owner_pid;
    r.resource_type = res_type;
    r.resource_id = res_id;
    r.action = action;
    r.host_time_ns = current_host_time_ns();
    resources_.push_back(std::move(r));
}

std::string GuestTraceLogger::export_markdown_correlation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "# WinIOS Execution Provenance and Trace Correlation Report\n\n";
    ss << "## 1. Summary Metrics\n";
    ss << "- Total Recorded Guest Instruction Samples: " << instructions_.size() << "\n";
    ss << "- Total Recorded Syscalls: " << syscalls_.size() << "\n";
    ss << "- Total Boundary Transitions: " << boundaries_.size() << "\n";
    ss << "- Total Input Events Delivered: " << inputs_.size() << "\n";
    ss << "- Total Framebuffer/Presentation Operations: " << framebuffers_.size() << "\n";
    ss << "- Total Resource Ownership Events: " << resources_.size() << "\n\n";

    ss << "## 2. Execution Domain Boundary Transitions\n\n";
    ss << "| Seq | PID | TID | Transition | Reason | PC | EL | Host Timestamp (ns) |\n";
    ss << "|---|---|---|---|---|---|---|---|\n";
    for (const auto& b : boundaries_) {
        ss << "| " << b.sequence << " | " << b.pid << " | " << b.tid
           << " | " << execution_domain_to_string(b.from_domain) << " -> " << execution_domain_to_string(b.to_domain)
           << " | " << b.reason << " | 0x" << std::hex << b.pc << std::dec
           << " | EL" << b.exception_level << " | " << b.host_time_ns << " |\n";
    }

    ss << "\n## 3. Syscall Trace Sequence with ABI Provenance\n\n";
    ss << "| PID:TID | Process | ABI | Syscall | Immediate | Ret/Err | Arguments (X0..X5) | Details |\n";
    ss << "|---|---|---|---|---|---|---|---|\n";
    for (const auto& sc : syscalls_) {
        ss << "| " << sc.pid << ":" << sc.tid << " | " << sc.process_name << " | " << sc.abi_identifier
           << " | " << sc.syscall_name << " (" << sc.syscall_num << ")"
           << " | 0x" << std::hex << sc.svc_immediate << std::dec
           << " | " << sc.return_value << "/" << sc.error_code
           << " | 0x" << std::hex << sc.args[0] << ", 0x" << sc.args[1] << ", 0x" << sc.args[2]
           << ", 0x" << sc.args[3] << ", 0x" << sc.args[4] << ", 0x" << sc.args[5] << std::dec
           << " | " << sc.details << " |\n";
    }

    ss << "\n## 4. Input Event Origin and Coordinate Normalization\n\n";
    ss << "| Host Timestamp (ns) | Origin | PID:TID | Event | Screen (X,Y) | Window (X,Y) | Window ID |\n";
    ss << "|---|---|---|---|---|---|---|\n";
    for (const auto& in : inputs_) {
        std::string typ = (in.event_type == 1 ? "TouchDown/BtnDown" : (in.event_type == 2 ? "TouchUp/BtnUp" : "Move"));
        ss << "| " << in.host_time_ns << " | " << in.input_origin << " | " << in.pid << ":" << in.tid
           << " | " << typ
           << " | (" << in.raw_x << ", " << in.raw_y << ")"
           << " | (" << in.window_x << ", " << in.window_y << ")"
           << " | " << in.window_id << " |\n";
    }

    ss << "\n## 5. Framebuffer Mutation and Presentation Trace\n\n";
    ss << "| Seq | Operation | PID | Window ID | Surface ID | Damage Rect [x,y,w,h] | Pres ID | Screenshot / Hash |\n";
    ss << "|---|---|---|---|---|---|---|---|\n";
    for (const auto& fb : framebuffers_) {
        ss << "| " << fb.sequence << " | " << fb.operation << " | " << fb.pid << " | " << fb.window_id
           << " | " << fb.surface_id << " | [" << fb.damage_x << "," << fb.damage_y << "," << fb.damage_w << "," << fb.damage_h << "]"
           << " | " << fb.presentation_id
           << " | " << (fb.screenshot_file.empty() ? "-" : fb.screenshot_file + " (" + fb.screenshot_sha256.substr(0, 12) + "...)") << " |\n";
    }

    ss << "\n## 6. Resource Ownership Audit Trail\n\n";
    ss << "| Seq | Owner PID | Resource Type | Resource ID | Action | Host Timestamp (ns) |\n";
    ss << "|---|---|---|---|---|---|\n";
    for (const auto& r : resources_) {
        ss << "| " << r.sequence << " | " << r.owner_pid << " | " << r.resource_type << " | " << r.resource_id
           << " | " << r.action << " | " << r.host_time_ns << " |\n";
    }

    return ss.str();
}

} // namespace winios::kernel

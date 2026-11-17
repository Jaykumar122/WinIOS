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
    fb_sequence_ = 0;
}

void GuestTraceLogger::log_instruction(std::uint32_t pid, const std::string& name, std::uint64_t pc, std::uint32_t opcode, std::uint64_t retired_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    instructions_.push_back(InstructionTraceRecord{pid, name, pc, opcode, retired_count});
}

void GuestTraceLogger::log_syscall(std::uint32_t pid, const std::string& name, std::uint64_t num, const std::string& sname,
                                   const std::uint64_t args[6], std::uint32_t svc_imm, std::int64_t ret, std::int32_t err, const std::string& details) {
    std::lock_guard<std::mutex> lock(mutex_);
    SyscallTraceRecord r;
    r.pid = pid;
    r.process_name = name;
    r.syscall_num = num;
    r.syscall_name = sname;
    for (int i = 0; i < 6; ++i) {
        r.args[i] = args ? args[i] : 0;
    }
    r.svc_immediate = svc_imm;
    r.return_value = ret;
    r.error_code = err;
    r.details = details;
    syscalls_.push_back(r);
}

void GuestTraceLogger::log_input(std::uint32_t pid, std::uint32_t evt_type, std::int32_t rx, std::int32_t ry,
                                 std::int32_t wx, std::int32_t wy, std::uint32_t wid, std::uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    inputs_.push_back(InputTraceRecord{pid, evt_type, rx, ry, wx, wy, wid, timestamp});
}

void GuestTraceLogger::log_framebuffer(std::uint32_t surf_id, std::uint32_t wid, std::uint32_t pid,
                                      const std::string& op, std::uint32_t dx, std::uint32_t dy, std::uint32_t dw, std::uint32_t dh,
                                      const std::string& shot_file, const std::string& shot_hash) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++fb_sequence_;
    framebuffers_.push_back(FramebufferTraceRecord{
        fb_sequence_, surf_id, wid, pid, op, dx, dy, dw, dh, shot_file, shot_hash
    });
}

std::string GuestTraceLogger::export_markdown_correlation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "# Campaign 214 Stage 214.2 — Guest Instruction and Syscall Trace Correlation Report\n\n";
    ss << "## 1. Summary Metrics\n";
    ss << "- Total Recorded Guest Instruction Samples: " << instructions_.size() << "\n";
    ss << "- Total Recorded Syscalls: " << syscalls_.size() << "\n";
    ss << "- Total Input Events Delivered: " << inputs_.size() << "\n";
    ss << "- Total Framebuffer/Presentation Operations: " << framebuffers_.size() << "\n\n";

    ss << "## 2. Guest Syscall Trace Sequence\n\n";
    ss << "| PID | Process | Syscall | Immediate | Ret/Err | Arguments (X0..X5) | Details |\n";
    ss << "|---|---|---|---|---|---|---|\n";
    for (const auto& sc : syscalls_) {
        ss << "| " << sc.pid << " | " << sc.process_name << " | " << sc.syscall_name << " (" << sc.syscall_num << ")"
           << " | 0x" << std::hex << sc.svc_immediate << std::dec
           << " | " << sc.return_value << "/" << sc.error_code
           << " | 0x" << std::hex << sc.args[0] << ", 0x" << sc.args[1] << ", 0x" << sc.args[2]
           << ", 0x" << sc.args[3] << ", 0x" << sc.args[4] << ", 0x" << sc.args[5] << std::dec
           << " | " << sc.details << " |\n";
    }

    ss << "\n## 3. Input Event to Surface Mutation Correlation\n\n";
    ss << "| Timestamp (us) | PID | Event Type | Screen (X,Y) | Window (X,Y) | Window ID |\n";
    ss << "|---|---|---|---|---|---|\n";
    for (const auto& in : inputs_) {
        std::string typ = (in.event_type == 1 ? "TouchDown" : (in.event_type == 2 ? "TouchUp" : "TouchMove"));
        ss << "| " << in.timestamp_us << " | " << in.pid << " | " << typ
           << " | (" << in.raw_x << ", " << in.raw_y << ")"
           << " | (" << in.window_x << ", " << in.window_y << ")"
           << " | " << in.window_id << " |\n";
    }

    ss << "\n## 4. Framebuffer Mutation and Presentation Trace\n\n";
    ss << "| Seq | Operation | PID | Window ID | Surface ID | Damage Rect (x,y,w,h) | Screenshot / Hash |\n";
    ss << "|---|---|---|---|---|---|---|\n";
    for (const auto& fb : framebuffers_) {
        ss << "| " << fb.sequence << " | " << fb.operation << " | " << fb.pid << " | " << fb.window_id
           << " | " << fb.surface_id << " | [" << fb.damage_x << "," << fb.damage_y << "," << fb.damage_w << "," << fb.damage_h << "]"
           << " | " << (fb.screenshot_file.empty() ? "-" : fb.screenshot_file + " (" + fb.screenshot_sha256.substr(0, 12) + "...)") << " |\n";
    }

    return ss.str();
}

} // namespace winios::kernel

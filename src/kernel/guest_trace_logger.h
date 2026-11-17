#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

namespace winios::kernel {

struct InstructionTraceRecord {
    std::uint32_t pid{0};
    std::string process_name;
    std::uint64_t pc{0};
    std::uint32_t opcode{0};
    std::uint64_t instruction_index{0};
};

struct SyscallTraceRecord {
    std::uint32_t pid{0};
    std::string process_name;
    std::uint64_t syscall_num{0};
    std::string syscall_name;
    std::uint64_t args[6]{0};
    std::uint32_t svc_immediate{0x80};
    std::int64_t return_value{0};
    std::int32_t error_code{0};
    std::string details;
};

struct InputTraceRecord {
    std::uint32_t pid{0};
    std::uint32_t event_type{0}; // 1=Down, 2=Up, 3=Move
    std::int32_t raw_x{0};
    std::int32_t raw_y{0};
    std::int32_t window_x{0};
    std::int32_t window_y{0};
    std::uint32_t window_id{0};
    std::uint64_t timestamp_us{0};
};

struct FramebufferTraceRecord {
    std::uint64_t sequence{0};
    std::uint32_t surface_id{0};
    std::uint32_t window_id{0};
    std::uint32_t pid{0};
    std::string operation; // "fill", "update", "present", "composite", "host_present"
    std::uint32_t damage_x{0};
    std::uint32_t damage_y{0};
    std::uint32_t damage_w{0};
    std::uint32_t damage_h{0};
    std::string screenshot_file;
    std::string screenshot_sha256;
};

class GuestTraceLogger {
public:
    static GuestTraceLogger& instance();

    void reset();

    void log_instruction(std::uint32_t pid, const std::string& name, std::uint64_t pc, std::uint32_t opcode, std::uint64_t retired_count);

    void log_syscall(std::uint32_t pid, const std::string& name, std::uint64_t num, const std::string& sname,
                     const std::uint64_t args[6], std::uint32_t svc_imm, std::int64_t ret, std::int32_t err, const std::string& details);

    void log_input(std::uint32_t pid, std::uint32_t evt_type, std::int32_t rx, std::int32_t ry,
                   std::int32_t wx, std::int32_t wy, std::uint32_t wid, std::uint64_t timestamp);

    void log_framebuffer(std::uint32_t surf_id, std::uint32_t wid, std::uint32_t pid,
                         const std::string& op, std::uint32_t dx, std::uint32_t dy, std::uint32_t dw, std::uint32_t dh,
                         const std::string& shot_file = "", const std::string& shot_hash = "");

    [[nodiscard]] const std::vector<InstructionTraceRecord>& instruction_records() const { return instructions_; }
    [[nodiscard]] const std::vector<SyscallTraceRecord>& syscall_records() const { return syscalls_; }
    [[nodiscard]] const std::vector<InputTraceRecord>& input_records() const { return inputs_; }
    [[nodiscard]] const std::vector<FramebufferTraceRecord>& framebuffer_records() const { return framebuffers_; }

    std::string export_markdown_correlation() const;

private:
    GuestTraceLogger() = default;

    mutable std::mutex mutex_;
    std::vector<InstructionTraceRecord> instructions_;
    std::vector<SyscallTraceRecord> syscalls_;
    std::vector<InputTraceRecord> inputs_;
    std::vector<FramebufferTraceRecord> framebuffers_;
    std::uint64_t fb_sequence_{0};
};

} // namespace winios::kernel

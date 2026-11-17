#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace winios::kernel {

enum class ExecutionDomain : std::uint8_t {
    GuestARM64 = 0,
    HostRuntime = 1,
    GuestToHostBoundary = 2,
    HostToGuestBoundary = 3
};

inline const char* execution_domain_to_string(ExecutionDomain d) noexcept {
    switch (d) {
    case ExecutionDomain::GuestARM64: return "GuestARM64";
    case ExecutionDomain::HostRuntime: return "HostRuntime";
    case ExecutionDomain::GuestToHostBoundary: return "Guest->Host";
    case ExecutionDomain::HostToGuestBoundary: return "Host->Guest";
    default: return "Unknown";
    }
}

struct InstructionTraceRecord {
    std::uint32_t pid{0};
    std::uint32_t tid{0};
    std::string process_name;
    std::uint64_t pc{0};
    std::uint32_t opcode{0};
    std::uint64_t instruction_index{0};
    std::uint32_t exception_level{0}; // 0 = EL0, 1 = EL1
    std::uint64_t host_time_ns{0};
};

struct SyscallTraceRecord {
    std::uint32_t pid{0};
    std::uint32_t tid{0};
    std::string process_name;
    std::uint64_t syscall_num{0};
    std::string syscall_name;
    std::string abi_identifier{"CategoryC_v1.0"}; // "DarwinBSD", "DarwinMach", "CategoryC_v1.0"
    std::uint64_t args[6]{0};
    std::uint32_t svc_immediate{0x80};
    std::int64_t return_value{0};
    std::int32_t error_code{0};
    std::uint32_t exception_level{1}; // Intercepted at EL1
    std::string details;
    std::uint64_t host_time_ns{0};
};

struct InputTraceRecord {
    std::uint32_t pid{0};
    std::uint32_t tid{0};
    std::uint32_t event_type{0}; // 1=Down, 2=Up, 3=Move
    std::string input_origin{"Win32Host"}; // "Win32Mouse", "Win32Keyboard", "TouchDigitizer", "SyntheticInjected"
    std::int32_t raw_x{0};
    std::int32_t raw_y{0};
    std::int32_t window_x{0};
    std::int32_t window_y{0};
    std::uint32_t window_id{0};
    std::uint64_t timestamp_us{0};
    std::uint64_t host_time_ns{0};
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
    std::uint64_t presentation_id{0};
    std::uint64_t host_time_ns{0};
};

struct BoundaryTransitionRecord {
    std::uint64_t sequence{0};
    std::uint32_t pid{0};
    std::uint32_t tid{0};
    ExecutionDomain from_domain{ExecutionDomain::GuestARM64};
    ExecutionDomain to_domain{ExecutionDomain::HostRuntime};
    std::string reason; // "SVC_Trap", "ERET_Return", "PageFault", "HostCallback"
    std::uint64_t pc{0};
    std::uint32_t exception_level{0};
    std::uint64_t host_time_ns{0};
};

struct ResourceOwnershipRecord {
    std::uint64_t sequence{0};
    std::uint32_t owner_pid{0};
    std::string resource_type; // "Window", "Surface", "VfsFile", "Package"
    std::uint32_t resource_id{0};
    std::string action; // "Allocated", "Transferred", "Released"
    std::uint64_t host_time_ns{0};
};

class GuestTraceLogger {
public:
    static GuestTraceLogger& instance();

    void reset();

    // Instruction logging
    void log_instruction(std::uint32_t pid, const std::string& name, std::uint64_t pc, std::uint32_t opcode,
                         std::uint64_t retired_count, std::uint32_t tid = 0, std::uint32_t el = 0);

    // Syscall logging
    void log_syscall(std::uint32_t pid, const std::string& name, std::uint64_t num, const std::string& sname,
                     const std::uint64_t args[6], std::uint32_t svc_imm, std::int64_t ret, std::int32_t err,
                     const std::string& details, const std::string& abi = "CategoryC_v1.0", std::uint32_t tid = 0);

    // Input logging
    void log_input(std::uint32_t pid, std::uint32_t evt_type, std::int32_t rx, std::int32_t ry,
                   std::int32_t wx, std::int32_t wy, std::uint32_t wid, std::uint64_t timestamp,
                   const std::string& origin = "Win32Host", std::uint32_t tid = 0);

    // Framebuffer logging
    void log_framebuffer(std::uint32_t surf_id, std::uint32_t wid, std::uint32_t pid,
                         const std::string& op, std::uint32_t dx, std::uint32_t dy, std::uint32_t dw, std::uint32_t dh,
                         const std::string& shot_file = "", const std::string& shot_hash = "", std::uint64_t pres_id = 0);

    // Boundary transition logging
    void log_boundary_transition(std::uint32_t pid, std::uint32_t tid, ExecutionDomain from_domain,
                                ExecutionDomain to_domain, const std::string& reason, std::uint64_t pc, std::uint32_t el);

    // Resource ownership logging
    void log_resource_ownership(std::uint32_t owner_pid, const std::string& res_type, std::uint32_t res_id, const std::string& action);

    // Accessors
    [[nodiscard]] const std::vector<InstructionTraceRecord>& instruction_records() const { return instructions_; }
    [[nodiscard]] const std::vector<SyscallTraceRecord>& syscall_records() const { return syscalls_; }
    [[nodiscard]] const std::vector<InputTraceRecord>& input_records() const { return inputs_; }
    [[nodiscard]] const std::vector<FramebufferTraceRecord>& framebuffer_records() const { return framebuffers_; }
    [[nodiscard]] const std::vector<BoundaryTransitionRecord>& boundary_records() const { return boundaries_; }
    [[nodiscard]] const std::vector<ResourceOwnershipRecord>& resource_records() const { return resources_; }

    std::string export_markdown_correlation() const;

private:
    GuestTraceLogger() = default;

    static std::uint64_t current_host_time_ns() noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    mutable std::mutex mutex_;
    std::vector<InstructionTraceRecord> instructions_;
    std::vector<SyscallTraceRecord> syscalls_;
    std::vector<InputTraceRecord> inputs_;
    std::vector<FramebufferTraceRecord> framebuffers_;
    std::vector<BoundaryTransitionRecord> boundaries_;
    std::vector<ResourceOwnershipRecord> resources_;
    std::uint64_t fb_sequence_{0};
    std::uint64_t boundary_sequence_{0};
    std::uint64_t resource_sequence_{0};
};

} // namespace winios::kernel

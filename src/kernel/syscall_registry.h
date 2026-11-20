#pragma once

#include <cstdint>
#include <string_view>
#include <optional>
#include <array>

namespace winios::kernel {

enum class SyscallCategory : std::uint8_t {
    PosixCore = 0,
    MemoryManagement = 1,
    Threading = 2,
    CategoryCDisplay = 3,
    CategoryCStorage = 4,
    CategoryCIpc = 5,
    CategoryCDevices = 6,
    Unknown = 7
};

inline const char* syscall_category_to_string(SyscallCategory cat) noexcept {
    switch (cat) {
    case SyscallCategory::PosixCore: return "PosixCore";
    case SyscallCategory::MemoryManagement: return "MemoryManagement";
    case SyscallCategory::Threading: return "Threading";
    case SyscallCategory::CategoryCDisplay: return "CategoryCDisplay";
    case SyscallCategory::CategoryCStorage: return "CategoryCStorage";
    case SyscallCategory::CategoryCIpc: return "CategoryCIpc";
    case SyscallCategory::CategoryCDevices: return "CategoryCDevices";
    default: return "Unknown";
    }
}

struct SyscallDescriptor {
    std::uint64_t       number{0};
    std::string_view    name{};
    SyscallCategory     category{SyscallCategory::Unknown};
    std::uint32_t       min_abi_version{0x00010000};
    std::uint8_t        arg_count{0};
    bool                modifies_memory{false};
    std::string_view    description{};
};

inline constexpr std::array<SyscallDescriptor, 32> kCanonicalSyscallRegistry = {{
    { 1,   "exit",                     SyscallCategory::PosixCore,         0x00010000, 1, false, "Terminate calling process with exit code" },
    { 3,   "read",                     SyscallCategory::PosixCore,         0x00010000, 3, true,  "Read bytes from file descriptor into user buffer" },
    { 4,   "write",                    SyscallCategory::PosixCore,         0x00010000, 3, false, "Write bytes from user buffer to file descriptor" },
    { 5,   "open",                     SyscallCategory::PosixCore,         0x00010000, 3, false, "Open file in VFS" },
    { 6,   "close",                    SyscallCategory::PosixCore,         0x00010000, 1, false, "Close file descriptor" },
    { 20,  "getpid",                   SyscallCategory::PosixCore,         0x00010000, 0, false, "Get current process identifier" },
    { 73,  "munmap",                   SyscallCategory::MemoryManagement,  0x00010000, 2, false, "Unmap virtual memory pages" },
    { 74,  "mprotect",                 SyscallCategory::MemoryManagement,  0x00010000, 3, false, "Change page protection flags" },
    { 197, "mmap",                     SyscallCategory::MemoryManagement,  0x00010000, 6, false, "Map memory pages or anonymous backing" },
    { 338, "stat64",                   SyscallCategory::PosixCore,         0x00010000, 2, true,  "Get file status into DarwinStat64 buffer" },
    { 339, "fstat64",                  SyscallCategory::PosixCore,         0x00010000, 2, true,  "Get open file descriptor status" },
    { 357, "bsdthread_create",         SyscallCategory::Threading,         0x00010000, 5, false, "Create new guest thread" },
    { 358, "bsdthread_terminate",      SyscallCategory::Threading,         0x00010000, 1, false, "Terminate calling thread" },

    // Category C: Display & Window Management
    { 500, "winios_window_create",     SyscallCategory::CategoryCDisplay,  0x00010000, 6, false, "Create window with frame and title" },
    { 501, "winios_window_destroy",    SyscallCategory::CategoryCDisplay,  0x00010000, 1, false, "Destroy window by identifier" },
    { 502, "winios_surface_update",    SyscallCategory::CategoryCDisplay,  0x00010000, 3, false, "Update surface buffer region" },
    { 503, "winios_surface_present",   SyscallCategory::CategoryCDisplay,  0x00010000, 1, false, "Commit surface to compositor" },
    { 504, "winios_poll_event",        SyscallCategory::CategoryCDisplay,  0x00010000, 2, true,  "Poll pending UI/lifecycle event" },
    { 505, "winios_surface_fill",      SyscallCategory::CategoryCDisplay,  0x00010000, 6, false, "Fill rectangle on window surface" },
    { 506, "winios_set_window_title",  SyscallCategory::CategoryCDisplay,  0x00010000, 3, false, "Update window title string" },
    { 507, "winios_get_abi_version",   SyscallCategory::CategoryCDisplay,  0x00010000, 1, false, "Query or negotiate Category C ABI version" },
    { 508, "winios_window_get_geometry", SyscallCategory::CategoryCDisplay,0x00010100, 2, true,  "Get window geometry (ABI v1.1+)" },
    { 509, "winios_window_set_geometry", SyscallCategory::CategoryCDisplay,0x00010100, 2, false, "Set window geometry (ABI v1.1+)" },
    { 510, "winios_window_set_focus",  SyscallCategory::CategoryCDisplay,  0x00010100, 1, false, "Raise window and set focus" },
    { 511, "winios_window_list",       SyscallCategory::CategoryCDisplay,  0x00010100, 2, true,  "Enumerate windows owned by process" },
    { 512, "winios_create_timer",      SyscallCategory::CategoryCDisplay,  0x00010100, 2, false, "Create guest event timer" },

    // Category C: Storage & IPC
    { 513, "winios_storage_open",      SyscallCategory::CategoryCStorage,  0x00010200, 3, false, "Open private application storage file" },
    { 514, "winios_storage_read",      SyscallCategory::CategoryCStorage,  0x00010200, 4, true,  "Read from isolated storage file" },
    { 515, "winios_storage_write",     SyscallCategory::CategoryCStorage,  0x00010200, 4, false, "Write to isolated storage file" },
    { 516, "winios_storage_close",     SyscallCategory::CategoryCStorage,  0x00010200, 1, false, "Close isolated storage file" },
    { 517, "winios_ipc_port_create",   SyscallCategory::CategoryCIpc,      0x00010200, 1, false, "Create IPC message port" },
    { 518, "winios_ipc_send",          SyscallCategory::CategoryCIpc,      0x00010200, 4, false, "Send IPC message via capability token" }
}};

inline std::optional<SyscallDescriptor> lookup_syscall(std::uint64_t sys_num) noexcept {
    for (const auto& desc : kCanonicalSyscallRegistry) {
        if (desc.number == sys_num) {
            return desc;
        }
    }
    return std::nullopt;
}

} // namespace winios::kernel

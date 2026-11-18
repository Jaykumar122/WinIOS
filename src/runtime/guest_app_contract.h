#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace winios::runtime {

// WinIOS Guest Application Execution Contract v1.2 (Campaign 217)
constexpr uint32_t kGuestAppContractVersion = 0x00010200;
constexpr uint32_t kGuestAppMinSupportedContract = 0x00010000;

#pragma pack(push, 8)

struct GuestAppArgBlock {
    uint32_t contract_version;    // kGuestAppContractVersion
    uint32_t process_id;          // Process ID
    uint64_t app_base_address;    // Base address where Mach-O is mapped
    uint64_t stack_base;          // Stack high address
    uint64_t stack_size;          // Allocated stack size
    uint64_t heap_base;           // Heap starting address
    uint64_t heap_limit;          // Heap maximum address
    uint32_t argc;                // Argument count
    uint32_t envc;                // Environment count
    uint64_t argv_ptrs;           // Guest pointer to argv array
    uint64_t envp_ptrs;           // Guest pointer to envp array
    uint32_t initial_window_id;   // Default window ID created for app (if any, or 0)
    uint32_t bundle_storage_id;   // Isolated storage partition ID
    uint32_t reserved[4];         // Padding / future extensions
};

enum class GuestTerminationReason : uint32_t {
    kNormal = 0,
    kException = 1,
    kKilled = 2,
    kQuotaExceeded = 3,
    kInvalidSyscall = 4
};

struct GuestExitDescriptor {
    uint32_t exit_code;
    uint32_t termination_reason; // GuestTerminationReason
    uint64_t retired_instructions;
    uint64_t elapsed_us;
};

#pragma pack(pop)

static_assert(sizeof(GuestAppArgBlock) == 96, "GuestAppArgBlock must be exactly 96 bytes");
static_assert(sizeof(GuestExitDescriptor) == 24, "GuestExitDescriptor must be exactly 24 bytes");

class GuestAppContract {
public:
    static bool ValidateArgBlock(const GuestAppArgBlock& block, std::string* error_out = nullptr);
    static bool NegotiateAbiVersion(uint32_t requested_version, uint32_t* negotiated_version);
    static bool ValidateStackAlignment(uint64_t sp);
    static GuestAppArgBlock CreateDefault(uint32_t pid, uint64_t app_base, uint64_t stack_base,
                                         uint64_t stack_sz, uint64_t heap_base, uint64_t heap_sz);
};

} // namespace winios::runtime

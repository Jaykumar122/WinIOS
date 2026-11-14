#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cpu/cpu.h"
#include "loader/macho_loader.h"
#include "memory/memory.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// Synthetic Kernel Boot Constants
// -----------------------------------------------------------------------------
inline constexpr std::uint64_t kSyntheticKernelMagic   = 0x53594E54484B524EULL; // "SYNTHKRN"
inline constexpr std::uint32_t kSyntheticKernelVersion = 1u;

// Boot flags
inline constexpr std::uint32_t kBootFlagMmuEnabled     = 1u << 0;
inline constexpr std::uint32_t kBootFlagDtbPresent     = 1u << 1;
inline constexpr std::uint32_t kBootFlagSmpEnabled     = 1u << 2;
inline constexpr std::uint32_t kBootFlagTimerEnabled   = 1u << 3;
inline constexpr std::uint32_t kBootFlagConsolePresent = 1u << 4;

// -----------------------------------------------------------------------------
// SyntheticKernelBootInfo: Packed Boot Contract Passed in Register X0 at EL1
// Total size: exactly 256 bytes
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
struct SyntheticKernelBootInfo {
    std::uint64_t magic{kSyntheticKernelMagic};    // offset 0
    std::uint32_t version{kSyntheticKernelVersion};// offset 8
    std::uint32_t flags{0};                        // offset 12
    std::uint64_t dram_base{0};                    // offset 16
    std::uint64_t dram_size{0};                    // offset 24
    std::uint64_t kernel_text_base{0};             // offset 32
    std::uint64_t kernel_text_size{0};             // offset 40
    std::uint64_t kernel_data_base{0};             // offset 48
    std::uint64_t kernel_data_size{0};             // offset 56
    std::uint64_t entry_point{0};                  // offset 64
    std::uint64_t initial_sp_el1{0};               // offset 72
    std::uint64_t initial_sp_el0{0};               // offset 80
    std::uint64_t mmio_console_base{0};            // offset 88
    std::uint64_t mmio_console_size{0};            // offset 96
    std::uint64_t free_memory_base{0};             // offset 104
    std::uint64_t free_memory_size{0};             // offset 112
    std::uint64_t cmdline_addr{0};                 // offset 120
    std::uint64_t cmdline_size{0};                 // offset 128
    std::uint64_t dtb_base{0};                     // offset 136
    std::uint64_t dtb_size{0};                     // offset 144
    std::uint64_t boot_timestamp_ticks{0};         // offset 152
    std::uint64_t cpu_count{1};                    // offset 160
    std::uint64_t page_granule_bytes{16384};       // offset 168 (16 KB)
    std::uint64_t reserved[10]{};                  // offset 176 (80 bytes)
};
#pragma pack(pop)
static_assert(sizeof(SyntheticKernelBootInfo) == 256, "SyntheticKernelBootInfo must be 256 bytes");

// -----------------------------------------------------------------------------
// Load Configuration
// -----------------------------------------------------------------------------
struct SyntheticKernelLoadConfig {
    std::uint64_t dram_base{0x40000000ULL};
    std::uint64_t dram_size{0x10000000ULL};        // 256 MB default
    std::uint64_t boot_info_addr{0x40008000ULL};   // Placed safely in low memory
    std::uint64_t initial_sp_el1{0x40020000ULL};
    std::uint64_t initial_sp_el0{0x40030000ULL};
    std::uint64_t mmio_console_base{0x090000ULL};
    std::uint64_t mmio_console_size{0x1000ULL};
    std::string cmdline{"console=tty0 debug=1"};
    std::uint64_t dtb_base{0};
    std::uint64_t dtb_size{0};
    std::uint32_t cpu_count{1};
    std::uint64_t page_granule{16384};             // 16 KB
    bool auto_calculate_free_memory{true};
    std::uint64_t explicit_free_base{0};
    std::uint64_t explicit_free_size{0};
};

// -----------------------------------------------------------------------------
// Load Status & Result
// -----------------------------------------------------------------------------
enum class SyntheticKernelLoadStatus {
    Success,
    FileNotFound,
    FileReadError,
    InvalidMachO,
    InvalidMagic,
    UnsupportedArchitecture,
    MemoryCapacityExceeded,
    InvalidBootInfo,
    NullMemory
};

[[nodiscard]] const char* synthetic_kernel_load_status_string(SyntheticKernelLoadStatus s) noexcept;

struct SyntheticKernelLoadResult {
    SyntheticKernelLoadStatus status{SyntheticKernelLoadStatus::FileNotFound};
    std::string error_message;
    MachOLoadResult macho_result;
    SyntheticKernelBootInfo boot_info{};
    std::uint64_t boot_info_guest_addr{0};
    std::uint64_t entry_point{0};
    std::uint64_t initial_sp_el1{0};
    std::uint64_t initial_sp_el0{0};
    std::string sha256_hex;

    [[nodiscard]] bool is_success() const noexcept {
        return status == SyntheticKernelLoadStatus::Success;
    }

    void configure_cpu(CPU& cpu) const {
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);
        cpu.set_daif(0);
        cpu.set_program_counter(entry_point);
        cpu.set_stack_pointer(initial_sp_el1);
        cpu.set_sp_el0(initial_sp_el0);
        cpu.write_register(0, boot_info_guest_addr);
        cpu.write_register(1, kSyntheticKernelMagic);
    }
};

// -----------------------------------------------------------------------------
// SyntheticKernelLoader
// -----------------------------------------------------------------------------
class SyntheticKernelLoader {
public:
    SyntheticKernelLoader() = default;
    ~SyntheticKernelLoader() = default;

    [[nodiscard]] static SyntheticKernelLoadResult load_from_file(
        const std::string& path,
        Memory& guest_mem,
        const SyntheticKernelLoadConfig& config = {});

    [[nodiscard]] static SyntheticKernelLoadResult load_from_buffer(
        const void* data,
        std::size_t size,
        Memory& guest_mem,
        const SyntheticKernelLoadConfig& config = {});

    static bool write_boot_info(
        Memory& guest_mem,
        std::uint64_t guest_addr,
        const SyntheticKernelBootInfo& info);

    static bool read_boot_info(
        const Memory& guest_mem,
        std::uint64_t guest_addr,
        SyntheticKernelBootInfo& out_info);

    [[nodiscard]] static bool validate_boot_info(
        const SyntheticKernelBootInfo& info,
        std::string* out_error = nullptr);
};

} // namespace winios::loader

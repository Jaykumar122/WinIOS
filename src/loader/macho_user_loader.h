#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

namespace winios::loader {

inline constexpr std::uint32_t kMhMagic64 = 0xFEEDFACFu;
inline constexpr std::int32_t  kCpuTypeArm64 = 0x0100000C;
inline constexpr std::uint32_t kLcSegment64 = 0x19u;
inline constexpr std::uint32_t kLcUnixThread = 0x5u;
inline constexpr std::uint32_t kLcMain = 0x80000028u;

struct LoadedUserImage {
    bool valid{false};
    std::uint64_t entry_point{0};
    std::uint64_t text_vmaddr{0};
    std::uint64_t text_vmsize{0};
    std::uint64_t data_vmaddr{0};
    std::uint64_t data_vmsize{0};
    std::uint64_t data_const_vmaddr{0};
    std::uint64_t data_const_vmsize{0};
    std::uint64_t stack_size{0x10000ULL}; // 64 KiB default stack
    std::size_t segments_loaded{0};
    std::string error_message;
};

class MachoUserLoader {
public:
    // Parse and load a freestanding 64-bit ARM64 Mach-O binary into a user address space
    static LoadedUserImage load_image(const std::vector<std::uint8_t>& binary,
                                     memory::UserAddressSpace& uas,
                                     memory::PhysicalPageManager& ppm,
                                     Memory& mem);

    // Header validation helper
    static bool validate_header(const std::uint8_t* data, std::size_t size, std::string* out_err = nullptr);

    // Embedded test applications
    static std::vector<std::uint8_t> get_embedded_first_app();
    static std::vector<std::uint8_t> get_embedded_init_app();
    static std::vector<std::uint8_t> get_embedded_test_workload();
};

} // namespace winios::loader

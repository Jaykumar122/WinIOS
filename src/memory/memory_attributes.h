#pragma once

#include <cstdint>
#include <string>

namespace winios::memory {

// -----------------------------------------------------------------------------
// ARMv8-A Memory Types and Attributes (ARM DDI 0487 D13.2.94 MAIR_EL1)
// -----------------------------------------------------------------------------
enum class MemoryType : std::uint8_t {
    Device_nGnRnE = 0,
    Device_nGnRE  = 1,
    Device_nGRE   = 2,
    Device_GRE    = 3,
    Normal        = 4,
};

enum class Cacheability : std::uint8_t {
    NonCacheable = 0,
    WriteThrough = 1,
    WriteBack    = 2,
};

enum class ShareabilityDomain : std::uint8_t {
    NonShareable   = 0,
    Reserved       = 1,
    OuterShareable = 2,
    InnerShareable = 3,
};

struct DecodedMemoryAttributes {
    MemoryType type{MemoryType::Normal};
    Cacheability inner_cacheability{Cacheability::WriteBack};
    Cacheability outer_cacheability{Cacheability::WriteBack};
    ShareabilityDomain shareability{ShareabilityDomain::InnerShareable};
    bool is_device{false};
    bool allow_unaligned{true};
    bool allow_atomics{true};
    bool allow_dc_zva{true};
    std::uint8_t raw_mair{0xFF};
    std::uint8_t raw_sh{0x03};

    [[nodiscard]] std::string to_string() const;
};

// Decodes raw MAIR attribute byte and 2-bit shareability field from descriptor
[[nodiscard]] DecodedMemoryAttributes decode_memory_attributes(std::uint8_t attr_byte,
                                                              std::uint8_t sh_bits = 0b11) noexcept;

} // namespace winios::memory

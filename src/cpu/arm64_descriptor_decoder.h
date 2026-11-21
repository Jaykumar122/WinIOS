#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <sstream>
#include <iomanip>

namespace winios::cpu {

enum class Arm64Granule : std::uint8_t {
    Granule4KB  = 0,
    Granule16KB = 1,
    Granule64KB = 2
};

enum class DecodedDescriptorType : std::uint8_t {
    Invalid = 0,
    Block   = 1,
    Table   = 2,
    Page    = 3
};

struct DecodedDescriptor {
    std::uint64_t raw_value{0};
    unsigned level{1};
    Arm64Granule granule{Arm64Granule::Granule16KB};
    DecodedDescriptorType type{DecodedDescriptorType::Invalid};
    bool valid{false};

    std::uint64_t output_address{0};
    std::uint8_t  attr_indx{0};
    bool          non_secure{false};
    std::uint8_t  ap{0};        // AP[2:1]
    std::uint8_t  sh{0};        // Shareability
    bool          access_flag{false};
    bool          non_global{false};
    bool          contiguous{false};
    bool          pxn{false};
    bool          uxn{false};

    // Table hierarchical attributes (Table descriptors only)
    bool          pxn_table{false};
    bool          xn_table{false};
    std::uint8_t  ap_table{0};
    bool          ns_table{false};

    [[nodiscard]] bool is_writable(bool el0) const noexcept {
        if (!valid) return false;
        if (el0) {
            return ap == 0b01;
        }
        return (ap == 0b00 || ap == 0b01);
    }

    [[nodiscard]] bool is_executable(bool el0) const noexcept {
        if (!valid) return false;
        if (el0) {
            return !uxn && (ap & 0b01) != 0;
        }
        return !pxn;
    }

    [[nodiscard]] bool is_wx_violation(bool el0 = false) const noexcept {
        return is_writable(el0) && is_executable(el0);
    }

    [[nodiscard]] std::string to_string() const {
        std::ostringstream oss;
        oss << "Raw: 0x" << std::hex << std::setw(16) << std::setfill('0') << raw_value << std::dec
            << " [Level " << level << " "
            << ((type == DecodedDescriptorType::Block) ? "Block" :
                (type == DecodedDescriptorType::Table) ? "Table" :
                (type == DecodedDescriptorType::Page) ? "Page" : "Invalid")
            << "] -> OutPA: 0x" << std::hex << output_address << std::dec
            << " AttrIndx=" << static_cast<unsigned>(attr_indx)
            << " AP=0b" << static_cast<unsigned>((ap >> 1) & 1) << static_cast<unsigned>(ap & 1)
            << " SH=" << static_cast<unsigned>(sh)
            << " AF=" << (access_flag ? 1 : 0)
            << " nG=" << (non_global ? 1 : 0)
            << " PXN=" << (pxn ? 1 : 0)
            << " UXN=" << (uxn ? 1 : 0);
        return oss.str();
    }
};

class Arm64DescriptorDecoder {
public:
    static DecodedDescriptor decode(
        std::uint64_t descriptor,
        unsigned level,
        Arm64Granule granule = Arm64Granule::Granule16KB) noexcept {

        DecodedDescriptor d{};
        d.raw_value = descriptor;
        d.level = level;
        d.granule = granule;

        // Bit 0: Valid bit
        d.valid = (descriptor & 0x1ULL) != 0;
        if (!d.valid) {
            d.type = DecodedDescriptorType::Invalid;
            return d;
        }

        const bool bit1 = (descriptor & 0x2ULL) != 0;

        if (level == 3) {
            // Level 3 entries with bit 1 = 1 are Page descriptors; bit 1 = 0 is reserved/invalid
            if (bit1) {
                d.type = DecodedDescriptorType::Page;
            } else {
                d.type = DecodedDescriptorType::Invalid;
                d.valid = false;
                return d;
            }
        } else {
            // Level 0, 1, 2:
            // bit1 = 0: Block descriptor (Level 1, 2 only; Level 0 block is invalid in ARMv8)
            // bit1 = 1: Table descriptor
            if (bit1) {
                d.type = DecodedDescriptorType::Table;
            } else {
                if (level == 0) {
                    d.type = DecodedDescriptorType::Invalid;
                    d.valid = false;
                    return d;
                }
                d.type = DecodedDescriptorType::Block;
            }
        }

        if (d.type == DecodedDescriptorType::Table) {
            // Table descriptor output address
            std::uint64_t addr_mask = 0x0000FFFFFFFFF000ULL;
            if (granule == Arm64Granule::Granule16KB) {
                addr_mask = 0x0000FFFFFFFFC000ULL;
            } else if (granule == Arm64Granule::Granule64KB) {
                addr_mask = 0x0000FFFFFFFF0000ULL;
            }
            d.output_address = descriptor & addr_mask;
            d.pxn_table = (descriptor & (1ULL << 59u)) != 0;
            d.xn_table  = (descriptor & (1ULL << 60u)) != 0;
            d.ap_table  = static_cast<std::uint8_t>((descriptor >> 61u) & 0x3u);
            d.ns_table  = (descriptor & (1ULL << 63u)) != 0;
            return d;
        }

        // Block or Page descriptor attributes
        std::uint64_t out_mask = 0;
        if (granule == Arm64Granule::Granule16KB) {
            if (d.type == DecodedDescriptorType::Block) {
                if (level == 1) {
                    // Level 1 16KB block = 32 MiB (bits [47:25])
                    out_mask = 0x0000FFFFFE000000ULL;
                } else if (level == 2) {
                    // Level 2 16KB block = 2 MiB (bits [47:21])
                    out_mask = 0x0000FFFFFFE00000ULL;
                }
            } else {
                // Level 3 16KB page = 16 KB (bits [47:14])
                out_mask = 0x0000FFFFFFFFC000ULL;
            }
        } else if (granule == Arm64Granule::Granule4KB) {
            if (d.type == DecodedDescriptorType::Block) {
                if (level == 1) {
                    out_mask = 0x0000FFFFC0000000ULL; // 1 GiB
                } else if (level == 2) {
                    out_mask = 0x0000FFFFFFE00000ULL; // 2 MiB
                }
            } else {
                out_mask = 0x0000FFFFFFFFF000ULL; // 4 KB
            }
        } else { // 64KB
            if (d.type == DecodedDescriptorType::Block) {
                out_mask = 0x0000FFFFFFE00000ULL; // 512 MiB
            } else {
                out_mask = 0x0000FFFFFFFF0000ULL; // 64 KB
            }
        }

        d.output_address = descriptor & out_mask;
        d.attr_indx   = static_cast<std::uint8_t>((descriptor >> 2u) & 0x7u);
        d.non_secure  = (descriptor & (1ULL << 5u)) != 0;
        d.ap          = static_cast<std::uint8_t>((descriptor >> 6u) & 0x3u);
        d.sh          = static_cast<std::uint8_t>((descriptor >> 8u) & 0x3u);
        d.access_flag = (descriptor & (1ULL << 10u)) != 0;
        d.non_global  = (descriptor & (1ULL << 11u)) != 0;
        d.contiguous  = (descriptor & (1ULL << 52u)) != 0;
        d.pxn         = (descriptor & (1ULL << 53u)) != 0;
        d.uxn         = (descriptor & (1ULL << 54u)) != 0;

        return d;
    }
};

} // namespace winios::cpu

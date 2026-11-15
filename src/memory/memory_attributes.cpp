#include "memory/memory_attributes.h"

namespace winios::memory {

namespace {

Cacheability decode_cacheability(std::uint8_t nibble) noexcept {
    if (nibble == 0b0100) {
        return Cacheability::NonCacheable;
    }
    const std::uint8_t top2 = (nibble >> 2u) & 0x3u;
    if (top2 == 0b10) {
        return Cacheability::WriteThrough;
    }
    if (top2 == 0b11) {
        return Cacheability::WriteBack;
    }
    // Tagged Normal or Transient Write-Through
    return (nibble & 0b1000) ? Cacheability::WriteBack : Cacheability::NonCacheable;
}

} // namespace

DecodedMemoryAttributes decode_memory_attributes(std::uint8_t attr_byte, std::uint8_t sh_bits) noexcept {
    DecodedMemoryAttributes attr{};
    attr.raw_mair = attr_byte;
    attr.raw_sh = sh_bits & 0x3u;

    // Decode Shareability
    switch (attr.raw_sh) {
    case 0b00: attr.shareability = ShareabilityDomain::NonShareable; break;
    case 0b10: attr.shareability = ShareabilityDomain::OuterShareable; break;
    case 0b11: attr.shareability = ShareabilityDomain::InnerShareable; break;
    default:   attr.shareability = ShareabilityDomain::Reserved; break;
    }

    // ARMv8 Device memory encoding: upper nibble is 0000
    if ((attr_byte & 0xF0u) == 0x00u) {
        attr.is_device = true;
        attr.allow_unaligned = false;
        attr.allow_atomics = false;
        attr.allow_dc_zva = false;
        attr.inner_cacheability = Cacheability::NonCacheable;
        attr.outer_cacheability = Cacheability::NonCacheable;
        // Device memory is always treated as Outer Shareable architecturally
        attr.shareability = ShareabilityDomain::OuterShareable;

        const std::uint8_t lower = attr_byte & 0x0Fu;
        if (lower == 0x00u) {
            attr.type = MemoryType::Device_nGnRnE;
        } else if (lower == 0x04u) {
            attr.type = MemoryType::Device_nGnRE;
        } else if (lower == 0x08u) {
            attr.type = MemoryType::Device_nGRE;
        } else {
            attr.type = MemoryType::Device_GRE;
        }
        return attr;
    }

    // Normal memory
    attr.type = MemoryType::Normal;
    attr.is_device = false;
    attr.allow_unaligned = true;
    attr.allow_atomics = true;
    attr.allow_dc_zva = true;

    attr.outer_cacheability = decode_cacheability(static_cast<std::uint8_t>((attr_byte >> 4u) & 0x0Fu));
    attr.inner_cacheability = decode_cacheability(static_cast<std::uint8_t>(attr_byte & 0x0Fu));

    return attr;
}

std::string DecodedMemoryAttributes::to_string() const {
    if (is_device) {
        switch (type) {
        case MemoryType::Device_nGnRnE: return "Device-nGnRnE";
        case MemoryType::Device_nGnRE:  return "Device-nGnRE";
        case MemoryType::Device_nGRE:   return "Device-nGRE";
        case MemoryType::Device_GRE:    return "Device-GRE";
        default:                        return "Device-Unknown";
        }
    }
    std::string s = "Normal (Inner=";
    switch (inner_cacheability) {
    case Cacheability::NonCacheable: s += "NC"; break;
    case Cacheability::WriteThrough: s += "WT"; break;
    case Cacheability::WriteBack:    s += "WB"; break;
    }
    s += ", Outer=";
    switch (outer_cacheability) {
    case Cacheability::NonCacheable: s += "NC"; break;
    case Cacheability::WriteThrough: s += "WT"; break;
    case Cacheability::WriteBack:    s += "WB"; break;
    }
    s += ", SH=";
    switch (shareability) {
    case ShareabilityDomain::NonShareable:   s += "NS"; break;
    case ShareabilityDomain::OuterShareable: s += "OSH"; break;
    case ShareabilityDomain::InnerShareable: s += "ISH"; break;
    default:                                 s += "Reserved"; break;
    }
    s += ")";
    return s;
}

} // namespace winios::memory

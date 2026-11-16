#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::cpu {

// -----------------------------------------------------------------------------
// ARM64 Vector Table Structure (2048-byte aligned)
// -----------------------------------------------------------------------------
enum class ExceptionOrigin {
    CurrentEL_SP0,     // 0x000 - 0x180
    CurrentEL_SPx,     // 0x200 - 0x380
    LowerEL_AArch64,   // 0x400 - 0x580
    LowerEL_AArch32    // 0x600 - 0x780
};

enum class ExceptionVectorType {
    Synchronous, // Offset + 0x00
    IRQ,         // Offset + 0x80
    FIQ,         // Offset + 0x100
    SError       // Offset + 0x180
};

const char* exception_origin_string(ExceptionOrigin origin) noexcept;
const char* exception_vector_type_string(ExceptionVectorType type) noexcept;

struct EsrSyndromeInfo {
    std::uint8_t  ec{0};       // Exception Class [31:26]
    bool          il{false};   // Instruction Length [25]
    std::uint32_t iss{0};      // Instruction Specific Syndrome [24:0]
    std::string   description;
};

// -----------------------------------------------------------------------------
// KernelExceptionVectors Manager
// -----------------------------------------------------------------------------
class KernelExceptionVectors {
public:
    static constexpr std::uint64_t kVectorTableAlignment = 2048ULL;
    static constexpr std::size_t   kTotalVectors = 16;
    static constexpr std::size_t   kVectorSlotSize = 0x80; // 128 bytes per slot

    static constexpr std::uint64_t compute_vector_offset(
        ExceptionOrigin origin,
        ExceptionVectorType type) noexcept {

        std::uint64_t base = 0;
        switch (origin) {
            case ExceptionOrigin::CurrentEL_SP0:   base = 0x000; break;
            case ExceptionOrigin::CurrentEL_SPx:   base = 0x200; break;
            case ExceptionOrigin::LowerEL_AArch64: base = 0x400; break;
            case ExceptionOrigin::LowerEL_AArch32: base = 0x600; break;
        }

        switch (type) {
            case ExceptionVectorType::Synchronous: return base + 0x000;
            case ExceptionVectorType::IRQ:         return base + 0x080;
            case ExceptionVectorType::FIQ:         return base + 0x100;
            case ExceptionVectorType::SError:      return base + 0x180;
        }
        return base;
    }

    static bool validate_vector_table_alignment(std::uint64_t vbar_el1, std::string* out_error = nullptr);

    static EsrSyndromeInfo decode_esr(std::uint32_t esr) noexcept;

    // Emits a synthetic freestanding vector table consisting of basic return/hang stubs
    static std::vector<std::uint32_t> build_default_vector_table();
};

} // namespace winios::cpu

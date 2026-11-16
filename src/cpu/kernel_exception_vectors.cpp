#include "kernel_exception_vectors.h"

#include <iomanip>
#include <sstream>

namespace winios::cpu {

const char* exception_origin_string(ExceptionOrigin origin) noexcept {
    switch (origin) {
        case ExceptionOrigin::CurrentEL_SP0:   return "CurrentEL_SP0";
        case ExceptionOrigin::CurrentEL_SPx:   return "CurrentEL_SPx";
        case ExceptionOrigin::LowerEL_AArch64: return "LowerEL_AArch64";
        case ExceptionOrigin::LowerEL_AArch32: return "LowerEL_AArch32";
    }
    return "UnknownOrigin";
}

const char* exception_vector_type_string(ExceptionVectorType type) noexcept {
    switch (type) {
        case ExceptionVectorType::Synchronous: return "Synchronous";
        case ExceptionVectorType::IRQ:         return "IRQ";
        case ExceptionVectorType::FIQ:         return "FIQ";
        case ExceptionVectorType::SError:      return "SError";
    }
    return "UnknownType";
}

bool KernelExceptionVectors::validate_vector_table_alignment(
    std::uint64_t vbar_el1,
    std::string* out_error) {

    if ((vbar_el1 & (kVectorTableAlignment - 1)) != 0) {
        if (out_error) {
            std::ostringstream oss;
            oss << "VBAR_EL1 base address 0x" << std::hex << vbar_el1
                << " is not aligned to " << std::dec << kVectorTableAlignment
                << " bytes (misaligned by " << (vbar_el1 & (kVectorTableAlignment - 1)) << " bytes)";
            *out_error = oss.str();
        }
        return false;
    }
    return true;
}

EsrSyndromeInfo KernelExceptionVectors::decode_esr(std::uint32_t esr) noexcept {
    EsrSyndromeInfo info;
    info.ec = static_cast<std::uint8_t>((esr >> 26) & 0x3F);
    info.il = ((esr >> 25) & 1) != 0;
    info.iss = esr & 0x01FFFFFF;

    switch (info.ec) {
        case 0x00: info.description = "Unknown reason"; break;
        case 0x01: info.description = "Trapped WFI or WFE instruction execution"; break;
        case 0x03: info.description = "Trapped MCR or MRC access (CP15)"; break;
        case 0x04: info.description = "Trapped MCRR or MRRC access (CP15)"; break;
        case 0x05: info.description = "Trapped MCR or MRC access (CP14)"; break;
        case 0x06: info.description = "Trapped LDC or STC access"; break;
        case 0x07: info.description = "Trapped access to SVE, Advanced SIMD, or FP functionality"; break;
        case 0x0E: info.description = "Illegal Execution state"; break;
        case 0x11: info.description = "SVC instruction execution in AArch32 state"; break;
        case 0x12: info.description = "HVC instruction execution in AArch32 state"; break;
        case 0x13: info.description = "SMC instruction execution in AArch32 state"; break;
        case 0x15: info.description = "SVC instruction execution in AArch64 state"; break;
        case 0x16: info.description = "HVC instruction execution in AArch64 state"; break;
        case 0x17: info.description = "SMC instruction execution in AArch64 state"; break;
        case 0x18: info.description = "Trapped MSR, MRS, or System instruction execution"; break;
        case 0x19: info.description = "Trapped access to SVE functionality"; break;
        case 0x20: info.description = "Instruction Abort from a lower Exception level"; break;
        case 0x21: info.description = "Instruction Abort taken without a change in Exception level"; break;
        case 0x22: info.description = "PC alignment fault"; break;
        case 0x24: info.description = "Data Abort from a lower Exception level"; break;
        case 0x25: info.description = "Data Abort taken without a change in Exception level"; break;
        case 0x26: info.description = "SP alignment fault"; break;
        case 0x28: info.description = "Trapped floating-point exception taken from AArch32 state"; break;
        case 0x2C: info.description = "Trapped floating-point exception taken from AArch64 state"; break;
        case 0x2F: info.description = "SError interrupt"; break;
        case 0x30: info.description = "Breakpoint exception from a lower Exception level"; break;
        case 0x31: info.description = "Breakpoint exception taken without a change in Exception level"; break;
        case 0x32: info.description = "Software Step exception from a lower Exception level"; break;
        case 0x33: info.description = "Software Step exception taken without a change in Exception level"; break;
        case 0x34: info.description = "Watchpoint exception from a lower Exception level"; break;
        case 0x35: info.description = "Watchpoint exception taken without a change in Exception level"; break;
        case 0x38: info.description = "BKPT instruction execution in AArch32 state"; break;
        case 0x3C: info.description = "BRK instruction execution in AArch64 state"; break;
        default: {
            std::ostringstream oss;
            oss << "Unrecognized Exception Class 0x" << std::hex << static_cast<int>(info.ec);
            info.description = oss.str();
            break;
        }
    }
    return info;
}

std::vector<std::uint32_t> KernelExceptionVectors::build_default_vector_table() {
    constexpr std::size_t words_per_slot = kVectorSlotSize / sizeof(std::uint32_t); // 32 words
    constexpr std::size_t total_words = kTotalVectors * words_per_slot;             // 512 words = 2048 bytes
    std::vector<std::uint32_t> table(total_words, 0xD503201FU);                     // fill with NOP (0xD503201F)

    for (std::size_t vec = 0; vec < kTotalVectors; ++vec) {
        std::size_t slot_offset = vec * words_per_slot;
        // Instruction 0: NOP (0xD503201F)
        table[slot_offset + 0] = 0xD503201FU;
        // Instruction 1: ERET (0xD69F03E0)
        table[slot_offset + 1] = 0xD69F03E0U;
    }
    return table;
}

} // namespace winios::cpu

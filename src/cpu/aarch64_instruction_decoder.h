#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <sstream>
#include <iomanip>

namespace winios::cpu {

enum class AddressingMode {
    None,
    ImmediateUnsignedOffset,
    ImmediateSignedOffset,
    ImmediatePreIndexed,
    ImmediatePostIndexed,
    RegisterOffset
};

struct DecodedInstruction {
    std::uint32_t   word{0};
    std::string     mnemonic{"UNKNOWN"};
    int             register_width{64}; // 32 or 64
    std::uint32_t   rt{0};              // Target register (0..31, 31 can be SP or ZR)
    std::uint32_t   rt2{0};             // Second target register for STP/LDP
    std::uint32_t   rn{0};              // Base register (31 = SP)
    std::int64_t    immediate_offset{0};// Byte offset
    AddressingMode  addressing_mode{AddressingMode::None};
    bool            has_writeback{false};
    std::string     sysreg_name;
    bool            is_valid{false};
    std::string     disassembly;

    [[nodiscard]] bool is_sp_base() const noexcept {
        return rn == 31;
    }
};

class AArch64InstructionDecoder {
public:
    static DecodedInstruction decode(std::uint32_t word) {
        DecodedInstruction d;
        d.word = word;

        // 1. ERET (0xD69F03E0)
        if (word == 0xD69F03E0u) {
            d.mnemonic = "eret";
            d.is_valid = true;
            d.disassembly = "eret";
            return d;
        }

        // 1b. NOP (0xD503201F)
        if (word == 0xD503201Fu) {
            d.mnemonic = "nop";
            d.is_valid = true;
            d.disassembly = "nop";
            return d;
        }

        // 1c. BL (Branch with link, immediate): 0x94000000 .. 0x97FFFFFF
        if ((word & 0xFC000000u) == 0x94000000u) {
            d.mnemonic = "bl";
            std::int32_t imm26 = static_cast<std::int32_t>(word & 0x03FFFFFFu);
            if (imm26 & 0x02000000) imm26 |= ~0x03FFFFFF; // sign extend 26-bit
            d.immediate_offset = static_cast<std::int64_t>(imm26) * 4;
            d.is_valid = true;
            d.disassembly = "bl #" + std::to_string(d.immediate_offset);
            return d;
        }

        // 1d. B (Branch, immediate): 0x14000000 .. 0x17FFFFFF
        if ((word & 0xFC000000u) == 0x14000000u) {
            d.mnemonic = "b";
            std::int32_t imm26 = static_cast<std::int32_t>(word & 0x03FFFFFFu);
            if (imm26 & 0x02000000) imm26 |= ~0x03FFFFFF; // sign extend 26-bit
            d.immediate_offset = static_cast<std::int64_t>(imm26) * 4;
            d.is_valid = true;
            d.disassembly = "b #" + std::to_string(d.immediate_offset);
            return d;
        }

        // 2. SUB (immediate, 64-bit): 0xD1000000 .. 0xD13FFFFF
        // sf=1, op=0, S=0, 100010, sh, imm12, Rn, Rd
        if ((word & 0xFF800000u) == 0xD1000000u) {
            d.mnemonic = "sub";
            d.register_width = 64;
            d.rn = (word >> 5) & 0x1Fu;
            d.rt = word & 0x1Fu;
            std::uint32_t shift = (word >> 22) & 1u;
            std::uint32_t imm12 = (word >> 10) & 0xFFFu;
            d.immediate_offset = static_cast<std::int64_t>(imm12 << (shift ? 12u : 0u));
            d.is_valid = true;
            std::string rd_str = (d.rt == 31) ? "sp" : ("x" + std::to_string(d.rt));
            std::string rn_str = (d.rn == 31) ? "sp" : ("x" + std::to_string(d.rn));
            d.disassembly = "sub " + rd_str + ", " + rn_str + ", #" + std::to_string(d.immediate_offset);
            return d;
        }

        // 3. ADD (immediate, 64-bit): 0x91000000 .. 0x913FFFFF
        if ((word & 0xFF800000u) == 0x91000000u) {
            d.mnemonic = "add";
            d.register_width = 64;
            d.rn = (word >> 5) & 0x1Fu;
            d.rt = word & 0x1Fu;
            std::uint32_t shift = (word >> 22) & 1u;
            std::uint32_t imm12 = (word >> 10) & 0xFFFu;
            d.immediate_offset = static_cast<std::int64_t>(imm12 << (shift ? 12u : 0u));
            d.is_valid = true;
            std::string rd_str = (d.rt == 31) ? "sp" : ("x" + std::to_string(d.rt));
            std::string rn_str = (d.rn == 31) ? "sp" : ("x" + std::to_string(d.rn));
            d.disassembly = "add " + rd_str + ", " + rn_str + ", #" + std::to_string(d.immediate_offset);
            return d;
        }

        // 4. STP (64-bit, signed offset): 0xA9000000 .. 0xA93FFFFF
        // opc=10, 101, 0, 010, L=0, imm7, Rt2, Rn, Rt1
        if ((word & 0xFFC00000u) == 0xA9000000u) {
            d.mnemonic = "stp";
            d.register_width = 64;
            d.rt = word & 0x1Fu;
            d.rn = (word >> 5) & 0x1Fu;
            d.rt2 = (word >> 10) & 0x1Fu;
            std::int32_t imm7 = static_cast<std::int32_t>((word >> 15) & 0x7Fu);
            if (imm7 & 0x40) imm7 |= ~0x7F; // sign extend
            d.immediate_offset = static_cast<std::int64_t>(imm7) * 8;
            d.addressing_mode = AddressingMode::ImmediateSignedOffset;
            d.is_valid = true;
            std::string rn_str = (d.rn == 31) ? "sp" : ("x" + std::to_string(d.rn));
            d.disassembly = "stp x" + std::to_string(d.rt) + ", x" + std::to_string(d.rt2) +
                            ", [" + rn_str + ", #" + std::to_string(d.immediate_offset) + "]";
            return d;
        }

        // 5. LDP (64-bit, signed offset): 0xA9400000 .. 0xA97FFFFF
        // opc=10, 101, 0, 010, L=1, imm7, Rt2, Rn, Rt1
        if ((word & 0xFFC00000u) == 0xA9400000u) {
            d.mnemonic = "ldp";
            d.register_width = 64;
            d.rt = word & 0x1Fu;
            d.rn = (word >> 5) & 0x1Fu;
            d.rt2 = (word >> 10) & 0x1Fu;
            std::int32_t imm7 = static_cast<std::int32_t>((word >> 15) & 0x7Fu);
            if (imm7 & 0x40) imm7 |= ~0x7F; // sign extend
            d.immediate_offset = static_cast<std::int64_t>(imm7) * 8;
            d.addressing_mode = AddressingMode::ImmediateSignedOffset;
            d.is_valid = true;
            std::string rn_str = (d.rn == 31) ? "sp" : ("x" + std::to_string(d.rn));
            d.disassembly = "ldp x" + std::to_string(d.rt) + ", x" + std::to_string(d.rt2) +
                            ", [" + rn_str + ", #" + std::to_string(d.immediate_offset) + "]";
            return d;
        }

        // 6. STR (32-bit, unsigned immediate offset): 0xB9000000 .. 0xB93FFFFF
        // size=10, 111001, opc=00, imm12, Rn, Rt
        if ((word & 0xFFC00000u) == 0xB9000000u) {
            d.mnemonic = "str";
            d.register_width = 32;
            d.rt = word & 0x1Fu;
            d.rn = (word >> 5) & 0x1Fu;
            std::uint32_t imm12 = (word >> 10) & 0xFFFu;
            d.immediate_offset = static_cast<std::int64_t>(imm12 * 4u);
            d.addressing_mode = AddressingMode::ImmediateUnsignedOffset;
            d.is_valid = true;
            std::string rt_str = (d.rt == 31) ? "wzr" : ("w" + std::to_string(d.rt));
            std::string rn_str = (d.rn == 31) ? "sp" : ("x" + std::to_string(d.rn));
            d.disassembly = "str " + rt_str + ", [" + rn_str + ", #" + std::to_string(d.immediate_offset) + "]";
            return d;
        }

        // 7. LDR (32-bit, unsigned immediate offset): 0xB9400000 .. 0xB97FFFFF
        // size=10, 111001, opc=01, imm12, Rn, Rt
        if ((word & 0xFFC00000u) == 0xB9400000u) {
            d.mnemonic = "ldr";
            d.register_width = 32;
            d.rt = word & 0x1Fu;
            d.rn = (word >> 5) & 0x1Fu;
            std::uint32_t imm12 = (word >> 10) & 0xFFFu;
            d.immediate_offset = static_cast<std::int64_t>(imm12 * 4u);
            d.addressing_mode = AddressingMode::ImmediateUnsignedOffset;
            d.is_valid = true;
            std::string rt_str = (d.rt == 31) ? "wzr" : ("w" + std::to_string(d.rt));
            std::string rn_str = (d.rn == 31) ? "sp" : ("x" + std::to_string(d.rn));
            d.disassembly = "ldr " + rt_str + ", [" + rn_str + ", #" + std::to_string(d.immediate_offset) + "]";
            return d;
        }

        // 8. STR (64-bit, unsigned immediate offset): 0xF9000000 .. 0xF93FFFFF
        // size=11, 111001, opc=00, imm12, Rn, Rt
        if ((word & 0xFFC00000u) == 0xF9000000u) {
            d.mnemonic = "str";
            d.register_width = 64;
            d.rt = word & 0x1Fu;
            d.rn = (word >> 5) & 0x1Fu;
            std::uint32_t imm12 = (word >> 10) & 0xFFFu;
            d.immediate_offset = static_cast<std::int64_t>(imm12 * 8u);
            d.addressing_mode = AddressingMode::ImmediateUnsignedOffset;
            d.is_valid = true;
            std::string rt_str = (d.rt == 31) ? "xzr" : ("x" + std::to_string(d.rt));
            std::string rn_str = (d.rn == 31) ? "sp" : ("x" + std::to_string(d.rn));
            d.disassembly = "str " + rt_str + ", [" + rn_str + ", #" + std::to_string(d.immediate_offset) + "]";
            return d;
        }

        // 9. LDR (64-bit, unsigned immediate offset): 0xF9400000 .. 0xF97FFFFF
        // size=11, 111001, opc=01, imm12, Rn, Rt
        if ((word & 0xFFC00000u) == 0xF9400000u) {
            d.mnemonic = "ldr";
            d.register_width = 64;
            d.rt = word & 0x1Fu;
            d.rn = (word >> 5) & 0x1Fu;
            std::uint32_t imm12 = (word >> 10) & 0xFFFu;
            d.immediate_offset = static_cast<std::int64_t>(imm12 * 8u);
            d.addressing_mode = AddressingMode::ImmediateUnsignedOffset;
            d.is_valid = true;
            std::string rt_str = (d.rt == 31) ? "xzr" : ("x" + std::to_string(d.rt));
            std::string rn_str = (d.rn == 31) ? "sp" : ("x" + std::to_string(d.rn));
            d.disassembly = "ldr " + rt_str + ", [" + rn_str + ", #" + std::to_string(d.immediate_offset) + "]";
            return d;
        }

        // 10. MRS (System register to register): 0xD5300000 .. 0xD53FFFFF
        // 1101 0101 0011, op0(2), op1(3), CRn(4), CRm(4), op2(3), Rt(5)
        if ((word & 0xFFF00000u) == 0xD5300000u) {
            d.mnemonic = "mrs";
            d.register_width = 64;
            d.rt = word & 0x1Fu;
            std::uint32_t sysreg_enc = (word >> 5) & 0x7FFFu; // 15 bits of sysreg
            d.sysreg_name = decode_sysreg(sysreg_enc);
            d.is_valid = true;
            d.disassembly = "mrs x" + std::to_string(d.rt) + ", " + d.sysreg_name;
            return d;
        }

        // 11. MSR (Register to system register): 0xD5100000 .. 0xD51FFFFF
        if ((word & 0xFFF00000u) == 0xD5100000u) {
            d.mnemonic = "msr";
            d.register_width = 64;
            d.rt = word & 0x1Fu;
            std::uint32_t sysreg_enc = (word >> 5) & 0x7FFFu;
            d.sysreg_name = decode_sysreg(sysreg_enc);
            d.is_valid = true;
            d.disassembly = "msr " + d.sysreg_name + ", x" + std::to_string(d.rt);
            return d;
        }

        // 12. MOVZ (32-bit): 0x52800000 .. 0x529FFFFF
        if ((word & 0xFF800000u) == 0x52800000u) {
            d.mnemonic = "movz";
            d.register_width = 32;
            d.rt = word & 0x1Fu;
            std::uint32_t imm16 = (word >> 5) & 0xFFFFu;
            d.immediate_offset = static_cast<std::int64_t>(imm16);
            d.is_valid = true;
            d.disassembly = "mov w" + std::to_string(d.rt) + ", #" + std::to_string(imm16);
            return d;
        }

        // Fallback for unhandled words
        std::ostringstream oss;
        oss << "unhandled (0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << word << ")";
        d.disassembly = oss.str();
        return d;
    }

private:
    static std::string decode_sysreg(std::uint32_t enc) {
        // enc is bits [19:5] shifted:
        // op0(2)=bits 14:13, op1(3)=bits 12:10, CRn(4)=bits 9:6, CRm(4)=bits 5:2, op2(3)=bits 1:0 (wait, op2 is 3 bits!)
        // In ARM64:
        // bit 20: op0 bit 1
        // bit 19: op0 bit 0
        // bit 18..16: op1 (3 bits)
        // bit 15..12: CRn (4 bits)
        // bit 11..8: CRm (4 bits)
        // bit 7..5: op2 (3 bits)
        // If enc = (word >> 5) & 0x7FFFu:
        // bits 14..0 correspond to bits 19..5 of the word:
        // bits 14..13: op1 (wait, bit 20 was 0xD53..., bit 20=1, bit 19=0, so op0=3)
        // Let's decode known system registers directly by (word & 0x001FFFE0u):
        std::uint32_t sysreg_mask = enc << 5u;
        switch (sysreg_mask) {
            case 0x00084100u: return "sp_el0";
            case 0x00084020u: return "elr_el1";
            case 0x00084000u: return "spsr_el1";
            case 0x00085200u: return "esr_el1";
            case 0x00086000u: return "far_el1";
            case 0x00081000u: return "sctlr_el1";
            case 0x00082000u: return "ttbr0_el1";
            case 0x00082020u: return "ttbr1_el1";
            case 0x00082040u: return "tcr_el1";
            case 0x00087000u: return "par_el1";
            case 0x0008C000u: return "vbar_el1";
            default: {
                std::ostringstream oss;
                oss << "sysreg_0x" << std::hex << sysreg_mask;
                return oss.str();
            }
        }
    }
};

} // namespace winios::cpu

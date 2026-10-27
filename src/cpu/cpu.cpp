#include "cpu.h"
#include "memory/memory.h"

#include <iostream>
#include <limits>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>

namespace {
constexpr std::uint32_t kMovzMask = 0xFF800000u;
constexpr std::uint32_t kMovz64 = 0xD2800000u;
constexpr std::uint32_t kMovn64 = 0x92800000u;
constexpr std::uint32_t kMovk64 = 0xF2800000u;
constexpr std::uint32_t kAddImmediateMask = 0xFF800000u;
constexpr std::uint32_t kAddImmediate64 = 0x91000000u;
constexpr std::uint32_t kSubImmediate64 = 0xD1000000u;
constexpr std::uint32_t kLogicalRegisterMask = 0xFF200000u;
constexpr std::uint32_t kOrr64 = 0xAA000000u;
constexpr std::uint32_t kLoadStoreUnsignedMask = 0xFFC00000u;
constexpr std::uint32_t kLdr64 = 0xF9400000u;
constexpr std::uint32_t kStr64 = 0xF9000000u;
constexpr std::uint32_t kBranchMask = 0xFC000000u;
constexpr std::uint32_t kBranch = 0x14000000u;
constexpr std::uint32_t kConditionalBranchMask = 0xFF000010u;
constexpr std::uint32_t kConditionalBranch = 0x54000000u;
constexpr std::uint32_t kCompareBranchMask = 0x7F000000u;
constexpr std::uint32_t kCbz = 0x34000000u;
constexpr std::uint32_t kAddSubImmediateMask = 0x1F800000u;
constexpr std::uint32_t kAddImmediate = 0x11000000u;
constexpr std::uint32_t kAddSubRegisterMask = 0x1F200000u;
constexpr std::uint32_t kAddSubRegister = 0x0B000000u;
constexpr std::uint32_t kAndsRegisterMask = 0x1F200000u;
constexpr std::uint32_t kAndsRegister = 0x0A000000u;

constexpr std::uint8_t kNegative = 0x8u;
constexpr std::uint8_t kZero = 0x4u;
constexpr std::uint8_t kCarry = 0x2u;
constexpr std::uint8_t kOverflow = 0x1u;

std::string register_index_message(std::size_t index) {
    return "Register index out of range: " + std::to_string(index)
           + " (valid range is 0.." + std::to_string(CPU::kRegisterCount - 1) + ")";
}

std::size_t pc_as_memory_offset(std::uint64_t pc) {
    if (pc > std::numeric_limits<std::size_t>::max()) {
        throw std::out_of_range("Instruction fetch address exceeds host memory offset range");
    }
    return static_cast<std::size_t>(pc);
}

std::uint64_t checked_add_address(std::uint64_t address,
                                  std::uint64_t displacement,
                                  const char* description) {
    if (displacement > std::numeric_limits<std::uint64_t>::max() - address) {
        throw std::out_of_range(std::string(description) + " overflows guest address space");
    }
    return address + displacement;
}

std::uint64_t checked_sub_address(std::uint64_t address,
                                  std::uint64_t displacement,
                                  const char* description) {
    if (displacement > address) {
        throw std::out_of_range(std::string(description) + " underflows guest address space");
    }
    return address - displacement;
}

std::uint64_t read_add_operand(const CPU& cpu, std::uint32_t register_index) {
    return register_index == CPU::kRegisterCount
               ? cpu.get_stack_pointer()
               : cpu.read_register(register_index);
}

std::uint64_t read_zero_register_operand(const CPU& cpu,
                                         std::uint32_t register_index) {
    return register_index == CPU::kRegisterCount
               ? 0
               : cpu.read_register(register_index);
}

std::uint64_t read_stack_pointer_operand(const CPU& cpu,
                                         std::uint32_t register_index) {
    return register_index == CPU::kRegisterCount
               ? cpu.get_stack_pointer()
               : cpu.read_register(register_index);
}

std::uint64_t width_mask(bool is64) {
    return is64 ? std::numeric_limits<std::uint64_t>::max() : 0xFFFFFFFFull;
}

std::uint64_t read_width_operand(const CPU& cpu, std::uint32_t index,
                                 bool is64, bool stack_pointer_allowed) {
    const std::uint64_t value = index == CPU::kRegisterCount
        ? (stack_pointer_allowed ? cpu.get_stack_pointer() : 0)
        : cpu.read_register(index);
    return value & width_mask(is64);
}

void write_width_result(CPU& cpu, std::uint32_t index, std::uint64_t value,
                        bool is64, bool stack_pointer_allowed) {
    value &= width_mask(is64);
    if (index == CPU::kRegisterCount) {
        if (stack_pointer_allowed) {
            cpu.set_stack_pointer(value);
        }
    } else {
        cpu.write_register(index, value);
    }
}

std::uint8_t arithmetic_flags(std::uint64_t left, std::uint64_t right,
                              std::uint64_t result, bool subtract, bool is64) {
    const std::uint64_t mask = width_mask(is64);
    const std::uint64_t sign = is64 ? (1ull << 63u) : (1ull << 31u);
    left &= mask;
    right &= mask;
    result &= mask;
    std::uint8_t flags = (result & sign) != 0 ? kNegative : 0;
    if (result == 0) flags |= kZero;
    if (subtract ? left >= right : result < left) flags |= kCarry;
    const bool overflow = subtract
        ? (((left ^ right) & (left ^ result) & sign) != 0)
        : (((~(left ^ right)) & (left ^ result) & sign) != 0);
    if (overflow) flags |= kOverflow;
    return flags;
}

bool condition_holds(std::uint32_t condition, std::uint8_t nzcv) {
    const bool n = (nzcv & kNegative) != 0;
    const bool z = (nzcv & kZero) != 0;
    const bool c = (nzcv & kCarry) != 0;
    const bool v = (nzcv & kOverflow) != 0;
    switch (condition & 0xFu) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return c;
    case 0x3: return !c;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return c && !z;
    case 0x9: return !c || z;
    case 0xA: return n == v;
    case 0xB: return n != v;
    case 0xC: return !z && (n == v);
    case 0xD: return z || (n != v);
    case 0xE: return true;
    default: return false;
    }
}

void write_add_result(CPU& cpu, std::uint32_t register_index, std::uint64_t value) {
    if (register_index == CPU::kRegisterCount) {
        cpu.set_stack_pointer(value);
    } else {
        cpu.write_register(register_index, value);
    }
}

void write_zero_register_result(CPU& cpu, std::uint32_t register_index,
                                std::uint64_t value) {
    if (register_index != CPU::kRegisterCount) {
        cpu.write_register(register_index, value);
    }
}

std::string register_name(std::uint32_t index, bool stack_pointer_allowed) {
    if (index == CPU::kRegisterCount) {
        return stack_pointer_allowed ? "SP" : "XZR";
    }
    return "X" + std::to_string(index);
}

std::string hex_word(std::uint32_t word) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << word;
    return stream.str();
}

std::string decode_disassembly(std::uint32_t instruction) {
    const std::uint32_t wide_opcode = instruction & 0x7F800000u;
    if (wide_opcode == 0x12800000u
        || wide_opcode == 0x72800000u
        || wide_opcode == 0x52800000u) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const auto destination = instruction & 0x1Fu;
        const auto halfword = (instruction >> 21u) & 0x3u;
        const auto immediate = (instruction >> 5u) & 0xFFFFu;
        const char* mnemonic = wide_opcode == 0x12800000u
            ? "MOVN" : wide_opcode == 0x72800000u ? "MOVK" : "MOVZ";
        const std::string destination_name =
            destination == CPU::kRegisterCount
                ? (is64 ? "XZR" : "WZR")
                : (std::string(is64 ? "X" : "W") + std::to_string(destination));
        std::ostringstream stream;
        stream << mnemonic << ' ' << destination_name << ", #" << immediate;
        if (halfword != 0) stream << ", LSL #" << (halfword * 16u);
        return stream.str();
    }
    if ((instruction & kMovzMask) == kMovn64
        || (instruction & kMovzMask) == kMovk64) {
        const auto destination = instruction & 0x1Fu;
        const auto halfword = (instruction >> 21u) & 0x3u;
        const auto immediate = (instruction >> 5u) & 0xFFFFu;
        const bool keep = (instruction & kMovzMask) == kMovk64;
        std::ostringstream stream;
        stream << (keep ? "MOVK " : "MOVN ")
               << register_name(destination, false) << ", #" << immediate;
        if (halfword != 0) {
            stream << ", LSL #" << (halfword * 16u);
        }
        return stream.str();
    }
    if ((instruction & kMovzMask) == kMovz64) {
        const auto destination = instruction & 0x1Fu;
        const auto halfword = (instruction >> 21u) & 0x3u;
        const auto immediate = (instruction >> 5u) & 0xFFFFu;
        std::ostringstream stream;
        stream << "MOVZ " << register_name(destination, false) << ", #"
               << immediate;
        if (halfword != 0) {
            stream << ", LSL #" << (halfword * 16u);
        }
        return stream.str();
    }
    if ((instruction & 0x7F200000u) == 0x2A000000u) {
        const auto destination = instruction & 0x1Fu;
        const auto source = (instruction >> 5u) & 0x1Fu;
        const auto shifted = (instruction >> 16u) & 0x1Fu;
        const auto shift_type = (instruction >> 22u) & 0x3u;
        const auto amount = (instruction >> 10u) & 0x3Fu;
        static constexpr const char* names[] = {"LSL", "LSR", "ASR", "RESERVED"};
        std::ostringstream stream;
        stream << "ORR " << register_name(destination, false) << ", "
               << register_name(source, false) << ", "
               << register_name(shifted, false);
        if (amount != 0 || shift_type != 0) {
            stream << ", " << names[shift_type] << " #" << amount;
        }
        return stream.str();
    }
    if ((instruction & kAddSubRegisterMask) == kAddSubRegister) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool subtract = (instruction & 0x40000000u) != 0;
        const bool set_flags = (instruction & 0x20000000u) != 0;
        const auto destination = instruction & 0x1Fu;
        const auto source = (instruction >> 5u) & 0x1Fu;
        const auto shifted = (instruction >> 16u) & 0x1Fu;
        const auto amount = (instruction >> 10u) & 0x3Fu;
        std::ostringstream stream;
        stream << (set_flags ? (subtract ? "SUBS " : "ADDS ")
                             : (subtract ? "SUB " : "ADD "))
               << (is64 ? "X" : "W") << destination << ", "
               << (is64 ? "X" : "W") << source << ", "
               << (is64 ? "X" : "W") << shifted;
        if (amount != 0) stream << ", LSL #" << amount;
        return stream.str();
    }
    if ((instruction & kConditionalBranchMask) == kConditionalBranch) {
        static constexpr const char* conditions[] = {
            "EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC",
            "HI", "LS", "GE", "LT", "GT", "LE", "AL", "NV"};
        const auto encoded = (instruction >> 5u) & 0x7FFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded);
        if ((encoded & 0x40000u) != 0) offset -= (std::int64_t{1} << 19u);
        std::ostringstream stream;
        stream << "B." << conditions[instruction & 0xFu] << " " << offset * 4;
        return stream.str();
    }
    if ((instruction & kCompareBranchMask) == kCbz
        || (instruction & kCompareBranchMask) == (kCbz | 0x01000000u)) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool nonzero = (instruction & 0x01000000u) != 0;
        const auto source = instruction & 0x1Fu;
        const auto encoded = (instruction >> 5u) & 0x7FFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded);
        if ((encoded & 0x40000u) != 0) offset -= (std::int64_t{1} << 19u);
        std::ostringstream stream;
        stream << (nonzero ? "CBNZ " : "CBZ ") << (is64 ? "X" : "W")
               << source << ", " << offset * 4;
        return stream.str();
    }
    if ((instruction & kLoadStoreUnsignedMask) == kLdr64
        || (instruction & kLoadStoreUnsignedMask) == kStr64) {
        const auto target = instruction & 0x1Fu;
        const auto base = (instruction >> 5u) & 0x1Fu;
        const auto offset = (instruction >> 10u) & 0xFFFu;
        std::ostringstream stream;
        stream << (((instruction & kLoadStoreUnsignedMask) == kLdr64)
                       ? "LDR " : "STR ")
               << register_name(target, false) << ", ["
               << register_name(base, true) << ", #" << (offset * 8u) << "]";
        return stream.str();
    }
    if ((instruction & kAddImmediateMask) == kAddImmediate64) {
        const auto destination = instruction & 0x1Fu;
        const auto source = (instruction >> 5u) & 0x1Fu;
        const auto shift = (instruction >> 22u) & 0x1u;
        const auto immediate = (instruction >> 10u) & 0xFFFu;
        std::ostringstream stream;
        stream << "ADD " << register_name(destination, true) << ", "
               << register_name(source, true) << ", #" << immediate;
        if (shift != 0) {
            stream << ", LSL #12";
        }
        return stream.str();
    }
    if ((instruction & kAddImmediateMask) == kSubImmediate64) {
        const auto destination = instruction & 0x1Fu;
        const auto source = (instruction >> 5u) & 0x1Fu;
        const auto shift = (instruction >> 22u) & 0x1u;
        const auto immediate = (instruction >> 10u) & 0xFFFu;
        std::ostringstream stream;
        stream << "SUB " << register_name(destination, true) << ", "
               << register_name(source, true) << ", #" << immediate;
        if (shift != 0) {
            stream << ", LSL #12";
        }
        return stream.str();
    }
    if ((instruction & kBranchMask) == kBranch) {
        const auto encoded_offset = instruction & 0x03FFFFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded_offset);
        if ((encoded_offset & 0x02000000u) != 0) {
            offset -= (std::int64_t{1} << 26u);
        }
        std::ostringstream stream;
        stream << "B " << (offset * 4);
        return stream.str();
    }
    return "UNKNOWN " + hex_word(instruction);
}

std::vector<std::string> changed_registers(const CPU& cpu,
                                           const std::uint64_t* before,
                                           std::uint64_t before_sp) {
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        if (cpu.read_register(i) != before[i]) {
            changed.push_back("X" + std::to_string(i));
        }
    }
    if (cpu.get_stack_pointer() != before_sp) {
        changed.push_back("SP");
    }
    return changed;
}
} // namespace

CPU::CPU() {
    reset();
}

void CPU::reset() {
    for (auto& reg : registers_) {
        reg = 0;
    }
    stack_pointer_ = 0;
    program_counter_ = 0;
    nzcv_ = 0;
}

void CPU::initialize_entry(std::uint64_t entry_address, const Memory& memory) {
    if (entry_address % 4u != 0
        || entry_address > memory.size()
        || memory.size() - static_cast<std::size_t>(entry_address) < 4u) {
        throw std::invalid_argument("Invalid guest entry address");
    }
    reset();
    program_counter_ = entry_address;
}

std::uint64_t CPU::read_register(std::size_t index) const {
    if (index >= kRegisterCount) {
        throw std::out_of_range(register_index_message(index));
    }
    return registers_[index];
}

void CPU::write_register(std::size_t index, std::uint64_t value) {
    if (index >= kRegisterCount) {
        throw std::out_of_range(register_index_message(index));
    }
    registers_[index] = value;
}

void CPU::step(Memory& memory) {
    const std::uint64_t instruction_address = program_counter_;
    const std::uint32_t instruction =
        memory.read_u32(pc_as_memory_offset(instruction_address));
    std::uint64_t before[kRegisterCount]{};
    for (std::size_t i = 0; i < kRegisterCount; ++i) {
        before[i] = registers_[i];
    }
    const std::uint64_t before_sp = stack_pointer_;
    const auto emit_trace = [&] {
        if (trace_sink_) {
            trace_sink_(TraceRecord{instruction_address, instruction,
                                    decode_disassembly(instruction),
                                    program_counter_,
                                    nzcv_,
                                    changed_registers(*this, before, before_sp)});
        }
    };

    const std::uint32_t wide_opcode = instruction & 0x7F800000u;
    if (wide_opcode == 0x12800000u
        || wide_opcode == 0x72800000u
        || wide_opcode == 0x52800000u) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t halfword = (instruction >> 21u) & 0x3u;
        const std::uint64_t immediate = (instruction >> 5u) & 0xFFFFu;
        if (!is64 && halfword >= 2u) {
            throw std::runtime_error("Unsupported 32-bit wide-move shift");
        }
        const std::uint64_t mask = std::uint64_t{0xFFFF} << (halfword * 16u);
        const std::uint64_t shifted = immediate << (halfword * 16u);
        if (wide_opcode == 0x12800000u) {
            write_width_result(*this, destination, ~shifted, is64, false);
        } else if (wide_opcode == 0x72800000u) {
            if (destination != CPU::kRegisterCount) {
                const std::uint64_t current = read_register(destination)
                    & width_mask(is64);
                write_width_result(*this, destination,
                                   (current & ~mask) | shifted, is64, false);
            }
        } else {
            write_width_result(*this, destination, shifted, is64, false);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    if ((instruction & kAddSubImmediateMask) == kAddImmediate) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool subtract = (instruction & 0x40000000u) != 0;
        const bool set_flags = (instruction & 0x20000000u) != 0;
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t source = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t shift = (instruction >> 22u) & 0x1u;
        const std::uint64_t immediate = (instruction >> 10u) & 0xFFFu;
        if (shift > 1u) {
            throw std::runtime_error("Unsupported ADD (immediate) shift encoding");
        }
        const std::uint64_t shifted_immediate = immediate << (shift * 12u);
        const std::uint64_t left = read_width_operand(
            *this, source, is64, true);
        const std::uint64_t right = shifted_immediate & width_mask(is64);
        const std::uint64_t result = subtract ? left - right : left + right;
        if (set_flags) {
            nzcv_ = arithmetic_flags(left, right, result, subtract, is64);
        }
        write_width_result(*this, destination, result, is64, true);
        program_counter_ = checked_add_address(instruction_address, 4u,
                                                "Instruction PC increment");
        emit_trace();
        return;
    }

    if ((instruction & kAddSubRegisterMask) == kAddSubRegister) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool subtract = (instruction & 0x40000000u) != 0;
        const bool set_flags = (instruction & 0x20000000u) != 0;
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t source = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t shifted = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t shift_type = (instruction >> 22u) & 0x3u;
        const std::uint32_t amount = (instruction >> 10u) & 0x3Fu;
        if (shift_type == 3u || amount >= (is64 ? 64u : 32u)) {
            throw std::runtime_error("Unsupported ADD/SUB register shift");
        }
        std::uint64_t right = read_width_operand(
            *this, shifted, is64, false);
        if (shift_type == 0u) {
            right = (amount == 0u) ? right : right << amount;
        } else if (shift_type == 1u) {
            right >>= amount;
        } else {
            const std::uint64_t sign = is64 ? (1ull << 63u) : (1ull << 31u);
            right = (right & sign) == 0 ? right >> amount
                                        : ~(~right >> amount);
        }
        const std::uint64_t left = read_width_operand(
            *this, source, is64, false);
        const std::uint64_t result = subtract ? left - right : left + right;
        if (set_flags) {
            nzcv_ = arithmetic_flags(left, right, result, subtract, is64);
        }
        write_width_result(*this, destination, result, is64, false);
        program_counter_ = checked_add_address(instruction_address, 4u,
                                                "Instruction PC increment");
        emit_trace();
        return;
    }

    if ((instruction & 0x7F200000u) == 0x2A000000u) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t source = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t shifted = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t shift_type = (instruction >> 22u) & 0x3u;
        const std::uint32_t amount = (instruction >> 10u) & 0x3Fu;
        if (shift_type == 3u || amount >= 64u) {
            throw std::runtime_error("Unsupported ORR shift encoding");
        }
        const std::uint64_t value = read_width_operand(
            *this, shifted, is64, false);
        std::uint64_t shifted_value = value;
        if (shift_type == 1u) {
            shifted_value = value >> amount;
        } else if (shift_type == 2u) {
            shifted_value = (value & (std::uint64_t{1} << 63u)) == 0
                                 ? value >> amount
                                 : ~(~value >> amount);
        } else if (amount != 0) {
            shifted_value = value << amount;
        }
        write_width_result(*this, destination,
                           read_width_operand(*this, source, is64, false)
                               | shifted_value,
                           is64, false);
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    if ((instruction & kAndsRegisterMask) == kAndsRegister) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const std::uint32_t destination = instruction & 0x1Fu;
        const std::uint32_t source = (instruction >> 5u) & 0x1Fu;
        const std::uint32_t shifted = (instruction >> 16u) & 0x1Fu;
        const std::uint32_t shift_type = (instruction >> 22u) & 0x3u;
        const std::uint32_t amount = (instruction >> 10u) & 0x3Fu;
        if (shift_type == 3u || amount >= (is64 ? 64u : 32u)) {
            throw std::runtime_error("Unsupported ANDS shift encoding");
        }
        std::uint64_t right = read_width_operand(
            *this, shifted, is64, false);
        if (shift_type == 0u) {
            right = amount == 0u ? right : right << amount;
        } else if (shift_type == 1u) {
            right >>= amount;
        } else {
            const std::uint64_t sign = is64 ? (1ull << 63u) : (1ull << 31u);
            right = (right & sign) == 0 ? right >> amount : ~(~right >> amount);
        }
        const std::uint64_t result = read_width_operand(
            *this, source, is64, false) & right;
        nzcv_ = (result & (is64 ? (1ull << 63u) : (1ull << 31u))) != 0
                    ? kNegative
                    : 0;
        if (result == 0) nzcv_ |= kZero;
        write_width_result(*this, destination, result, is64, false);
        program_counter_ = checked_add_address(instruction_address, 4u,
                                                "Instruction PC increment");
        emit_trace();
        return;
    }

    if ((instruction & kLoadStoreUnsignedMask) == kLdr64
        || (instruction & kLoadStoreUnsignedMask) == kStr64) {
        const bool load = (instruction & kLoadStoreUnsignedMask) == kLdr64;
        const std::uint32_t target = instruction & 0x1Fu;
        const std::uint32_t base = (instruction >> 5u) & 0x1Fu;
        const std::uint64_t offset =
            static_cast<std::uint64_t>((instruction >> 10u) & 0xFFFu) * 8u;
        const std::uint64_t address = checked_add_address(
            read_stack_pointer_operand(*this, base), offset,
            "Load/store effective address");
        const std::size_t memory_address = pc_as_memory_offset(address);
        if (load) {
            const std::uint64_t value = memory.read_u64(memory_address);
            write_zero_register_result(*this, target, value);
        } else {
            const std::uint64_t value = read_zero_register_operand(*this, target);
            memory.write_u64(memory_address, value);
        }
        program_counter_ = checked_add_address(
            instruction_address, 4u, "Instruction PC increment");
        emit_trace();
        return;
    }

    if ((instruction & kConditionalBranchMask) == kConditionalBranch) {
        const std::uint32_t condition = instruction & 0xFu;
        const std::uint32_t encoded = (instruction >> 5u) & 0x7FFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded);
        if ((encoded & 0x40000u) != 0) {
            offset -= (std::int64_t{1} << 19u);
        }
        const std::int64_t displacement = offset * 4;
        if (condition_holds(condition, nzcv_)) {
            if (displacement >= 0) {
                program_counter_ = checked_add_address(
                    instruction_address, static_cast<std::uint64_t>(displacement),
                    "Conditional branch target");
            } else {
                program_counter_ = checked_sub_address(
                    instruction_address, static_cast<std::uint64_t>(-displacement),
                    "Conditional branch target");
            }
        } else {
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
        }
        emit_trace();
        return;
    }

    if ((instruction & kCompareBranchMask) == kCbz
        || (instruction & kCompareBranchMask) == (kCbz | 0x01000000u)) {
        const bool is64 = (instruction & 0x80000000u) != 0;
        const bool nonzero = (instruction & 0x01000000u) != 0;
        const std::uint32_t source = instruction & 0x1Fu;
        const std::uint32_t encoded = (instruction >> 5u) & 0x7FFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded);
        if ((encoded & 0x40000u) != 0) {
            offset -= (std::int64_t{1} << 19u);
        }
        const std::uint64_t value = read_width_operand(
            *this, source, is64, false);
        const bool take = nonzero ? value != 0 : value == 0;
        if (take) {
            const std::int64_t displacement = offset * 4;
            if (displacement >= 0) {
                program_counter_ = checked_add_address(
                    instruction_address, static_cast<std::uint64_t>(displacement),
                    "Compare branch target");
            } else {
                program_counter_ = checked_sub_address(
                    instruction_address, static_cast<std::uint64_t>(-displacement),
                    "Compare branch target");
            }
        } else {
            program_counter_ = checked_add_address(
                instruction_address, 4u, "Instruction PC increment");
        }
        emit_trace();
        return;
    }

    if ((instruction & kBranchMask) == kBranch) {
        const std::uint32_t encoded_offset = instruction & 0x03FFFFFFu;
        std::int64_t offset = static_cast<std::int64_t>(encoded_offset);
        if ((encoded_offset & 0x02000000u) != 0) {
            offset -= (std::int64_t{1} << 26u);
        }
        offset *= 4;
        if (offset >= 0) {
            program_counter_ = checked_add_address(
                instruction_address, static_cast<std::uint64_t>(offset),
                "Branch target");
        } else {
            program_counter_ = checked_sub_address(
                instruction_address, static_cast<std::uint64_t>(-offset),
                "Branch target");
        }
        emit_trace();
        return;
    }

    throw std::runtime_error("Unsupported AArch64 instruction encoding: 0x"
                             + std::to_string(instruction));
}

std::size_t CPU::run(Memory& memory, std::size_t max_instructions) {
    std::size_t executed = 0;
    while (executed < max_instructions) {
        step(memory);
        ++executed;
    }
    return executed;
}

void CPU::run_demo() {
    write_register(0, 10);
    write_register(1, 20);
    write_register(2, read_register(0) + read_register(1));

    std::cout << "Demo calculation: X2 = "
              << read_register(2) << '\n';

    program_counter_ = 0;

    std::cout << "CPU state demonstration complete.\n";
}

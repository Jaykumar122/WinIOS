// =============================================================================
// WinIOS Phase 52 Stage 34: Independent Reference Execution Model Implementation
// =============================================================================

#include "reference/independent_reference_model.h"

#include <cstring>
#include <sstream>
#include <iomanip>

namespace winios::reference {

// -----------------------------------------------------------------------------
// Reference Memory Implementation
// -----------------------------------------------------------------------------
ReferenceMemory::ReferenceMemory(std::size_t size_bytes)
    : ram_(size_bytes, 0) {}

std::uint8_t ReferenceMemory::read_u8(std::uint64_t paddr) const {
    if (paddr >= ram_.size()) return 0;
    return ram_[static_cast<std::size_t>(paddr)];
}

std::uint16_t ReferenceMemory::read_u16(std::uint64_t paddr) const {
    if (paddr + 2 > ram_.size()) return 0;
    std::uint16_t val = 0;
    std::memcpy(&val, &ram_[static_cast<std::size_t>(paddr)], 2);
    return val;
}

std::uint32_t ReferenceMemory::read_u32(std::uint64_t paddr) const {
    if (paddr + 4 > ram_.size()) return 0;
    std::uint32_t val = 0;
    std::memcpy(&val, &ram_[static_cast<std::size_t>(paddr)], 4);
    return val;
}

std::uint64_t ReferenceMemory::read_u64(std::uint64_t paddr) const {
    if (paddr + 8 > ram_.size()) return 0;
    std::uint64_t val = 0;
    std::memcpy(&val, &ram_[static_cast<std::size_t>(paddr)], 8);
    return val;
}

void ReferenceMemory::write_u8(std::uint64_t paddr, std::uint8_t val) {
    if (paddr < ram_.size()) {
        ram_[static_cast<std::size_t>(paddr)] = val;
    }
}

void ReferenceMemory::write_u16(std::uint64_t paddr, std::uint16_t val) {
    if (paddr + 2 <= ram_.size()) {
        std::memcpy(&ram_[static_cast<std::size_t>(paddr)], &val, 2);
    }
}

void ReferenceMemory::write_u32(std::uint64_t paddr, std::uint32_t val) {
    if (paddr + 4 <= ram_.size()) {
        std::memcpy(&ram_[static_cast<std::size_t>(paddr)], &val, 4);
    }
}

void ReferenceMemory::write_u64(std::uint64_t paddr, std::uint64_t val) {
    if (paddr + 8 <= ram_.size()) {
        std::memcpy(&ram_[static_cast<std::size_t>(paddr)], &val, 8);
    }
}

void ReferenceMemory::write_bytes(std::uint64_t paddr, const std::uint8_t* src, std::size_t len) {
    if (paddr + len <= ram_.size() && src != nullptr) {
        std::memcpy(&ram_[static_cast<std::size_t>(paddr)], src, len);
    }
}

void ReferenceMemory::read_bytes(std::uint64_t paddr, std::uint8_t* dst, std::size_t len) const {
    if (paddr + len <= ram_.size() && dst != nullptr) {
        std::memcpy(dst, &ram_[static_cast<std::size_t>(paddr)], len);
    }
}

// -----------------------------------------------------------------------------
// Reference Exclusive Monitor Implementation
// -----------------------------------------------------------------------------
void ReferenceExclusiveMonitor::load_exclusive(std::size_t core_id, std::uint64_t paddr, std::uint64_t size) {
    if (core_id < kMaxCores) {
        reservations_[core_id].valid = true;
        reservations_[core_id].paddr = paddr;
        reservations_[core_id].size = size;
    }
}

bool ReferenceExclusiveMonitor::store_exclusive(std::size_t core_id, std::uint64_t paddr, std::uint64_t size) {
    if (core_id >= kMaxCores) return false;
    const auto& res = reservations_[core_id];
    if (!res.valid) return false;
    if (res.paddr != paddr || res.size != size) return false;

    // Successful store invalidates the caller's reservation
    reservations_[core_id].valid = false;
    return true;
}

void ReferenceExclusiveMonitor::invalidate_reservations(std::uint64_t paddr, std::uint64_t size) {
    if (size == 0) return;
    const std::uint64_t end_paddr = paddr + size;
    for (std::size_t c = 0; c < kMaxCores; ++c) {
        if (!reservations_[c].valid) continue;
        const std::uint64_t res_end = reservations_[c].paddr + reservations_[c].size;
        // Range overlap test: max(start1, start2) < min(end1, end2)
        const std::uint64_t overlap_start = (paddr > reservations_[c].paddr) ? paddr : reservations_[c].paddr;
        const std::uint64_t overlap_end = (end_paddr < res_end) ? end_paddr : res_end;
        if (overlap_start < overlap_end) {
            reservations_[c].valid = false;
        }
    }
}

bool ReferenceExclusiveMonitor::is_reservation_valid(std::size_t core_id, std::uint64_t paddr, std::uint64_t size) const noexcept {
    if (core_id >= kMaxCores) return false;
    const auto& res = reservations_[core_id];
    return res.valid && (res.paddr == paddr) && (res.size == size);
}

void ReferenceExclusiveMonitor::clear() noexcept {
    for (auto& r : reservations_) {
        r.valid = false;
        r.paddr = 0;
        r.size = 0;
    }
}

// -----------------------------------------------------------------------------
// Reference Virtual Timer Implementation
// -----------------------------------------------------------------------------
void ReferenceVirtualTimer::step_cycles(std::uint64_t cycles) noexcept {
    cntvct_ += cycles;
    if (is_interrupt_asserted()) {
        ctl_ |= (1u << 2); // Set ISTATUS
    } else {
        ctl_ &= ~(1u << 2);
    }
}

bool ReferenceVirtualTimer::is_interrupt_asserted() const noexcept {
    const bool enable = (ctl_ & 1u) != 0;
    const bool imask = (ctl_ & (1u << 1)) != 0;
    return enable && !imask && (cntvct_ >= cval_);
}

// -----------------------------------------------------------------------------
// Reference Virtual DMA Engine Implementation
// -----------------------------------------------------------------------------
ReferenceDmaResult ReferenceDmaEngine::execute_descriptor_chain(
    ReferenceMemory& mem,
    ReferenceExclusiveMonitor& monitor,
    std::uint64_t first_desc_paddr,
    std::size_t core_id) {

    (void)core_id;
    ReferenceDmaResult result{};
    result.success = true;

    std::uint64_t curr_paddr = first_desc_paddr;
    constexpr std::size_t kMaxDescriptors = 256;

    while (curr_paddr != 0 && result.descriptors_processed < kMaxDescriptors) {
        if (curr_paddr + 64 > mem.size()) {
            result.success = false;
            result.error_message = "Descriptor address out of memory bounds";
            break;
        }

        const std::uint64_t desc_id = mem.read_u64(curr_paddr + 0);
        (void)desc_id;
        const std::uint64_t src_paddr = mem.read_u64(curr_paddr + 8);
        const std::uint64_t dst_paddr = mem.read_u64(curr_paddr + 16);
        const std::uint64_t length_bytes = mem.read_u64(curr_paddr + 24);
        const std::uint32_t op_type = mem.read_u32(curr_paddr + 32);
        const std::uint32_t flags = mem.read_u32(curr_paddr + 36);
        const std::uint32_t fill_pattern = mem.read_u32(curr_paddr + 44);
        const std::uint64_t next_paddr = mem.read_u64(curr_paddr + 48);

        if (dst_paddr + length_bytes > mem.size()) {
            result.success = false;
            result.error_message = "DMA destination address out of bounds";
            break;
        }

        if (op_type == 0) { // Memcpy
            if (src_paddr + length_bytes > mem.size()) {
                result.success = false;
                result.error_message = "DMA source address out of bounds";
                break;
            }
            std::vector<std::uint8_t> tmp(static_cast<std::size_t>(length_bytes));
            mem.read_bytes(src_paddr, tmp.data(), tmp.size());
            mem.write_bytes(dst_paddr, tmp.data(), tmp.size());
        } else if (op_type == 1) { // Fill
            for (std::size_t offset = 0; offset < length_bytes; offset += 4) {
                const std::size_t remaining = static_cast<std::size_t>(length_bytes - offset);
                if (remaining >= 4) {
                    mem.write_u32(dst_paddr + offset, fill_pattern);
                } else {
                    mem.write_bytes(dst_paddr + offset, reinterpret_cast<const std::uint8_t*>(&fill_pattern), remaining);
                }
            }
        } else {
            result.success = false;
            result.error_message = "Unsupported DMA operation type in reference model";
            break;
        }

        // DMA write invalidates exclusive monitors across cluster
        monitor.invalidate_reservations(dst_paddr, length_bytes);

        // Update descriptor status: 1 = Success (offset 40)
        mem.write_u32(curr_paddr + 40, 1);

        result.descriptors_processed++;
        result.bytes_transferred += static_cast<std::size_t>(length_bytes);

        const bool chain_next = (flags & (1u << 1)) != 0;
        if (!chain_next) {
            break;
        }
        curr_paddr = next_paddr;
    }

    return result;
}

// -----------------------------------------------------------------------------
// Reference AArch64 Instruction Executor Implementation
// -----------------------------------------------------------------------------
bool ReferenceExecutor::evaluate_condition(std::uint32_t cond, std::uint32_t nzcv) noexcept {
    const bool n = (nzcv & (1u << 31)) != 0;
    const bool z = (nzcv & (1u << 30)) != 0;
    const bool c = (nzcv & (1u << 29)) != 0;
    const bool v = (nzcv & (1u << 28)) != 0;

    switch (cond & 0xFu) {
        case 0x0: return z;                   // EQ
        case 0x1: return !z;                  // NE
        case 0x2: return c;                   // CS / HS
        case 0x3: return !c;                  // CC / LO
        case 0x4: return n;                   // MI
        case 0x5: return !n;                  // PL
        case 0x6: return v;                   // VS
        case 0x7: return !v;                  // VC
        case 0x8: return c && !z;             // HI
        case 0x9: return !c || z;             // LS
        case 0xA: return n == v;              // GE
        case 0xB: return n != v;              // LT
        case 0xC: return !z && (n == v);      // GT
        case 0xD: return z || (n != v);       // LE
        case 0xE: return true;                // AL
        case 0xF: return true;                // NV
        default:  return true;
    }
}

StepResult ReferenceExecutor::step_instruction(
    ReferenceCpuState& cpu,
    ReferenceMemory& mem,
    ReferenceExclusiveMonitor& monitor,
    std::size_t core_id) {

    StepResult res{};
    const std::uint64_t pc = cpu.pc;
    if (pc + 4 > mem.size()) {
        res.success = false;
        res.error_message = "PC out of memory bounds";
        return res;
    }

    const std::uint32_t instr = mem.read_u32(pc);

    // 1. Unconditional Branch: b imm26
    // Opcode pattern: 000101 imm26 (bits 31:26 == 0b000101)
    if ((instr & 0xFC000000u) == 0x14000000u) {
        std::int64_t imm26 = static_cast<std::int64_t>(instr & 0x03FFFFFFu);
        if (imm26 & 0x02000000) {
            imm26 |= ~static_cast<std::int64_t>(0x03FFFFFFu); // Sign-extend
        }
        res.target_pc = pc + (imm26 << 2);
        res.branch_taken = true;
        cpu.pc = res.target_pc;
        cpu.retirement_count++;
        res.success = true;
        res.disassembly = "b";
        return res;
    }

    // 2. Conditional Branch: b.cond imm19
    // Opcode pattern: 01010100 imm19 0 cond (bits 31:24 == 0x54, bit 4 == 0)
    if ((instr & 0xFF000010u) == 0x54000000u) {
        const std::uint32_t cond = instr & 0xFu;
        std::int64_t imm19 = static_cast<std::int64_t>((instr >> 5u) & 0x7FFFFu);
        if (imm19 & 0x40000) {
            imm19 |= ~static_cast<std::int64_t>(0x7FFFFu); // Sign-extend
        }
        if (evaluate_condition(cond, cpu.nzcv)) {
            res.target_pc = pc + (imm19 << 2);
            res.branch_taken = true;
            cpu.pc = res.target_pc;
        } else {
            cpu.pc = pc + 4;
        }
        cpu.retirement_count++;
        res.success = true;
        res.disassembly = "b.cond";
        return res;
    }

    // 3. CBZ / CBNZ: sf 011010 op imm19 Rt
    if ((instr & 0x7E000000u) == 0x34000000u) {
        const bool sf = (instr & 0x80000000u) != 0;
        const bool is_cbnz = (instr & 0x01000000u) != 0;
        const std::size_t rt = instr & 0x1Fu;
        std::int64_t imm19 = static_cast<std::int64_t>((instr >> 5u) & 0x7FFFFu);
        if (imm19 & 0x40000) {
            imm19 |= ~static_cast<std::int64_t>(0x7FFFFu);
        }
        std::uint64_t val = (rt == 31) ? 0 : cpu.x[rt];
        if (!sf) val &= 0xFFFFFFFFu;

        const bool condition_met = is_cbnz ? (val != 0) : (val == 0);
        if (condition_met) {
            res.target_pc = pc + (imm19 << 2);
            res.branch_taken = true;
            cpu.pc = res.target_pc;
        } else {
            cpu.pc = pc + 4;
        }
        cpu.retirement_count++;
        res.success = true;
        res.disassembly = is_cbnz ? "cbnz" : "cbz";
        return res;
    }

    // 4. Data-processing immediate: ADD/ADDS/SUB/SUBS (immediate)
    // Opcode pattern: sf op S 100010 sh imm12 Rn Rd
    if ((instr & 0x1F000000u) == 0x11000000u) {
        const bool sf = (instr & 0x80000000u) != 0;
        const bool is_sub = (instr & 0x40000000u) != 0;
        const bool set_flags = (instr & 0x20000000u) != 0;
        const bool shift12 = ((instr >> 22u) & 0x3u) == 1u;
        std::uint64_t imm12 = (instr >> 10u) & 0xFFFu;
        if (shift12) imm12 <<= 12u;
        const std::size_t rn = (instr >> 5u) & 0x1Fu;
        const std::size_t rd = instr & 0x1Fu;

        const std::uint64_t op1 = (rn == 31) ? cpu.sp : cpu.x[rn];
        const std::uint64_t op2 = imm12;

        if (sf) { // 64-bit
            std::uint64_t result = 0;
            if (!is_sub) {
                result = op1 + op2;
                if (set_flags) {
                    const bool n = (static_cast<std::int64_t>(result) < 0);
                    const bool z = (result == 0);
                    const bool c = (result < op1); // carry out of addition
                    const bool v = ((~(op1 ^ op2) & (op1 ^ result)) & 0x8000000000000000ull) != 0;
                    cpu.set_nzcv(n, z, c, v);
                }
            } else {
                result = op1 - op2;
                if (set_flags) {
                    const bool n = (static_cast<std::int64_t>(result) < 0);
                    const bool z = (result == 0);
                    const bool c = (op1 >= op2); // no borrow
                    const bool v = (((op1 ^ op2) & (op1 ^ result)) & 0x8000000000000000ull) != 0;
                    cpu.set_nzcv(n, z, c, v);
                }
            }
            if (rd != 31 || !set_flags) {
                if (rd == 31) cpu.sp = result;
                else cpu.x[rd] = result;
            }
        } else { // 32-bit
            const std::uint32_t op1_32 = static_cast<std::uint32_t>(op1);
            const std::uint32_t op2_32 = static_cast<std::uint32_t>(op2);
            std::uint32_t result = 0;
            if (!is_sub) {
                result = op1_32 + op2_32;
                if (set_flags) {
                    const bool n = (static_cast<std::int32_t>(result) < 0);
                    const bool z = (result == 0);
                    const bool c = (result < op1_32);
                    const bool v = ((~(op1_32 ^ op2_32) & (op1_32 ^ result)) & 0x80000000u) != 0;
                    cpu.set_nzcv(n, z, c, v);
                }
            } else {
                result = op1_32 - op2_32;
                if (set_flags) {
                    const bool n = (static_cast<std::int32_t>(result) < 0);
                    const bool z = (result == 0);
                    const bool c = (op1_32 >= op2_32);
                    const bool v = (((op1_32 ^ op2_32) & (op1_32 ^ result)) & 0x80000000u) != 0;
                    cpu.set_nzcv(n, z, c, v);
                }
            }
            if (rd != 31 || !set_flags) {
                if (rd == 31) cpu.sp = result;
                else cpu.x[rd] = result; // 32-bit zero-extended to 64-bit
            }
        }
        cpu.pc = pc + 4;
        cpu.retirement_count++;
        res.success = true;
        res.disassembly = is_sub ? "sub" : "add";
        return res;
    }

    // 4b. Data-processing shifted register: ADD/ADDS/SUB/SUBS (shifted register)
    // Opcode pattern: sf op S 01011 shift 0 Rm imm6 Rn Rd
    if ((instr & 0x1F200000u) == 0x0B000000u) {
        const bool sf = (instr & 0x80000000u) != 0;
        const bool is_sub = (instr & 0x40000000u) != 0;
        const bool set_flags = (instr & 0x20000000u) != 0;
        const std::uint32_t shift_type = (instr >> 22u) & 0x3u;
        const std::size_t rm = (instr >> 16u) & 0x1Fu;
        const std::size_t imm6 = (instr >> 10u) & 0x3Fu;
        const std::size_t rn = (instr >> 5u) & 0x1Fu;
        const std::size_t rd = instr & 0x1Fu;

        const std::uint64_t op1 = (rn == 31) ? 0 : cpu.x[rn];
        std::uint64_t op2 = (rm == 31) ? 0 : cpu.x[rm];
        if (shift_type == 0) op2 <<= imm6;
        else if (shift_type == 1) op2 >>= imm6;

        if (sf) { // 64-bit
            std::uint64_t result = is_sub ? (op1 - op2) : (op1 + op2);
            if (set_flags) {
                const bool n = (static_cast<std::int64_t>(result) < 0);
                const bool z = (result == 0);
                const bool c = is_sub ? (op1 >= op2) : (result < op1);
                const bool v = (((op1 ^ op2) & (op1 ^ result)) & 0x8000000000000000ull) != 0;
                cpu.set_nzcv(n, z, c, v);
            }
            if (rd != 31) cpu.x[rd] = result;
        } else { // 32-bit
            const std::uint32_t op1_32 = static_cast<std::uint32_t>(op1);
            const std::uint32_t op2_32 = static_cast<std::uint32_t>(op2);
            std::uint32_t result = is_sub ? (op1_32 - op2_32) : (op1_32 + op2_32);
            if (set_flags) {
                const bool n = (static_cast<std::int32_t>(result) < 0);
                const bool z = (result == 0);
                const bool c = is_sub ? (op1_32 >= op2_32) : (result < op1_32);
                const bool v = (((op1_32 ^ op2_32) & (op1_32 ^ result)) & 0x80000000u) != 0;
                cpu.set_nzcv(n, z, c, v);
            }
            if (rd != 31) cpu.x[rd] = result;
        }
        cpu.pc = pc + 4;
        cpu.retirement_count++;
        res.success = true;
        res.disassembly = is_sub ? "sub (reg)" : "add (reg)";
        return res;
    }

    // 5. Logical shifted register: AND/ANDS/ORR/EOR/BIC
    // Opcode pattern: sf opc 01010 shift N Rm imm6 Rn Rd
    if ((instr & 0x1F000000u) == 0x0A000000u) {
        const bool sf = (instr & 0x80000000u) != 0;
        const std::uint32_t opc = (instr >> 29u) & 0x3u;
        const bool n_invert = (instr & (1u << 21)) != 0;
        const std::size_t rm = (instr >> 16u) & 0x1Fu;
        const std::size_t rn = (instr >> 5u) & 0x1Fu;
        const std::size_t rd = instr & 0x1Fu;

        std::uint64_t op1 = (rn == 31) ? 0 : cpu.x[rn];
        std::uint64_t op2 = (rm == 31) ? 0 : cpu.x[rm];
        if (n_invert) op2 = ~op2;

        std::uint64_t result = 0;
        if (opc == 0) result = op1 & op2;      // AND / BIC
        else if (opc == 1) result = op1 | op2; // ORR / ORN
        else if (opc == 2) result = op1 ^ op2; // EOR / EON
        else if (opc == 3) {                   // ANDS / BICS
            result = op1 & op2;
            if (!sf) result &= 0xFFFFFFFFu;
            const bool n = sf ? (static_cast<std::int64_t>(result) < 0) : (static_cast<std::int32_t>(result) < 0);
            const bool z = (result == 0);
            cpu.set_nzcv(n, z, false, false);
        }

        if (!sf) result &= 0xFFFFFFFFu;
        if (rd != 31) cpu.x[rd] = result;

        cpu.pc = pc + 4;
        cpu.retirement_count++;
        res.success = true;
        res.disassembly = "logical";
        return res;
    }

    // 6. LDR / STR (immediate unsigned offset)
    // 1x11 1001 01 imm12 Rn Rt (32-bit: size=2, 64-bit: size=3)
    if ((instr & 0x3B200000u) == 0x39000000u) {
        const std::uint32_t size = (instr >> 30u) & 0x3u;
        const bool is_load = (instr & 0x00400000u) != 0;
        const std::size_t imm12 = (instr >> 10u) & 0xFFFu;
        const std::size_t rn = (instr >> 5u) & 0x1Fu;
        const std::size_t rt = instr & 0x1Fu;

        const std::uint64_t base = (rn == 31) ? cpu.sp : cpu.x[rn];
        const std::uint64_t offset = imm12 << size;
        const std::uint64_t target_addr = base + offset;

        if (is_load) {
            if (size == 2) {
                const std::uint32_t val = mem.read_u32(target_addr);
                if (rt != 31) cpu.x[rt] = val;
            } else if (size == 3) {
                const std::uint64_t val = mem.read_u64(target_addr);
                if (rt != 31) cpu.x[rt] = val;
            }
        } else {
            const std::uint64_t val = (rt == 31) ? 0 : cpu.x[rt];
            if (size == 2) {
                mem.write_u32(target_addr, static_cast<std::uint32_t>(val));
                monitor.invalidate_reservations(target_addr, 4);
            } else if (size == 3) {
                mem.write_u64(target_addr, val);
                monitor.invalidate_reservations(target_addr, 8);
            }
        }
        cpu.pc = pc + 4;
        cpu.retirement_count++;
        res.success = true;
        res.disassembly = is_load ? "ldr" : "str";
        return res;
    }

    // 7. LDXR / STXR (Exclusive load/store)
    // 1x00 1000 0101 1111 0111 11 Rn Rt (LDXR)
    if ((instr & 0x3FE00000u) == 0x085F7C00u) {
        const std::uint32_t size = (instr >> 30u) & 0x3u;
        const std::size_t rn = (instr >> 5u) & 0x1Fu;
        const std::size_t rt = instr & 0x1Fu;
        const std::uint64_t base = (rn == 31) ? cpu.sp : cpu.x[rn];

        if (size == 2) {
            const std::uint32_t val = mem.read_u32(base);
            monitor.load_exclusive(core_id, base, 4);
            if (rt != 31) cpu.x[rt] = val;
        } else if (size == 3) {
            const std::uint64_t val = mem.read_u64(base);
            monitor.load_exclusive(core_id, base, 8);
            if (rt != 31) cpu.x[rt] = val;
        }
        cpu.pc = pc + 4;
        cpu.retirement_count++;
        res.success = true;
        res.disassembly = "ldxr";
        return res;
    }

    // STXR: 1x00 1000 000 Rs 0111 11 Rn Rt
    if ((instr & 0x3FE08000u) == 0x08000000u) {
        const std::uint32_t size = (instr >> 30u) & 0x3u;
        const std::size_t rs = (instr >> 16u) & 0x1Fu;
        const std::size_t rn = (instr >> 5u) & 0x1Fu;
        const std::size_t rt = instr & 0x1Fu;
        const std::uint64_t base = (rn == 31) ? cpu.sp : cpu.x[rn];

        const std::size_t access_size = (size == 2) ? 4 : 8;
        const bool success = monitor.store_exclusive(core_id, base, access_size);
        if (success) {
            const std::uint64_t val = (rt == 31) ? 0 : cpu.x[rt];
            if (size == 2) mem.write_u32(base, static_cast<std::uint32_t>(val));
            else mem.write_u64(base, val);
            monitor.invalidate_reservations(base, access_size);
        }
        if (rs != 31) {
            cpu.x[rs] = success ? 0 : 1; // 0 = Success, 1 = Fail
        }
        cpu.pc = pc + 4;
        cpu.retirement_count++;
        res.success = true;
        res.disassembly = "stxr";
        return res;
    }

    // 8. Barriers (DMB, DSB, ISB) -> NOP in single-threaded reference model
    if ((instr & 0xFFFFF0FFu) == 0xD50330DFu || (instr & 0xFFFFF0FFu) == 0xD5033FDFu) {
        cpu.pc = pc + 4;
        cpu.retirement_count++;
        res.success = true;
        res.disassembly = "barrier";
        return res;
    }

    // Fallthrough: Unsupported opcode in reference subset
    std::ostringstream oss;
    oss << "Unsupported instruction in reference model at 0x" << std::hex << pc
        << ": 0x" << std::setfill('0') << std::setw(8) << instr;
    res.success = false;
    res.error_message = oss.str();
    return res;
}

// -----------------------------------------------------------------------------
// Differential Divergence Diagnostics Implementation
// -----------------------------------------------------------------------------
DivergenceReport DifferentialValidator::compare_cpu_state(
    std::size_t core_id,
    const ReferenceCpuState& expected,
    std::uint64_t actual_pc,
    std::uint32_t actual_nzcv,
    std::uint64_t actual_retirements,
    const std::uint64_t* actual_x31) {

    DivergenceReport report{};
    report.core_id = core_id;
    report.pc = actual_pc;
    report.retirement_count = actual_retirements;

    if (expected.pc != actual_pc) {
        report.has_divergence = true;
        report.field_name = "PC";
        report.expected_value = expected.pc;
        report.actual_value = actual_pc;
        report.message = "Program Counter mismatch";
        return report;
    }

    if (expected.retirement_count != actual_retirements) {
        report.has_divergence = true;
        report.field_name = "RetirementCount";
        report.expected_value = expected.retirement_count;
        report.actual_value = actual_retirements;
        report.message = "Instruction retirement count mismatch";
        return report;
    }

    if ((expected.nzcv & 0xF0000000u) != (actual_nzcv & 0xF0000000u)) {
        report.has_divergence = true;
        report.field_name = "NZCV";
        report.expected_value = (expected.nzcv & 0xF0000000u);
        report.actual_value = (actual_nzcv & 0xF0000000u);
        report.message = "Architectural condition flags (NZCV) mismatch";
        return report;
    }

    if (actual_x31 != nullptr) {
        for (std::size_t i = 0; i < 31; ++i) {
            if (expected.x[i] != actual_x31[i]) {
                report.has_divergence = true;
                report.field_name = "X" + std::to_string(i);
                report.expected_value = expected.x[i];
                report.actual_value = actual_x31[i];
                report.message = "General-purpose register value mismatch";
                return report;
            }
        }
    }

    return report;
}

DivergenceReport DifferentialValidator::compare_memory(
    const ReferenceMemory& expected,
    const std::uint8_t* actual_ram,
    std::size_t actual_size) {

    DivergenceReport report{};
    if (expected.size() != actual_size) {
        report.has_divergence = true;
        report.field_name = "MemorySize";
        report.expected_value = expected.size();
        report.actual_value = actual_size;
        report.message = "Guest RAM capacity mismatch";
        return report;
    }

    for (std::size_t i = 0; i < actual_size; ++i) {
        if (expected.data()[i] != actual_ram[i]) {
            report.has_divergence = true;
            report.field_name = "MemoryByte";
            report.pc = i; // Store diverging physical address in pc field
            report.expected_value = expected.data()[i];
            report.actual_value = actual_ram[i];
            report.message = "Memory content mismatch at physical address";
            return report;
        }
    }

    return report;
}


// -----------------------------------------------------------------------------
// Reference MMU & Page Table Walker Implementation
// -----------------------------------------------------------------------------
ReferenceTranslationResult ReferencePageTableWalker::walk_stage1(
    const ReferenceMemory& mem,
    std::uint64_t ttbr0,
    std::uint64_t tcr,
    std::uint64_t sctlr,
    std::uint64_t va,
    int access_kind,
    unsigned current_el) {

    ReferenceTranslationResult res{};
    res.page_size = 4096;

    // Check if Stage 1 translation is disabled
    if ((sctlr & 0x1ULL) == 0) {
        res.success = true;
        res.physical_address = va;
        res.readable = true;
        res.writable = true;
        res.executable = true;
        res.el0_accessible = true;
        return res;
    }

    // Top-Byte Ignore (TBI0)
    std::uint64_t eff_va = va;
    if (tcr & (1ULL << 37u)) {
        eff_va &= 0x00FFFFFFFFFFFFFFULL;
    }

    // Address size check based on T0SZ
    unsigned t0sz = static_cast<unsigned>(tcr & 0x3Fu);
    if (t0sz == 0) t0sz = 16u;
    const unsigned va_bits = (t0sz >= 16u && t0sz <= 39u) ? (64u - t0sz) : 48u;
    if ((eff_va >> va_bits) != 0ULL) {
        res.fault_type = ReferenceMmuFaultType::AddressSize;
        res.fault_level = 0;
        res.fsc = 0x00; // Address size fault Level 0
        return res;
    }

    const std::uint64_t mem_size = mem.size();
    std::uint64_t table_pa = ttbr0 & 0x0000FFFFFFFFF000ULL;

    auto check_permissions = [&](std::uint64_t desc, unsigned level, std::uint64_t pa, std::uint64_t page_sz) -> ReferenceTranslationResult {
        ReferenceTranslationResult r{};
        r.page_size = page_sz;
        r.physical_address = pa;

        const std::uint8_t ap = static_cast<std::uint8_t>((desc >> 6u) & 0x3u);
        const bool uxn = ((desc >> 54u) & 0x1ULL) != 0;
        const bool pxn = ((desc >> 53u) & 0x1ULL) != 0;
        const bool af  = ((desc >> 10u) & 0x1ULL) != 0;

        r.el0_accessible = (ap & 0x1u) != 0;
        r.writable = (ap & 0x2u) == 0;
        r.readable = true;
        r.executable = (current_el == 0) ? !uxn : !pxn;

        // Check Access Flag (AF)
        if (!af) {
            r.fault_type = ReferenceMmuFaultType::AccessFlag;
            r.fault_level = level;
            r.fsc = 0x08u + level;
            return r;
        }

        // Check Permissions
        if (current_el == 0 && !r.el0_accessible) {
            r.fault_type = ReferenceMmuFaultType::Permission;
            r.fault_level = level;
            r.fsc = 0x0Cu + level;
            return r;
        }
        if (access_kind == 1 && !r.writable) { // Write access
            r.fault_type = ReferenceMmuFaultType::Permission;
            r.fault_level = level;
            r.fsc = 0x0Cu + level;
            return r;
        }
        if (access_kind == 2 && !r.executable) { // Execute access
            r.fault_type = ReferenceMmuFaultType::Permission;
            r.fault_level = level;
            r.fsc = 0x0Cu + level;
            return r;
        }

        r.success = true;
        return r;
    };

    // --- Level 0 Walk ---
    const std::uint64_t l0_idx = (eff_va >> 39u) & 0x1FFu;
    const std::uint64_t l0_entry_addr = table_pa + l0_idx * 8u;
    if (l0_entry_addr + 8u > mem_size || l0_entry_addr < table_pa) {
        res.fault_type = ReferenceMmuFaultType::Translation;
        res.fault_level = 0;
        res.fsc = 0x04;
        return res;
    }
    const std::uint64_t l0_desc = mem.read_u64(l0_entry_addr);
    if ((l0_desc & 0x3u) != 0x3u) { // Level 0 must be Table descriptor
        res.fault_type = ReferenceMmuFaultType::Translation;
        res.fault_level = 0;
        res.fsc = 0x04;
        return res;
    }

    // --- Level 1 Walk ---
    table_pa = l0_desc & 0x0000FFFFFFFFF000ULL;
    const std::uint64_t l1_idx = (eff_va >> 30u) & 0x1FFu;
    const std::uint64_t l1_entry_addr = table_pa + l1_idx * 8u;
    if (l1_entry_addr + 8u > mem_size || l1_entry_addr < table_pa) {
        res.fault_type = ReferenceMmuFaultType::Translation;
        res.fault_level = 1;
        res.fsc = 0x05;
        return res;
    }
    const std::uint64_t l1_desc = mem.read_u64(l1_entry_addr);
    if ((l1_desc & 0x1u) == 0) {
        res.fault_type = ReferenceMmuFaultType::Translation;
        res.fault_level = 1;
        res.fsc = 0x05;
        return res;
    }
    if ((l1_desc & 0x3u) == 0x1u) { // 1GB Block
        if ((l1_desc & 0x3FFFF000ULL) != 0) {
            res.fault_type = ReferenceMmuFaultType::AddressSize;
            res.fault_level = 1;
            res.fsc = 0x01;
            return res;
        }
        const std::uint64_t pa = (l1_desc & 0x0000FFFFC0000000ULL) | (eff_va & 0x3FFFFFFFULL);
        return check_permissions(l1_desc, 1, pa, 1024ULL * 1024 * 1024);
    }

    // --- Level 2 Walk ---
    table_pa = l1_desc & 0x0000FFFFFFFFF000ULL;
    const std::uint64_t l2_idx = (eff_va >> 21u) & 0x1FFu;
    const std::uint64_t l2_entry_addr = table_pa + l2_idx * 8u;
    if (l2_entry_addr + 8u > mem_size || l2_entry_addr < table_pa) {
        res.fault_type = ReferenceMmuFaultType::Translation;
        res.fault_level = 2;
        res.fsc = 0x06;
        return res;
    }
    const std::uint64_t l2_desc = mem.read_u64(l2_entry_addr);
    if ((l2_desc & 0x1u) == 0) {
        res.fault_type = ReferenceMmuFaultType::Translation;
        res.fault_level = 2;
        res.fsc = 0x06;
        return res;
    }
    if ((l2_desc & 0x3u) == 0x1u) { // 2MB Block
        if ((l2_desc & 0x001FF000ULL) != 0) {
            res.fault_type = ReferenceMmuFaultType::AddressSize;
            res.fault_level = 2;
            res.fsc = 0x02;
            return res;
        }
        const std::uint64_t pa = (l2_desc & 0x0000FFFFFFE00000ULL) | (eff_va & 0x1FFFFFULL);
        return check_permissions(l2_desc, 2, pa, 2ULL * 1024 * 1024);
    }

    // --- Level 3 Walk ---
    table_pa = l2_desc & 0x0000FFFFFFFFF000ULL;
    const std::uint64_t l3_idx = (eff_va >> 12u) & 0x1FFu;
    const std::uint64_t l3_entry_addr = table_pa + l3_idx * 8u;
    if (l3_entry_addr + 8u > mem_size || l3_entry_addr < table_pa) {
        res.fault_type = ReferenceMmuFaultType::Translation;
        res.fault_level = 3;
        res.fsc = 0x07;
        return res;
    }
    const std::uint64_t l3_desc = mem.read_u64(l3_entry_addr);
    if ((l3_desc & 0x3u) != 0x3u) { // Level 3 must be Page descriptor (bits[1:0] == 11)
        res.fault_type = ReferenceMmuFaultType::Translation;
        res.fault_level = 3;
        res.fsc = 0x07;
        return res;
    }

    const std::uint64_t pa = (l3_desc & 0x0000FFFFFFFFF000ULL) | (eff_va & 0xFFFULL);
    return check_permissions(l3_desc, 3, pa, 4096ULL);
}

// -----------------------------------------------------------------------------
// Reference Exception Model Implementation
// -----------------------------------------------------------------------------
std::uint32_t ReferenceExceptionModel::calculate_vector_offset(unsigned origin_el, unsigned target_el, bool sp_sel, int exception_type) noexcept {
    std::uint32_t table_section = 0;
    if (origin_el < target_el) {
        table_section = 0x400u; // Lower EL, AArch64
    } else {
        table_section = sp_sel ? 0x200u : 0x000u; // Current EL with SPx / SP0
    }
    std::uint32_t type_offset = 0;
    switch (exception_type) {
    case 0: type_offset = 0x000u; break; // Sync
    case 1: type_offset = 0x080u; break; // IRQ
    case 2: type_offset = 0x100u; break; // FIQ
    case 3: type_offset = 0x180u; break; // SError
    }
    return table_section + type_offset;
}

ReferenceExceptionEntryResult ReferenceExceptionModel::take_synchronous_exception(
    std::uint64_t /*current_pc*/,
    unsigned current_el,
    bool sp_sel,
    std::uint8_t daif,
    std::uint8_t nzcv,
    std::uint64_t vbar_el1,
    std::uint32_t ec,
    std::uint32_t iss,
    std::uint64_t fault_address,
    bool has_fault_address,
    std::uint64_t preferred_return,
    unsigned target_el) {

    ReferenceExceptionEntryResult res{};
    res.target_el = target_el;

    res.vector_offset = calculate_vector_offset(current_el, target_el, sp_sel, 0);
    res.target_pc = vbar_el1 + res.vector_offset;

    // SPSR encoding
    const std::uint32_t m = (current_el == 0)
        ? 0u
        : static_cast<std::uint32_t>(((current_el & 0x3u) << 2u) | (sp_sel ? 0x1u : 0x0u));
    res.spsr = (static_cast<std::uint32_t>(nzcv & 0xFu) << 28u)
             | (static_cast<std::uint32_t>(daif & 0xFu) << 6u)
             | m;

    res.elr = preferred_return;
    res.esr = ((ec & 0x3Fu) << 26u) | (1u << 25u) | (iss & 0x01FFFFFFu);
    res.far = has_fault_address ? fault_address : 0;
    res.daif = 0xFu;
    res.sp_sel = true;

    return res;
}

ReferenceExceptionEntryResult ReferenceExceptionModel::take_asynchronous_exception(
    std::uint64_t current_pc,
    unsigned current_el,
    bool sp_sel,
    std::uint8_t daif,
    std::uint8_t nzcv,
    std::uint64_t vbar_el1,
    bool is_fiq,
    std::uint32_t /*intid*/,
    std::uint8_t /*priority*/,
    unsigned target_el) {

    ReferenceExceptionEntryResult res{};
    res.target_el = target_el;

    res.vector_offset = calculate_vector_offset(current_el, target_el, sp_sel, is_fiq ? 2 : 1);
    res.target_pc = vbar_el1 + res.vector_offset;

    const std::uint32_t m = (current_el == 0)
        ? 0u
        : static_cast<std::uint32_t>(((current_el & 0x3u) << 2u) | (sp_sel ? 0x1u : 0x0u));
    res.spsr = (static_cast<std::uint32_t>(nzcv & 0xFu) << 28u)
             | (static_cast<std::uint32_t>(daif & 0xFu) << 6u)
             | m;

    res.elr = current_pc;
    res.esr = 0u; // EC = 0 for IRQ/FIQ
    res.far = 0u;
    res.daif = 0xFu;
    res.sp_sel = true;

    return res;
}

bool ReferenceExceptionModel::execute_eret(
    ReferenceCpuState& cpu,
    std::uint32_t spsr,
    std::uint64_t elr,
    std::string& error_msg) {

    const std::uint32_t m = spsr & 0x1Fu;
    const std::uint32_t target_el = (m >> 2u) & 0x3u;

    // Valid modes: 0x00 (EL0t), 0x04 (EL1t), 0x05 (EL1h)
    const bool valid_m = (m == 0x00u || m == 0x04u || m == 0x05u);
    if (!valid_m || target_el > 1) {
        error_msg = "ERET: invalid SPSR M field: 0x" + std::to_string(m);
        return false;
    }

    const bool n = ((spsr >> 31u) & 1u) != 0;
    const bool z = ((spsr >> 30u) & 1u) != 0;
    const bool c = ((spsr >> 29u) & 1u) != 0;
    const bool v = ((spsr >> 28u) & 1u) != 0;
    cpu.set_nzcv(n, z, c, v);

    cpu.pc = elr;
    cpu.retirement_count++;
    return true;
}

// -----------------------------------------------------------------------------
// Reference Syndrome Model Implementation
// -----------------------------------------------------------------------------
std::uint32_t ReferenceSyndromeModel::compute_data_abort_esr(
    bool is_same_el,
    std::uint32_t fsc,
    bool is_write,
    unsigned access_size) {

    const std::uint32_t ec = is_same_el ? 0x25u : 0x24u;
    const std::uint32_t sas = (access_size == 8) ? 3u : ((access_size == 4) ? 2u : ((access_size == 2) ? 1u : 0u));
    const std::uint32_t iss = (fsc & 0x3Fu)
                            | (is_write ? (1u << 6u) : 0u) // WnR
                            | (sas << 22u)                // SAS
                            | (1u << 24u);                // ISV (Instruction Syndrome Valid)
    return (ec << 26u) | (1u << 25u) | iss;
}

std::uint32_t ReferenceSyndromeModel::compute_instruction_abort_esr(
    bool is_same_el,
    std::uint32_t fsc) {

    const std::uint32_t ec = is_same_el ? 0x21u : 0x20u;
    const std::uint32_t iss = fsc & 0x3Fu;
    return (ec << 26u) | (1u << 25u) | iss;
}

std::uint32_t ReferenceSyndromeModel::compute_svc_esr(std::uint16_t imm16) {
    const std::uint32_t ec = 0x15u; // SVC in AArch64
    const std::uint32_t iss = imm16;
    return (ec << 26u) | (1u << 25u) | iss;
}

std::uint32_t ReferenceSyndromeModel::compute_pc_alignment_esr(bool /*is_same_el*/) {
    const std::uint32_t ec = 0x22u; // PC alignment fault
    return (ec << 26u) | (1u << 25u);
}

std::uint32_t ReferenceSyndromeModel::compute_undefined_instruction_esr() {
    const std::uint32_t ec = 0x00u; // Unknown reason / Undefined instruction
    return (ec << 26u) | (1u << 25u);
}

// -----------------------------------------------------------------------------
// Reference GIC Model Implementation
// -----------------------------------------------------------------------------
ReferenceGicModel::ReferenceGicModel() noexcept {
    reset();
}

void ReferenceGicModel::reset() noexcept {
    enabled_ = true;
    pmr_.fill(0xFFu);
    priority_.fill(0x80u);

    for (auto& row : ppi_enabled_) row.fill(false);
    for (auto& row : ppi_pending_) row.fill(false);
    for (auto& row : ppi_active_)  row.fill(false);

    spi_enabled_.fill(false);
    spi_pending_.fill(false);
    spi_active_.fill(false);
    spi_target_core_.fill(0);
}

void ReferenceGicModel::set_pmr(std::size_t core_id, std::uint8_t pmr) noexcept {
    if (core_id < kMaxCores) {
        pmr_[core_id] = pmr;
    }
}

void ReferenceGicModel::set_irq_priority(std::uint32_t intid, std::uint8_t priority) noexcept {
    if (intid < kTotalIrqs) {
        priority_[intid] = priority;
    }
}

void ReferenceGicModel::set_irq_enabled(std::uint32_t intid, bool enabled, std::size_t core_id) noexcept {
    if (intid < 32) {
        if (core_id < kMaxCores) ppi_enabled_[core_id][intid] = enabled;
    } else if (intid < kTotalIrqs) {
        spi_enabled_[intid - 32] = enabled;
    }
}

void ReferenceGicModel::set_irq_pending(std::uint32_t intid, bool pending, std::size_t core_id) noexcept {
    if (intid < 32) {
        if (core_id < kMaxCores) ppi_pending_[core_id][intid] = pending;
    } else if (intid < kTotalIrqs) {
        spi_pending_[intid - 32] = pending;
    }
}

ReferenceGicSignal ReferenceGicModel::evaluate(std::size_t core_id, unsigned /*current_el*/) const noexcept {
    ReferenceGicSignal sig{};
    if (!enabled_ || core_id >= kMaxCores) return sig;

    const std::uint8_t current_pmr = pmr_[core_id];
    std::uint8_t highest_prio = 0xFFu;
    std::uint32_t best_intid = 1023u;

    // Evaluate SGIs & PPIs for this core (INTID 0..31)
    for (std::uint32_t id = 0; id < 32; ++id) {
        if (ppi_enabled_[core_id][id] && ppi_pending_[core_id][id] && !ppi_active_[core_id][id]) {
            const std::uint8_t prio = priority_[id];
            // Lower numeric value = higher priority. Tie-breaker: lower INTID wins.
            if (prio < current_pmr && (prio < highest_prio || (prio == highest_prio && id < best_intid))) {
                highest_prio = prio;
                best_intid = id;
            }
        }
    }

    // Evaluate SPIs targeting this core (INTID 32..127)
    for (std::uint32_t id = 32; id < kTotalIrqs; ++id) {
        const std::size_t idx = id - 32;
        if (spi_enabled_[idx] && spi_pending_[idx] && !spi_active_[idx] && spi_target_core_[idx] == core_id) {
            const std::uint8_t prio = priority_[id];
            if (prio < current_pmr && (prio < highest_prio || (prio == highest_prio && id < best_intid))) {
                highest_prio = prio;
                best_intid = id;
            }
        }
    }

    if (best_intid != 1023u) {
        sig.has_interrupt = true;
        sig.intid = best_intid;
        sig.priority = highest_prio;
        sig.is_fiq = (highest_prio < 0x80u); // Group 0 / Secure mapped to FIQ
    }

    return sig;
}

std::uint32_t ReferenceGicModel::acknowledge_irq(std::size_t core_id) noexcept {
    const auto sig = evaluate(core_id, 1);
    if (!sig.has_interrupt) return 1023u;

    const std::uint32_t id = sig.intid;
    if (id < 32) {
        ppi_pending_[core_id][id] = false;
        ppi_active_[core_id][id] = true;
    } else if (id < kTotalIrqs) {
        spi_pending_[id - 32] = false;
        spi_active_[id - 32] = true;
    }
    return id;
}

void ReferenceGicModel::end_of_interrupt(std::size_t core_id, std::uint32_t intid) noexcept {
    if (intid < 32) {
        if (core_id < kMaxCores) ppi_active_[core_id][intid] = false;
    } else if (intid < kTotalIrqs) {
        spi_active_[intid - 32] = false;
    }
}

// -----------------------------------------------------------------------------
// Reference Litmus Verifier Implementation
// -----------------------------------------------------------------------------
LitmusOutcome ReferenceLitmusVerifier::verify_message_passing(std::uint64_t read_flag, std::uint64_t read_data) {
    LitmusOutcome outcome{};
    outcome.r0 = read_flag;
    outcome.r1 = read_data;
    outcome.valid = true;

    // Forbidden outcome in standard Message Passing:
    // Core 1 reads Flag == 1, but reads Data == 0 (violating barrier visibility)
    if (read_flag == 1 && read_data == 0) {
        outcome.is_forbidden = true;
    }
    return outcome;
}

LitmusOutcome ReferenceLitmusVerifier::verify_store_buffering(std::uint64_t read_y, std::uint64_t read_x) {
    LitmusOutcome outcome{};
    outcome.r0 = read_y;
    outcome.r1 = read_x;
    outcome.valid = true;

    // Under strict sequential consistency with DSB barriers, r0 == 0 && r1 == 0 is forbidden
    if (read_y == 0 && read_x == 0) {
        outcome.is_forbidden = true;
    }
    return outcome;
}

// -----------------------------------------------------------------------------
// Reference System Register Model (Stage 36)
// -----------------------------------------------------------------------------
std::uint64_t ReferenceSysRegModel::write_wn_zero_extends_xn(std::uint64_t original_xn, std::uint32_t wn_val) noexcept {
    (void)original_xn;
    return static_cast<std::uint64_t>(wn_val);
}

std::uint32_t ReferenceSysRegModel::read_wn_lower32(std::uint64_t xn_val) noexcept {
    return static_cast<std::uint32_t>(xn_val & 0xFFFFFFFFu);
}

bool ReferenceSysRegModel::check_privilege(unsigned current_el, std::uint16_t key, bool is_read) noexcept {
    if (current_el == 0) {
        // Legal at EL0:
        // NZCV: 0x5A10
        // DAIF: 0x5A11
        // CurrentEL: 0x4212 (read-only)
        // TPIDR_EL0: 0x5E82
        // TPIDRRO_EL0: 0x5E83 (read-only)
        if (key == 0x5A10u || key == 0x5A11u || key == 0x5E82u) {
            return true;
        }
        if (key == 0x4212u || key == 0x5E83u) {
            return is_read;
        }
        return false;
    }
    // EL1+: All EL0 and EL1 registers are accessible
    return true;
}

ReferenceSysRegResult ReferenceSysRegModel::step_sysreg_instruction(
    ReferenceCpuState& cpu,
    ReferenceSysRegState& sysregs,
    std::uint32_t instruction) {

    ReferenceSysRegResult result{};
    // Detect system-register instruction: bits[31:22] = 1101010100 and bit[20] = 1
    if (((instruction & 0xFFC00000u) != 0xD5000000u) || ((instruction & 0x00100000u) == 0u)) {
        result.status = ReferenceSysRegStatus::UnallocatedEncoding;
        return result;
    }

    const bool is_read = (instruction & (1u << 21u)) != 0u;
    const std::uint32_t rt = instruction & 0x1Fu;
    const std::uint32_t op1 = (instruction >> 16u) & 0x7u;
    const std::uint32_t crn = (instruction >> 12u) & 0xFu;
    const std::uint32_t crm = (instruction >> 8u) & 0xFu;
    const std::uint32_t op2 = (instruction >> 5u) & 0x7u;
    std::uint32_t o0 = (instruction >> 19u) & 0x1u;
    if (o0 == 0u && !(op1 == 0u && crn == 0u && (op2 == 2u || (op2 >= 4u && op2 <= 7u)))) {
        o0 = 1u;
    }
    const std::uint16_t key = static_cast<std::uint16_t>(
        (o0 << 14u) | (op1 << 11u) | (crn << 7u) | (crm << 3u) | op2);

    result.is_mrs = is_read;
    result.key = key;
    result.rt = rt;

    // Privilege check
    if (!check_privilege(sysregs.current_el, key, is_read)) {
        result.status = ReferenceSysRegStatus::UndefinedException;
        return result;
    }

    if (is_read) {
        // MRS
        std::uint64_t val = 0;
        switch (key) {
        case 0x5A10u: // NZCV
            val = static_cast<std::uint64_t>(cpu.nzcv);
            result.reg_name = "NZCV";
            break;
        case 0x5A11u: // DAIF
            val = static_cast<std::uint64_t>(sysregs.daif) << 6u;
            result.reg_name = "DAIF";
            break;
        case 0x4212u: // CurrentEL
            val = static_cast<std::uint64_t>(sysregs.current_el & 0x3u) << 2u;
            result.reg_name = "CurrentEL";
            break;
        case 0x4200u: // SPSR_EL1
            val = sysregs.spsr_el1;
            result.reg_name = "SPSR_EL1";
            break;
        case 0x4201u: // ELR_EL1
            val = sysregs.elr_el1;
            result.reg_name = "ELR_EL1";
            break;
        case 0x4290u: // ESR_EL1
            val = sysregs.esr_el1;
            result.reg_name = "ESR_EL1";
            break;
        case 0x4300u: // FAR_EL1
            val = sysregs.far_el1;
            result.reg_name = "FAR_EL1";
            break;
        case 0x4600u: // VBAR_EL1
            val = sysregs.vbar_el1;
            result.reg_name = "VBAR_EL1";
            break;
        case 0x4080u: // SCTLR_EL1
            val = sysregs.sctlr_el1;
            result.reg_name = "SCTLR_EL1";
            break;
        case 0x4102u: // TCR_EL1
            val = sysregs.tcr_el1;
            result.reg_name = "TCR_EL1";
            break;
        case 0x4100u: // TTBR0_EL1
            val = sysregs.ttbr0_el1;
            result.reg_name = "TTBR0_EL1";
            break;
        case 0x4101u: // TTBR1_EL1
            val = sysregs.ttbr1_el1;
            result.reg_name = "TTBR1_EL1";
            break;
        case 0x4510u: // MAIR_EL1
            val = sysregs.mair_el1;
            result.reg_name = "MAIR_EL1";
            break;
        case 0x4082u: // CPACR_EL1
            val = sysregs.cpacr_el1;
            result.reg_name = "CPACR_EL1";
            break;
        case 0x5E82u: // TPIDR_EL0
            val = sysregs.tpidr_el0;
            result.reg_name = "TPIDR_EL0";
            break;
        case 0x4684u: // TPIDR_EL1
            val = sysregs.tpidr_el1;
            result.reg_name = "TPIDR_EL1";
            break;
        case 0x5E83u: // TPIDRRO_EL0
            val = sysregs.tpidrro_el0;
            result.reg_name = "TPIDRRO_EL0";
            break;
        default:
            result.status = ReferenceSysRegStatus::UnallocatedEncoding;
            return result;
        }

        result.value = val;
        if (rt < 31) {
            cpu.x[rt] = val;
        }
    } else {
        // MSR
        const std::uint64_t val = (rt < 31) ? cpu.x[rt] : 0ULL;
        result.value = val;
        switch (key) {
        case 0x5A10u: // NZCV
            cpu.nzcv = static_cast<std::uint32_t>(val & 0xF0000000u);
            result.reg_name = "NZCV";
            break;
        case 0x5A11u: // DAIF
            sysregs.daif = static_cast<std::uint8_t>((val >> 6u) & 0xFu);
            result.reg_name = "DAIF";
            break;
        case 0x4212u: // CurrentEL (Read-only!)
            result.status = ReferenceSysRegStatus::UndefinedException;
            return result;
        case 0x4200u: // SPSR_EL1
            sysregs.spsr_el1 = static_cast<std::uint32_t>(val & 0xFFFFFFFFu);
            result.reg_name = "SPSR_EL1";
            break;
        case 0x4201u: // ELR_EL1
            sysregs.elr_el1 = val;
            result.reg_name = "ELR_EL1";
            break;
        case 0x4290u: // ESR_EL1
            sysregs.esr_el1 = static_cast<std::uint32_t>(val & 0xFFFFFFFFu);
            result.reg_name = "ESR_EL1";
            break;
        case 0x4300u: // FAR_EL1
            sysregs.far_el1 = val;
            result.reg_name = "FAR_EL1";
            break;
        case 0x4600u: // VBAR_EL1
            sysregs.vbar_el1 = val;
            result.reg_name = "VBAR_EL1";
            break;
        case 0x4080u: // SCTLR_EL1
            sysregs.sctlr_el1 = val;
            result.reg_name = "SCTLR_EL1";
            break;
        case 0x4102u: // TCR_EL1
            sysregs.tcr_el1 = val;
            result.reg_name = "TCR_EL1";
            break;
        case 0x4100u: // TTBR0_EL1
            sysregs.ttbr0_el1 = val;
            result.reg_name = "TTBR0_EL1";
            break;
        case 0x4101u: // TTBR1_EL1
            sysregs.ttbr1_el1 = val;
            result.reg_name = "TTBR1_EL1";
            break;
        case 0x4510u: // MAIR_EL1
            sysregs.mair_el1 = val;
            result.reg_name = "MAIR_EL1";
            break;
        case 0x4082u: // CPACR_EL1
            sysregs.cpacr_el1 = val;
            result.reg_name = "CPACR_EL1";
            break;
        case 0x5E82u: // TPIDR_EL0
            sysregs.tpidr_el0 = val;
            result.reg_name = "TPIDR_EL0";
            break;
        case 0x4684u: // TPIDR_EL1
            sysregs.tpidr_el1 = val;
            result.reg_name = "TPIDR_EL1";
            break;
        case 0x5E83u: // TPIDRRO_EL0
            if (sysregs.current_el == 0) {
                result.status = ReferenceSysRegStatus::UndefinedException;
                return result;
            }
            sysregs.tpidrro_el0 = val;
            result.reg_name = "TPIDRRO_EL0";
            break;
        default:
            result.status = ReferenceSysRegStatus::UnallocatedEncoding;
            return result;
        }
    }

    cpu.pc += 4;
    cpu.retirement_count++;
    result.status = ReferenceSysRegStatus::Success;
    return result;
}

bool ReferenceSysRegModel::is_fp_simd_enabled(unsigned current_el, std::uint64_t cpacr_el1) noexcept {
    const std::uint32_t fpen = static_cast<std::uint32_t>((cpacr_el1 >> 20u) & 0x3u);
    if (current_el == 0) {
        return fpen == 0b11;
    }
    return (fpen & 0x1u) != 0;
}

} // namespace winios::reference

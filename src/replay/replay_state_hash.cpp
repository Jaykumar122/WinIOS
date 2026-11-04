#include "replay/replay_state_hash.h"
#include "replay/replay_profiler.h"
#include "trace/trace_serializer.h"

#include <array>
#include <cstring>
#include <sstream>

namespace winios::replay {

namespace {

inline std::uint64_t mix64(std::uint64_t h, std::uint64_t v) noexcept {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

} // namespace

void ReplayStateHash::hash_cpu(const CPU& cpu,
                              std::uint64_t& out_regs_hash,
                              std::uint64_t& out_simd_hash,
                              std::uint64_t& out_sysreg_hash) noexcept {
    // 1. Integer Registers + PSTATE
    std::vector<std::uint8_t> int_buf;
    int_buf.reserve(32 * 8 + 8 * 8);

    for (std::size_t i = 0; i < 31; ++i) {
        const std::uint64_t r = cpu.read_register(i);
        std::uint8_t b[8];
        trace::encode_u64_le(b, r);
        int_buf.insert(int_buf.end(), b, b + 8);
    }

    const std::uint64_t pc = cpu.get_program_counter();
    const std::uint64_t sp0 = cpu.get_sp_el0();
    const std::uint64_t sp1 = cpu.get_sp_el1();
    const std::uint64_t sp2 = cpu.get_sp_el2();
    const std::uint64_t sp3 = cpu.get_sp_el3();
    const std::uint64_t pstate = (static_cast<std::uint64_t>(cpu.get_nzcv()) & 0xFu) |
                                 ((static_cast<std::uint64_t>(cpu.get_daif()) & 0xFu) << 4u) |
                                 ((static_cast<std::uint64_t>(cpu.get_current_el()) & 0x3u) << 8u) |
                                 ((static_cast<std::uint64_t>(cpu.get_sp_sel() ? 1 : 0)) << 10u) |
                                 ((static_cast<std::uint64_t>(cpu.get_il() ? 1 : 0)) << 11u) |
                                 ((static_cast<std::uint64_t>(cpu.get_pstate_ss() ? 1 : 0)) << 12u);

    std::uint8_t tmp[8];
    trace::encode_u64_le(tmp, pc); int_buf.insert(int_buf.end(), tmp, tmp + 8);
    trace::encode_u64_le(tmp, sp0); int_buf.insert(int_buf.end(), tmp, tmp + 8);
    trace::encode_u64_le(tmp, sp1); int_buf.insert(int_buf.end(), tmp, tmp + 8);
    trace::encode_u64_le(tmp, sp2); int_buf.insert(int_buf.end(), tmp, tmp + 8);
    trace::encode_u64_le(tmp, sp3); int_buf.insert(int_buf.end(), tmp, tmp + 8);
    trace::encode_u64_le(tmp, pstate); int_buf.insert(int_buf.end(), tmp, tmp + 8);

    const std::uint32_t c1 = trace::calculate_crc32c(int_buf.data(), int_buf.size());
    const std::uint32_t c2 = trace::calculate_crc32c(int_buf.data(), int_buf.size(), c1);
    out_regs_hash = (static_cast<std::uint64_t>(c1) << 32u) | c2;

    // 2. SIMD / FP Registers
    std::vector<std::uint8_t> simd_buf;
    simd_buf.reserve(32 * 16 + 16);
    for (std::size_t i = 0; i < 32; ++i) {
        const auto v = cpu.read_vector_register(i);
        std::uint8_t b[8];
        trace::encode_u64_le(b, v.low);
        simd_buf.insert(simd_buf.end(), b, b + 8);
        trace::encode_u64_le(b, v.high);
        simd_buf.insert(simd_buf.end(), b, b + 8);
    }
    const std::uint64_t fpcr = cpu.get_fpcr();
    const std::uint64_t fpsr = cpu.get_fpsr();
    trace::encode_u64_le(tmp, fpcr); simd_buf.insert(simd_buf.end(), tmp, tmp + 8);
    trace::encode_u64_le(tmp, fpsr); simd_buf.insert(simd_buf.end(), tmp, tmp + 8);

    const std::uint32_t sc1 = trace::calculate_crc32c(simd_buf.data(), simd_buf.size());
    const std::uint32_t sc2 = trace::calculate_crc32c(simd_buf.data(), simd_buf.size(), sc1);
    out_simd_hash = (static_cast<std::uint64_t>(sc1) << 32u) | sc2;

    // 3. System Registers
    std::vector<std::uint8_t> sys_buf;
    sys_buf.reserve(32 * 8);
    auto append_u64 = [&](std::uint64_t val) {
        std::uint8_t b[8];
        trace::encode_u64_le(b, val);
        sys_buf.insert(sys_buf.end(), b, b + 8);
    };

    append_u64(cpu.get_vbar_el1());
    append_u64(cpu.get_vbar_el2());
    append_u64(cpu.get_vbar_el3());
    append_u64(cpu.get_elr_el1());
    append_u64(cpu.get_elr_el2());
    append_u64(cpu.get_elr_el3());
    append_u64(cpu.get_esr_el1());
    append_u64(cpu.get_esr_el2());
    append_u64(cpu.get_esr_el3());
    append_u64(cpu.get_spsr_el1());
    append_u64(cpu.get_spsr_el2());
    append_u64(cpu.get_spsr_el3());
    append_u64(cpu.get_sctlr_el1());
    append_u64(cpu.get_hcr_el2());
    append_u64(cpu.get_scr_el3());
    append_u64(cpu.get_cptr_el3());
    append_u64(cpu.get_mdcr_el3());
    append_u64(cpu.get_mdcr_el2());
    append_u64(cpu.get_ttbr0_el1());
    append_u64(cpu.get_ttbr1_el1());
    append_u64(cpu.get_tcr_el1());
    append_u64(cpu.get_mair_el1());

    const std::uint32_t yc1 = trace::calculate_crc32c(sys_buf.data(), sys_buf.size());
    const std::uint32_t yc2 = trace::calculate_crc32c(sys_buf.data(), sys_buf.size(), yc1);
    out_sysreg_hash = (static_cast<std::uint64_t>(yc1) << 32u) | yc2;
}

std::uint64_t ReplayStateHash::hash_memory(const Memory& memory, ReplayProfile* profile) noexcept {
    if (memory.size() == 0 || memory.raw_data() == nullptr) {
        return 0;
    }
    ReplayProfileScope memory_scope(profile ? &profile->memory_hash : nullptr);
    const std::uint32_t c1 = trace::calculate_crc32c(memory.raw_data(), memory.size());
    const std::uint32_t c2 = trace::calculate_crc32c(memory.raw_data(), memory.size(), c1);
    if (profile != nullptr) {
        ++profile->memory_hash_calls;
        profile->memory_bytes_hashed += static_cast<std::uint64_t>(memory.size()) * 2ULL;
        profile->crc_calls += 2;
        profile->crc_bytes += static_cast<std::uint64_t>(memory.size()) * 2ULL;
    }
    return (static_cast<std::uint64_t>(c1) << 32u) | c2;
}

std::uint64_t ReplayStateHash::hash_timers(const VirtualTime& vt, std::uint16_t core_id) noexcept {
    const auto& phys = vt.get_phys_timer(core_id);
    const auto& virt = vt.get_virt_timer(core_id);
    const auto& hp   = vt.get_cnthp_timer(core_id);

    std::uint8_t buf[64]{};
    trace::encode_u64_le(buf + 0,  phys.cval);
    trace::encode_u64_le(buf + 8,  virt.cval);
    trace::encode_u64_le(buf + 16, hp.cval);
    trace::encode_u64_le(buf + 24, vt.get_physical_counter());
    trace::encode_u64_le(buf + 32, vt.get_virtual_counter(core_id));
    trace::encode_u64_le(buf + 40, vt.get_frequency());

    buf[48] = (phys.enable ? 1 : 0) | ((phys.imask ? 1 : 0) << 1) | ((phys.istatus ? 1 : 0) << 2);
    buf[49] = (virt.enable ? 1 : 0) | ((virt.imask ? 1 : 0) << 1) | ((virt.istatus ? 1 : 0) << 2);
    buf[50] = (hp.enable ? 1 : 0)   | ((hp.imask ? 1 : 0) << 1)   | ((hp.istatus ? 1 : 0) << 2);

    const std::uint32_t c1 = trace::calculate_crc32c(buf, sizeof(buf));
    const std::uint32_t c2 = trace::calculate_crc32c(buf, sizeof(buf), c1);
    return (static_cast<std::uint64_t>(c1) << 32u) | c2;
}

std::uint64_t ReplayStateHash::hash_gic(const GICv3& gic, std::uint16_t core_id) noexcept {
    std::uint8_t buf[32]{};
    trace::encode_u32_le(buf + 0, gic.read_icc_pmr(core_id));
    trace::encode_u32_le(buf + 4, gic.read_icc_bpr1(core_id));
    trace::encode_u32_le(buf + 8, gic.read_icc_ctlr(core_id));
    trace::encode_u32_le(buf + 12, gic.is_enabled(core_id) ? 1 : 0);

    const std::uint32_t c1 = trace::calculate_crc32c(buf, sizeof(buf));
    const std::uint32_t c2 = trace::calculate_crc32c(buf, sizeof(buf), c1);
    return (static_cast<std::uint64_t>(c1) << 32u) | c2;
}

ReplayComponentHashes ReplayStateHash::compute_state_hashes(
    const CPU& cpu,
    const Memory& memory,
    const VirtualTime* vt,
    const GICv3* gic,
    ReplayProfile* profile
) noexcept {
    ReplayComponentHashes h{};
    if (profile != nullptr) {
        ++profile->hash_calls;
    }
    hash_cpu(cpu, h.cpu_registers_hash, h.simd_registers_hash, h.system_registers_hash);
    h.memory_hash = hash_memory(memory, profile);

    const std::uint16_t cid = static_cast<std::uint16_t>(cpu.get_core_id());
    if (vt) {
        h.timer_hash = hash_timers(*vt, cid);
    }
    if (gic) {
        h.gic_hash = hash_gic(*gic, cid);
    }

    std::uint64_t comb = 0xcbf29ce484222325ULL;
    comb = mix64(comb, h.cpu_registers_hash);
    comb = mix64(comb, h.simd_registers_hash);
    comb = mix64(comb, h.system_registers_hash);
    comb = mix64(comb, h.memory_hash);
    comb = mix64(comb, h.timer_hash);
    comb = mix64(comb, h.gic_hash);
    h.combined_hash = comb;

    return h;
}

bool ReplayStateHash::compare_and_diff(
    const ReplayComponentHashes& expected,
    const ReplayComponentHashes& actual,
    std::string& out_diff_description
) {
    if (expected == actual) {
        out_diff_description.clear();
        return true;
    }

    std::ostringstream ss;
    ss << "State hash mismatch detected:\n";
    if (expected.cpu_registers_hash != actual.cpu_registers_hash) {
        ss << "  - CPU Registers: expected 0x" << std::hex << expected.cpu_registers_hash
           << ", actual 0x" << actual.cpu_registers_hash << "\n";
    }
    if (expected.simd_registers_hash != actual.simd_registers_hash) {
        ss << "  - SIMD/FP Registers: expected 0x" << std::hex << expected.simd_registers_hash
           << ", actual 0x" << actual.simd_registers_hash << "\n";
    }
    if (expected.system_registers_hash != actual.system_registers_hash) {
        ss << "  - System Registers: expected 0x" << std::hex << expected.system_registers_hash
           << ", actual 0x" << actual.system_registers_hash << "\n";
    }
    if (expected.memory_hash != actual.memory_hash) {
        ss << "  - Guest Memory: expected 0x" << std::hex << expected.memory_hash
           << ", actual 0x" << actual.memory_hash << "\n";
    }
    if (expected.timer_hash != actual.timer_hash) {
        ss << "  - Timers: expected 0x" << std::hex << expected.timer_hash
           << ", actual 0x" << actual.timer_hash << "\n";
    }
    if (expected.gic_hash != actual.gic_hash) {
        ss << "  - GICv3: expected 0x" << std::hex << expected.gic_hash
           << ", actual 0x" << actual.gic_hash << "\n";
    }

    out_diff_description = ss.str();
    return false;
}

} // namespace winios::replay

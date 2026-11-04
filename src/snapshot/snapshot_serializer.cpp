#include "snapshot/snapshot_serializer.h"
#include "trace/trace_serializer.h"
#include <algorithm>
#include <cstring>

namespace winios::snapshot {

namespace {

bool is_chunk_all_zero(const std::uint8_t* data, std::size_t size) noexcept {
    const auto* words = reinterpret_cast<const std::uint64_t*>(data);
    const std::size_t word_count = size / sizeof(std::uint64_t);
    for (std::size_t i = 0; i < word_count; ++i) {
        if (words[i] != 0ULL) {
            return false;
        }
    }
    for (std::size_t i = word_count * sizeof(std::uint64_t); i < size; ++i) {
        if (data[i] != 0u) {
            return false;
        }
    }
    return true;
}

} // namespace

// =============================================================================
// CPU State Serialization
// =============================================================================

std::vector<std::uint8_t> serialize_cpu_state(const CPU& cpu) {
    constexpr std::size_t kCpuStatePayloadSize = 1128;
    std::vector<std::uint8_t> buffer(kCpuStatePayloadSize, 0u);
    std::uint8_t* dst = buffer.data();

    // 1. Core ID & PC
    trace::encode_u32_le(dst + 0, static_cast<std::uint32_t>(cpu.get_core_id()));
    trace::encode_u32_le(dst + 4, 0u); // reserved
    trace::encode_u64_le(dst + 8, cpu.get_program_counter());

    // 2. GPRs X0..X30 (31 * 8 = 248 bytes)
    std::size_t offset = 16;
    for (std::size_t i = 0; i < 31; ++i) {
        trace::encode_u64_le(dst + offset, cpu.read_register(i));
        offset += 8;
    }

    // 3. Banked Stack Pointers (4 * 8 = 32 bytes)
    trace::encode_u64_le(dst + offset + 0,  cpu.get_sp_el0());
    trace::encode_u64_le(dst + offset + 8,  cpu.get_sp_el1());
    trace::encode_u64_le(dst + offset + 16, cpu.get_sp_el2());
    trace::encode_u64_le(dst + offset + 24, cpu.get_sp_el3());
    offset += 32;

    // 4. PSTATE flags (8 * 4 = 32 bytes)
    trace::encode_u32_le(dst + offset + 0,  static_cast<std::uint32_t>(cpu.get_current_el()));
    trace::encode_u32_le(dst + offset + 4,  cpu.get_sp_sel() ? 1u : 0u);
    trace::encode_u32_le(dst + offset + 8,  cpu.get_nzcv());
    trace::encode_u32_le(dst + offset + 12, cpu.get_daif());
    trace::encode_u32_le(dst + offset + 16, cpu.get_il() ? 1u : 0u);
    trace::encode_u32_le(dst + offset + 20, cpu.get_pstate_ss() ? 1u : 0u);
    trace::encode_u32_le(dst + offset + 24, cpu.architectural_exceptions() ? 1u : 0u);
    trace::encode_u32_le(dst + offset + 28, 0u); // reserved
    offset += 32;

    // 5. SIMD V0..V31 (32 * 16 = 512 bytes) + FPCR/FPSR (8 bytes)
    for (std::size_t i = 0; i < 32; ++i) {
        const auto v = cpu.read_vector_register(i);
        trace::encode_u64_le(dst + offset + 0, v.low);
        trace::encode_u64_le(dst + offset + 8, v.high);
        offset += 16;
    }
    trace::encode_u32_le(dst + offset + 0, cpu.get_fpcr());
    trace::encode_u32_le(dst + offset + 4, cpu.get_fpsr());
    offset += 8;

    // 6. EL1 Sysregs (48 bytes)
    trace::encode_u64_le(dst + offset + 0,  cpu.get_elr_el1());
    trace::encode_u32_le(dst + offset + 8,  cpu.get_spsr_el1());
    trace::encode_u32_le(dst + offset + 12, 0u); // reserved
    trace::encode_u32_le(dst + offset + 16, cpu.get_esr_el1());
    trace::encode_u32_le(dst + offset + 20, 0u); // reserved
    trace::encode_u64_le(dst + offset + 24, cpu.get_far_el1());
    trace::encode_u64_le(dst + offset + 32, cpu.get_vbar_el1());
    trace::encode_u64_le(dst + offset + 40, cpu.get_cntkctl_el1());
    offset += 48;

    // 7. EL2 Sysregs (96 bytes)
    trace::encode_u64_le(dst + offset + 0,  cpu.get_elr_el2());
    trace::encode_u32_le(dst + offset + 8,  cpu.get_spsr_el2());
    trace::encode_u32_le(dst + offset + 12, 0u); // reserved
    trace::encode_u32_le(dst + offset + 16, cpu.get_esr_el2());
    trace::encode_u32_le(dst + offset + 20, 0u); // reserved
    trace::encode_u64_le(dst + offset + 24, cpu.get_far_el2());
    trace::encode_u64_le(dst + offset + 32, cpu.get_hpfar_el2());
    trace::encode_u64_le(dst + offset + 40, cpu.get_vbar_el2());
    trace::encode_u64_le(dst + offset + 48, cpu.get_hcr_el2());
    trace::encode_u64_le(dst + offset + 56, cpu.get_vttbr_el2());
    trace::encode_u64_le(dst + offset + 64, cpu.get_vtcr_el2());
    trace::encode_u64_le(dst + offset + 72, cpu.get_cntvoff_el2());
    trace::encode_u64_le(dst + offset + 80, cpu.get_cnthctl_el2());
    trace::encode_u64_le(dst + offset + 88, cpu.get_mdcr_el2());
    offset += 96;

    // 8. EL3 Sysregs (72 bytes)
    trace::encode_u64_le(dst + offset + 0,  cpu.get_sp_el3());
    trace::encode_u32_le(dst + offset + 8,  cpu.get_spsr_el3());
    trace::encode_u32_le(dst + offset + 12, 0u); // reserved
    trace::encode_u64_le(dst + offset + 16, cpu.get_elr_el3());
    trace::encode_u32_le(dst + offset + 24, cpu.get_esr_el3());
    trace::encode_u32_le(dst + offset + 28, 0u); // reserved
    trace::encode_u64_le(dst + offset + 32, cpu.get_far_el3());
    trace::encode_u64_le(dst + offset + 40, cpu.get_vbar_el3());
    trace::encode_u64_le(dst + offset + 48, cpu.get_scr_el3());
    trace::encode_u64_le(dst + offset + 56, cpu.get_cptr_el3());
    trace::encode_u64_le(dst + offset + 64, cpu.get_mdcr_el3());
    offset += 72;

    // 9. MMU Sysregs (48 bytes)
    trace::encode_u64_le(dst + offset + 0,  cpu.get_sctlr_el1());
    trace::encode_u64_le(dst + offset + 8,  cpu.get_tcr_el1());
    trace::encode_u64_le(dst + offset + 16, cpu.get_ttbr0_el1());
    trace::encode_u64_le(dst + offset + 24, cpu.get_ttbr1_el1());
    trace::encode_u64_le(dst + offset + 32, cpu.get_mair_el1());
    trace::encode_u64_le(dst + offset + 40, cpu.get_par_el1());
    offset += 48;

    // 10. Lifecycle State (8 bytes)
    dst[offset + 0] = cpu.is_halted() ? 1u : 0u;
    dst[offset + 1] = cpu.is_waiting() ? 1u : 0u;
    dst[offset + 2] = 0u; // waiting_for_event
    dst[offset + 3] = cpu.get_event_register() ? 1u : 0u;
    dst[offset + 4] = cpu.is_started() ? 1u : 0u;
    dst[offset + 5] = cpu.is_stopped() ? 1u : 0u;
    dst[offset + 6] = 0u; // padding
    dst[offset + 7] = 0u; // padding
    offset += 8;

    // 11. Trace Sequence (8 bytes)
    trace::encode_u64_le(dst + offset, cpu.get_instruction_trace_sequence());
    offset += 8;

    return buffer;
}

SnapshotResult deserialize_cpu_state(const std::uint8_t* src, std::size_t size, CPU& cpu) {
    constexpr std::size_t kCpuStatePayloadSize = 1128;
    if (!src || size < kCpuStatePayloadSize) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "CPU state buffer truncated");
    }

    // 1. Core ID & PC
    const auto core_id = trace::decode_u32_le(src + 0);
    cpu.set_core_id(core_id);
    cpu.set_program_counter(trace::decode_u64_le(src + 8));

    // 2. GPRs
    std::size_t offset = 16;
    for (std::size_t i = 0; i < 31; ++i) {
        cpu.write_register(i, trace::decode_u64_le(src + offset));
        offset += 8;
    }

    // 3. Banked SPs
    cpu.set_sp_el0(trace::decode_u64_le(src + offset + 0));
    cpu.set_sp_el1(trace::decode_u64_le(src + offset + 8));
    cpu.set_sp_el2(trace::decode_u64_le(src + offset + 16));
    cpu.set_sp_el3(trace::decode_u64_le(src + offset + 24));
    offset += 32;

    // 4. PSTATE flags
    cpu.set_current_el(trace::decode_u32_le(src + offset + 0));
    cpu.set_sp_sel(trace::decode_u32_le(src + offset + 4) != 0);
    cpu.set_nzcv(static_cast<std::uint8_t>(trace::decode_u32_le(src + offset + 8)));
    cpu.set_daif(static_cast<std::uint8_t>(trace::decode_u32_le(src + offset + 12)));
    cpu.set_il(trace::decode_u32_le(src + offset + 16) != 0);
    cpu.set_pstate_ss(trace::decode_u32_le(src + offset + 20) != 0);
    cpu.set_architectural_exceptions(trace::decode_u32_le(src + offset + 24) != 0);
    offset += 32;

    // 5. SIMD V0..V31 & FPCR/FPSR
    for (std::size_t i = 0; i < 32; ++i) {
        CPU::VectorRegister v{};
        v.low = trace::decode_u64_le(src + offset + 0);
        v.high = trace::decode_u64_le(src + offset + 8);
        cpu.write_vector_register(i, v);
        offset += 16;
    }
    cpu.set_fpcr(trace::decode_u32_le(src + offset + 0));
    cpu.set_fpsr(trace::decode_u32_le(src + offset + 4));
    offset += 8;

    // 6. EL1 Sysregs
    cpu.set_elr_el1(trace::decode_u64_le(src + offset + 0));
    cpu.set_spsr_el1(trace::decode_u32_le(src + offset + 8));
    cpu.set_esr_el1(trace::decode_u32_le(src + offset + 16));
    cpu.set_far_el1(trace::decode_u64_le(src + offset + 24));
    cpu.set_vbar_el1(trace::decode_u64_le(src + offset + 32));
    cpu.set_cntkctl_el1(trace::decode_u64_le(src + offset + 40));
    offset += 48;

    // 7. EL2 Sysregs
    cpu.set_elr_el2(trace::decode_u64_le(src + offset + 0));
    cpu.set_spsr_el2(trace::decode_u32_le(src + offset + 8));
    cpu.set_esr_el2(trace::decode_u32_le(src + offset + 16));
    cpu.set_far_el2(trace::decode_u64_le(src + offset + 24));
    cpu.set_hpfar_el2(trace::decode_u64_le(src + offset + 32));
    cpu.set_vbar_el2(trace::decode_u64_le(src + offset + 40));
    cpu.set_hcr_el2(trace::decode_u64_le(src + offset + 48));
    cpu.set_vttbr_el2(trace::decode_u64_le(src + offset + 56));
    cpu.set_vtcr_el2(trace::decode_u64_le(src + offset + 64));
    cpu.set_cntvoff_el2(trace::decode_u64_le(src + offset + 72));
    cpu.set_cnthctl_el2(trace::decode_u64_le(src + offset + 80));
    cpu.set_mdcr_el2(trace::decode_u64_le(src + offset + 88));
    offset += 96;

    // 8. EL3 Sysregs
    cpu.set_sp_el3(trace::decode_u64_le(src + offset + 0));
    cpu.set_spsr_el3(trace::decode_u32_le(src + offset + 8));
    cpu.set_elr_el3(trace::decode_u64_le(src + offset + 16));
    cpu.set_esr_el3(trace::decode_u32_le(src + offset + 24));
    cpu.set_far_el3(trace::decode_u64_le(src + offset + 32));
    cpu.set_vbar_el3(trace::decode_u64_le(src + offset + 40));
    cpu.set_scr_el3(trace::decode_u64_le(src + offset + 48));
    cpu.set_cptr_el3(trace::decode_u64_le(src + offset + 56));
    cpu.set_mdcr_el3(trace::decode_u64_le(src + offset + 64));
    offset += 72;

    // 9. MMU Sysregs
    cpu.set_sctlr_el1(trace::decode_u64_le(src + offset + 0));
    cpu.set_tcr_el1(trace::decode_u64_le(src + offset + 8));
    cpu.set_ttbr0_el1(trace::decode_u64_le(src + offset + 16));
    cpu.set_ttbr1_el1(trace::decode_u64_le(src + offset + 24));
    cpu.set_mair_el1(trace::decode_u64_le(src + offset + 32));
    cpu.set_par_el1(trace::decode_u64_le(src + offset + 40));
    offset += 48;

    // 10. Lifecycle
    cpu.set_halted(src[offset + 0] != 0u);
    cpu.set_waiting_for_interrupt(src[offset + 1] != 0u);
    cpu.set_waiting_for_event(src[offset + 2] != 0u);
    cpu.set_event_register(src[offset + 3] != 0u);
    cpu.set_started(src[offset + 4] != 0u);
    cpu.set_stopped(src[offset + 5] != 0u);
    offset += 8;

    // 11. Trace Sequence
    cpu.set_instruction_trace_sequence(trace::decode_u64_le(src + offset));
    offset += 8;

    // Architectural policy: flush TLB on restore
    cpu.get_mmu().invalidate_tlb_all();
    cpu.clear_exclusive_monitor();

    return SnapshotResult::ok();
}

// =============================================================================
// Memory State Serialization (Chunked Sparse Serialization)
// =============================================================================

std::vector<std::uint8_t> serialize_memory_state(const Memory& memory, std::size_t chunk_size) {
    if (chunk_size == 0) {
        chunk_size = kMemoryChunkSizeBytes;
    }

    const std::size_t total_bytes = memory.size();
    const std::uint32_t total_chunks = static_cast<std::uint32_t>((total_bytes + chunk_size - 1u) / chunk_size);
    const std::uint8_t* raw_mem = memory.raw_data();

    // Pass 1: Identify non-zero chunks
    struct ChunkInfo {
        std::uint32_t index;
        std::uint32_t offset;
        std::uint32_t size;
        std::uint32_t crc32c;
    };
    std::vector<ChunkInfo> non_zero_chunks;
    non_zero_chunks.reserve(total_chunks);

    for (std::uint32_t i = 0; i < total_chunks; ++i) {
        const std::size_t chunk_offset = static_cast<std::size_t>(i) * chunk_size;
        const std::size_t cur_chunk_size = std::min(chunk_size, total_bytes - chunk_offset);

        if (!is_chunk_all_zero(raw_mem + chunk_offset, cur_chunk_size)) {
            const std::uint32_t crc = trace::calculate_crc32c(raw_mem + chunk_offset, cur_chunk_size);
            non_zero_chunks.push_back(ChunkInfo{
                i,
                static_cast<std::uint32_t>(chunk_offset),
                static_cast<std::uint32_t>(cur_chunk_size),
                crc
            });
        }
    }

    // Pass 2: Serialize header + chunk records
    // Header size = 32 bytes
    // Per non-zero chunk = 16 bytes descriptor + payload bytes
    std::size_t total_payload_size = 32;
    for (const auto& ch : non_zero_chunks) {
        total_payload_size += 16 + ch.size;
    }

    std::vector<std::uint8_t> buffer(total_payload_size);
    std::uint8_t* dst = buffer.data();

    // Section Header (32 bytes)
    trace::encode_u64_le(dst + 0, 0ULL); // base_physical_address
    trace::encode_u64_le(dst + 8, static_cast<std::uint64_t>(total_bytes));
    trace::encode_u32_le(dst + 16, static_cast<std::uint32_t>(chunk_size));
    trace::encode_u32_le(dst + 20, total_chunks);
    trace::encode_u32_le(dst + 24, static_cast<std::uint32_t>(non_zero_chunks.size()));
    trace::encode_u32_le(dst + 28, 0u); // flags

    std::size_t write_pos = 32;
    for (const auto& ch : non_zero_chunks) {
        trace::encode_u32_le(dst + write_pos + 0,  ch.index);
        trace::encode_u32_le(dst + write_pos + 4,  ch.size);
        trace::encode_u32_le(dst + write_pos + 8,  ch.crc32c);
        trace::encode_u32_le(dst + write_pos + 12, 0u); // flags
        write_pos += 16;

        std::memcpy(dst + write_pos, raw_mem + ch.offset, ch.size);
        write_pos += ch.size;
    }

    return buffer;
}

SnapshotResult deserialize_memory_state(const std::uint8_t* src, std::size_t size, Memory& memory) {
    if (!src || size < 32) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Memory section header truncated");
    }

    const std::uint64_t total_memory_bytes = trace::decode_u64_le(src + 8);
    const std::uint32_t chunk_size = trace::decode_u32_le(src + 16);
    const std::uint32_t total_chunks = trace::decode_u32_le(src + 20);
    const std::uint32_t non_zero_count = trace::decode_u32_le(src + 24);
    (void)total_chunks;

    if (total_memory_bytes != memory.size()) {
        return SnapshotResult::fail(SnapshotErrorCode::MemoryCapacityMismatch, "Memory capacity mismatch between snapshot and instance");
    }
    if (chunk_size == 0) {
        return SnapshotResult::fail(SnapshotErrorCode::InvalidStateData, "Invalid memory chunk size 0");
    }

    // Reset guest memory to clean zero state
    memory.reset_zero();
    std::uint8_t* raw_mem = memory.raw_data();

    std::size_t read_pos = 32;
    for (std::uint32_t i = 0; i < non_zero_count; ++i) {
        if (read_pos + 16 > size) {
            return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Truncated chunk descriptor");
        }

        const std::uint32_t chunk_index = trace::decode_u32_le(src + read_pos + 0);
        const std::uint32_t chunk_len   = trace::decode_u32_le(src + read_pos + 4);
        const std::uint32_t stored_crc  = trace::decode_u32_le(src + read_pos + 8);
        read_pos += 16;

        if (read_pos + chunk_len > size) {
            return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Truncated chunk payload data");
        }

        const std::size_t dest_offset = static_cast<std::size_t>(chunk_index) * chunk_size;
        if (dest_offset + chunk_len > total_memory_bytes) {
            return SnapshotResult::fail(SnapshotErrorCode::OversizedSection, "Memory chunk offset out of range");
        }

        const std::uint32_t computed_crc = trace::calculate_crc32c(src + read_pos, chunk_len);
        if (computed_crc != stored_crc) {
            return SnapshotResult::fail(SnapshotErrorCode::CorruptedChunk, "Memory chunk CRC32C verification failed");
        }

        std::memcpy(raw_mem + dest_offset, src + read_pos, chunk_len);
        read_pos += chunk_len;
    }

    return SnapshotResult::ok();
}

// =============================================================================
// VirtualTime & Timer State Serialization
// =============================================================================

std::vector<std::uint8_t> serialize_timer_state(const VirtualTime& vt, std::size_t core_count) {
    if (core_count == 0) core_count = 1;
    // Header: 24 bytes
    // Per-core: 64 bytes
    const std::size_t total_size = 24 + core_count * 64;
    std::vector<std::uint8_t> buffer(total_size, 0u);
    std::uint8_t* dst = buffer.data();

    // 1. System-wide counter & frequency
    trace::encode_u64_le(dst + 0,  vt.get_physical_counter());
    trace::encode_u64_le(dst + 8,  vt.get_frequency());
    trace::encode_u32_le(dst + 16, static_cast<std::uint32_t>(core_count));
    trace::encode_u32_le(dst + 20, 0u); // reserved

    std::size_t offset = 24;
    for (std::size_t c = 0; c < core_count; ++c) {
        const auto& phys = vt.get_phys_timer(c);
        const auto& virt = vt.get_virt_timer(c);
        const auto& cnthp = vt.get_cnthp_timer(c);

        trace::encode_u32_le(dst + offset + 0, static_cast<std::uint32_t>(c));
        dst[offset + 4] = phys.enable ? 1u : 0u;
        dst[offset + 5] = phys.imask ? 1u : 0u;
        dst[offset + 6] = phys.istatus ? 1u : 0u;
        dst[offset + 7] = 0u;
        trace::encode_u64_le(dst + offset + 8, phys.cval);

        dst[offset + 16] = virt.enable ? 1u : 0u;
        dst[offset + 17] = virt.imask ? 1u : 0u;
        dst[offset + 18] = virt.istatus ? 1u : 0u;
        dst[offset + 19] = 0u;
        trace::encode_u64_le(dst + offset + 20, virt.cval);

        dst[offset + 28] = cnthp.enable ? 1u : 0u;
        dst[offset + 29] = cnthp.imask ? 1u : 0u;
        dst[offset + 30] = cnthp.istatus ? 1u : 0u;
        dst[offset + 31] = 0u;
        trace::encode_u64_le(dst + offset + 32, cnthp.cval);

        trace::encode_u64_le(dst + offset + 40, vt.get_cntvoff_el2(c));
        trace::encode_u64_le(dst + offset + 48, vt.get_cntkctl_el1(c));
        trace::encode_u32_le(dst + offset + 56, static_cast<std::uint32_t>(vt.get_cnthctl_el2(c)));
        dst[offset + 60] = vt.is_core_offline(c) ? 1u : 0u;
        dst[offset + 61] = 0u;
        dst[offset + 62] = 0u;
        dst[offset + 63] = 0u;

        offset += 64;
    }

    return buffer;
}

SnapshotResult deserialize_timer_state(const std::uint8_t* src, std::size_t size, VirtualTime& vt) {
    if (!src || size < 24) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Timer section header truncated");
    }

    const std::uint64_t phys_cnt   = trace::decode_u64_le(src + 0);
    const std::uint64_t freq       = trace::decode_u64_le(src + 8);
    const std::uint32_t core_count = trace::decode_u32_le(src + 16);

    if (size < 24 + static_cast<std::size_t>(core_count) * 64) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Timer core state truncated");
    }

    vt.set_frequency(freq);
    vt.set_physical_counter(phys_cnt);

    std::size_t offset = 24;
    for (std::uint32_t i = 0; i < core_count; ++i) {
        const auto c = trace::decode_u32_le(src + offset + 0);

        TimerState phys{};
        phys.enable  = (src[offset + 4] != 0u);
        phys.imask   = (src[offset + 5] != 0u);
        phys.istatus = (src[offset + 6] != 0u);
        phys.cval    = trace::decode_u64_le(src + offset + 8);
        vt.set_phys_timer(c, phys);

        TimerState virt{};
        virt.enable  = (src[offset + 16] != 0u);
        virt.imask   = (src[offset + 17] != 0u);
        virt.istatus = (src[offset + 18] != 0u);
        virt.cval    = trace::decode_u64_le(src + offset + 20);
        vt.set_virt_timer(c, virt);

        TimerState cnthp{};
        cnthp.enable  = (src[offset + 28] != 0u);
        cnthp.imask   = (src[offset + 29] != 0u);
        cnthp.istatus = (src[offset + 30] != 0u);
        cnthp.cval    = trace::decode_u64_le(src + offset + 32);
        vt.set_cnthp_timer(c, cnthp);

        vt.set_cntvoff_el2(c, trace::decode_u64_le(src + offset + 40));
        vt.set_cntkctl_el1(c, trace::decode_u64_le(src + offset + 48));
        vt.set_cnthctl_el2(c, trace::decode_u32_le(src + offset + 56));
        vt.set_core_offline(c, src[offset + 60] != 0u);

        offset += 64;
    }

    vt.evaluate_all_timers();
    return SnapshotResult::ok();
}

// =============================================================================
// GIC State Serialization
// =============================================================================

std::vector<std::uint8_t> serialize_gic_state(const GICv3& gic) {
    const std::size_t num_cores = gic.get_core_count();
    const auto dist = gic.get_distributor_state();

    // Distributor fixed size:
    // 16 (header) + 5*16 (bitmasks) + 128 (priorities) + 32 (cfg) + 16 (level) + 768 (router) = 1056 bytes
    // Per-core fixed:
    // 8 (core header) + 76 (RD) + 24 (ICC) + 56 (ICH) = 164 bytes
    // Plus active priority vectors
    std::size_t estimated_size = 1056 + num_cores * 256;
    std::vector<std::uint8_t> buffer;
    buffer.reserve(estimated_size);

    auto write_u32 = [&](std::uint32_t val) {
        std::uint8_t b[4];
        trace::encode_u32_le(b, val);
        buffer.insert(buffer.end(), b, b + 4);
    };
    auto write_u64 = [&](std::uint64_t val) {
        std::uint8_t b[8];
        trace::encode_u64_le(b, val);
        buffer.insert(buffer.end(), b, b + 8);
    };

    // 1. Distributor Header
    write_u32(static_cast<std::uint32_t>(num_cores));
    write_u32(dist.gicd_ctlr);
    write_u32(dist.gicd_typer);
    write_u32(0u); // reserved

    // Bitmasks
    for (int i = 0; i < 4; ++i) write_u32(dist.gicd_igroupr[i]);
    for (int i = 0; i < 4; ++i) write_u32(dist.gicd_igrpmodr[i]);
    for (int i = 0; i < 4; ++i) write_u32(dist.gicd_isenabler[i]);
    for (int i = 0; i < 4; ++i) write_u32(dist.gicd_ispendr[i]);
    for (int i = 0; i < 4; ++i) write_u32(dist.gicd_isactiver[i]);

    // Priorities (128 bytes)
    buffer.insert(buffer.end(), dist.gicd_ipriorityr, dist.gicd_ipriorityr + 128);

    // Config & Levels
    for (int i = 0; i < 8; ++i) write_u32(dist.gicd_icfgr[i]);
    for (int i = 0; i < 4; ++i) write_u32(dist.gicd_level[i]);
    for (int i = 0; i < 96; ++i) write_u64(dist.gicd_irouter[i]);

    // 2. Per-Core Redistributor & CPU Interface
    for (std::size_t c = 0; c < num_cores; ++c) {
        const auto& rd = gic.get_redistributor(c);
        const auto& icc = gic.get_cpu_interface(c);

        write_u32(static_cast<std::uint32_t>(c));
        write_u32(0u); // reserved

        // RD registers
        write_u32(rd.ctlr);
        write_u32(rd.waker);
        write_u32(rd.igroupr0);
        write_u32(rd.igrpmodr0);
        write_u32(rd.isenabler0);
        write_u32(rd.ispendr0);
        write_u32(rd.isactiver0);
        buffer.insert(buffer.end(), rd.ipriorityr, rd.ipriorityr + 32);
        write_u32(rd.icfgr0);
        write_u32(rd.icfgr1);
        write_u32(rd.level0);

        // ICC registers
        buffer.push_back(icc.icc_pmr_el1);
        buffer.push_back(icc.icc_bpr0_el1);
        buffer.push_back(icc.icc_bpr1_el1);
        buffer.push_back(0u); // pad
        write_u32(icc.icc_ctlr_el1);
        write_u32(icc.icc_igrpen0_el1);
        write_u32(icc.icc_igrpen1_el1);
        write_u32(icc.icc_sre_el1);
        write_u32(icc.icc_sre_el2);
        write_u32(icc.gic_enabled.load() ? 1u : 0u);

        // ICH registers
        write_u32(icc.ich_hcr_el2);
        write_u32(0u); // pad
        write_u64(icc.ich_vtr_el2);
        write_u64(icc.ich_vmcr_el2);
        for (int i = 0; i < 4; ++i) write_u64(icc.ich_lr_el2[i]);

        // Active Priorities vector
        write_u32(static_cast<std::uint32_t>(icc.active_priorities.size()));
        buffer.insert(buffer.end(), icc.active_priorities.begin(), icc.active_priorities.end());
        // Align to 4
        while (buffer.size() % 4 != 0) buffer.push_back(0u);

        // Active Virt Priorities vector
        write_u32(static_cast<std::uint32_t>(icc.active_virt_priorities.size()));
        buffer.insert(buffer.end(), icc.active_virt_priorities.begin(), icc.active_virt_priorities.end());
        while (buffer.size() % 4 != 0) buffer.push_back(0u);
    }

    return buffer;
}

SnapshotResult deserialize_gic_state(const std::uint8_t* src, std::size_t size, GICv3& gic) {
    if (!src || size < 1056) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "GIC section distributor truncated");
    }

    std::size_t offset = 0;
    auto read_u32 = [&]() {
        const auto val = trace::decode_u32_le(src + offset);
        offset += 4;
        return val;
    };
    auto read_u64 = [&]() {
        const auto val = trace::decode_u64_le(src + offset);
        offset += 8;
        return val;
    };

    const std::uint32_t num_cores = read_u32();
    gic.set_core_count(num_cores);

    GICv3::DistributorState dist{};
    dist.gicd_ctlr = read_u32();
    dist.gicd_typer = read_u32();
    (void)read_u32(); // reserved

    for (int i = 0; i < 4; ++i) dist.gicd_igroupr[i] = read_u32();
    for (int i = 0; i < 4; ++i) dist.gicd_igrpmodr[i] = read_u32();
    for (int i = 0; i < 4; ++i) dist.gicd_isenabler[i] = read_u32();
    for (int i = 0; i < 4; ++i) dist.gicd_ispendr[i] = read_u32();
    for (int i = 0; i < 4; ++i) dist.gicd_isactiver[i] = read_u32();

    std::memcpy(dist.gicd_ipriorityr, src + offset, 128);
    offset += 128;

    for (int i = 0; i < 8; ++i) dist.gicd_icfgr[i] = read_u32();
    for (int i = 0; i < 4; ++i) dist.gicd_level[i] = read_u32();
    for (int i = 0; i < 96; ++i) dist.gicd_irouter[i] = read_u64();

    gic.set_distributor_state(dist);

    // Per-core restoration
    for (std::uint32_t c = 0; c < num_cores; ++c) {
        if (offset + 160 > size) {
            return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "GIC core state truncated");
        }

        const auto core_id = read_u32();
        (void)read_u32(); // reserved

        auto& rd = gic.get_redistributor(core_id);
        auto& icc = gic.get_cpu_interface(core_id);

        rd.ctlr = read_u32();
        rd.waker = read_u32();
        rd.igroupr0 = read_u32();
        rd.igrpmodr0 = read_u32();
        rd.isenabler0 = read_u32();
        rd.ispendr0 = read_u32();
        rd.isactiver0 = read_u32();
        std::memcpy(rd.ipriorityr, src + offset, 32);
        offset += 32;
        rd.icfgr0 = read_u32();
        rd.icfgr1 = read_u32();
        rd.level0 = read_u32();

        icc.icc_pmr_el1 = src[offset + 0];
        icc.icc_bpr0_el1 = src[offset + 1];
        icc.icc_bpr1_el1 = src[offset + 2];
        offset += 4;
        icc.icc_ctlr_el1 = read_u32();
        icc.icc_igrpen0_el1 = read_u32();
        icc.icc_igrpen1_el1 = read_u32();
        icc.icc_sre_el1 = read_u32();
        icc.icc_sre_el2 = read_u32();
        icc.gic_enabled.store(read_u32() != 0);

        icc.ich_hcr_el2 = read_u32();
        (void)read_u32();
        icc.ich_vtr_el2 = read_u64();
        icc.ich_vmcr_el2 = read_u64();
        for (int i = 0; i < 4; ++i) icc.ich_lr_el2[i] = read_u64();

        const std::uint32_t prio_count = read_u32();
        if (offset + prio_count > size) {
            return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "GIC priority stack truncated");
        }
        icc.active_priorities.assign(src + offset, src + offset + prio_count);
        offset += prio_count;
        while (offset % 4 != 0) offset++;

        const std::uint32_t virt_prio_count = read_u32();
        if (offset + virt_prio_count > size) {
            return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "GIC virt priority stack truncated");
        }
        icc.active_virt_priorities.assign(src + offset, src + offset + virt_prio_count);
        offset += virt_prio_count;
        while (offset % 4 != 0) offset++;
    }

    return SnapshotResult::ok();
}

// =============================================================================
// PMU State Serialization
// =============================================================================

std::vector<std::uint8_t> serialize_pmu_state(const PMU& pmu, std::size_t core_id) {
    constexpr std::size_t kPmuSize = 96;
    std::vector<std::uint8_t> buffer(kPmuSize, 0u);
    std::uint8_t* dst = buffer.data();

    trace::encode_u32_le(dst + 0, static_cast<std::uint32_t>(core_id));
    trace::encode_u32_le(dst + 4, pmu.read_pmcr());
    trace::encode_u32_le(dst + 8, pmu.read_pmcntenset());
    trace::encode_u32_le(dst + 12, pmu.read_pmovsset());
    trace::encode_u32_le(dst + 16, pmu.read_pmintenset());
    trace::encode_u32_le(dst + 20, pmu.read_pmselr());
    trace::encode_u32_le(dst + 24, pmu.read_pmuserenr());
    trace::encode_u32_le(dst + 28, 0u);

    trace::encode_u64_le(dst + 32, pmu.read_pmccntr());
    trace::encode_u32_le(dst + 40, pmu.read_pmccfiltr());
    trace::encode_u32_le(dst + 44, 0u);

    for (std::size_t i = 0; i < 6; ++i) {
        trace::encode_u32_le(dst + 48 + i * 4, pmu.read_pmevcntr(i));
        trace::encode_u32_le(dst + 72 + i * 4, pmu.read_pmevtyper(i));
    }

    return buffer;
}

SnapshotResult deserialize_pmu_state(const std::uint8_t* src, std::size_t size, PMU& pmu) {
    constexpr std::size_t kPmuSize = 96;
    if (!src || size < kPmuSize) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "PMU state truncated");
    }

    pmu.write_pmcr(trace::decode_u32_le(src + 4));
    pmu.write_pmcntenset(trace::decode_u32_le(src + 8));
    pmu.write_pmovsset(trace::decode_u32_le(src + 12));
    pmu.write_pmintenset(trace::decode_u32_le(src + 16));
    pmu.write_pmselr(trace::decode_u32_le(src + 20));
    pmu.write_pmuserenr(trace::decode_u32_le(src + 24));

    pmu.write_pmccntr(trace::decode_u64_le(src + 32));
    pmu.write_pmccfiltr(trace::decode_u32_le(src + 40));

    for (std::size_t i = 0; i < 6; ++i) {
        pmu.write_pmevcntr(i, trace::decode_u32_le(src + 48 + i * 4));
        pmu.write_pmevtyper(i, trace::decode_u32_le(src + 72 + i * 4));
    }

    return SnapshotResult::ok();
}

// =============================================================================
// Debug Architecture State Serialization
// =============================================================================

std::vector<std::uint8_t> serialize_debug_state(const DebugArchitecture& dbg, std::size_t core_id) {
    // Header (16 bytes: core_id, pad, mdscr_el1)
    // 4 Breakpoints: 4 * 16 = 64 bytes
    // 4 Watchpoints: 4 * 16 = 64 bytes
    // Total = 144 bytes
    constexpr std::size_t kDebugSize = 144;
    std::vector<std::uint8_t> buffer(kDebugSize, 0u);
    std::uint8_t* dst = buffer.data();

    trace::encode_u32_le(dst + 0, static_cast<std::uint32_t>(core_id));
    trace::encode_u32_le(dst + 4, 0u);
    trace::encode_u64_le(dst + 8, dbg.get_mdscr_el1());

    std::size_t offset = 16;
    for (std::size_t i = 0; i < 4; ++i) {
        trace::encode_u64_le(dst + offset + 0, dbg.get_dbgbvr(i));
        trace::encode_u32_le(dst + offset + 8, dbg.get_dbgbcr(i));
        trace::encode_u32_le(dst + offset + 12, 0u);
        offset += 16;
    }

    for (std::size_t i = 0; i < 4; ++i) {
        trace::encode_u64_le(dst + offset + 0, dbg.get_dbgwvr(i));
        trace::encode_u32_le(dst + offset + 8, dbg.get_dbgwcr(i));
        trace::encode_u32_le(dst + offset + 12, 0u);
        offset += 16;
    }

    return buffer;
}

SnapshotResult deserialize_debug_state(const std::uint8_t* src, std::size_t size, DebugArchitecture& dbg) {
    constexpr std::size_t kDebugSize = 144;
    if (!src || size < kDebugSize) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Debug state truncated");
    }

    dbg.set_mdscr_el1(trace::decode_u64_le(src + 8));

    std::size_t offset = 16;
    for (std::size_t i = 0; i < 4; ++i) {
        dbg.set_dbgbvr(i, trace::decode_u64_le(src + offset + 0));
        dbg.set_dbgbcr(i, trace::decode_u32_le(src + offset + 8));
        offset += 16;
    }

    for (std::size_t i = 0; i < 4; ++i) {
        dbg.set_dbgwvr(i, trace::decode_u64_le(src + offset + 0));
        dbg.set_dbgwcr(i, trace::decode_u32_le(src + offset + 8));
        offset += 16;
    }

    return SnapshotResult::ok();
}

// =============================================================================
// Metadata Serialization
// =============================================================================

std::vector<std::uint8_t> serialize_metadata(const std::string& description, const std::string& creator) {
    std::vector<std::uint8_t> buffer;
    auto write_str = [&](const std::string& s) {
        std::uint8_t len_buf[4];
        trace::encode_u32_le(len_buf, static_cast<std::uint32_t>(s.size()));
        buffer.insert(buffer.end(), len_buf, len_buf + 4);
        buffer.insert(buffer.end(), s.begin(), s.end());
    };

    write_str(description);
    write_str(creator);
    return buffer;
}

SnapshotResult deserialize_metadata(const std::uint8_t* src, std::size_t size, std::string& out_description, std::string& out_creator) {
    if (!src || size < 8) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Metadata section truncated");
    }

    std::size_t offset = 0;
    auto read_str = [&](std::string& out) -> bool {
        if (offset + 4 > size) return false;
        const auto len = trace::decode_u32_le(src + offset);
        offset += 4;
        if (offset + len > size) return false;
        out.assign(reinterpret_cast<const char*>(src + offset), len);
        offset += len;
        return true;
    };

    if (!read_str(out_description) || !read_str(out_creator)) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Metadata strings truncated");
    }
    return SnapshotResult::ok();
}

} // namespace winios::snapshot

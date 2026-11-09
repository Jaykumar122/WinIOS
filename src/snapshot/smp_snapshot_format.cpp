#include "snapshot/smp_snapshot_format.h"
#include "trace/trace_serializer.h"

#include <algorithm>
#include <cstring>

namespace winios::snapshot {

namespace {

inline bool is_chunk_zero(const std::uint8_t* ptr, std::size_t size) noexcept {
    const auto* u64_ptr = reinterpret_cast<const std::uint64_t*>(ptr);
    const std::size_t u64_count = size / sizeof(std::uint64_t);
    for (std::size_t i = 0; i < u64_count; ++i) {
        if (u64_ptr[i] != 0) return false;
    }
    for (std::size_t i = u64_count * sizeof(std::uint64_t); i < size; ++i) {
        if (ptr[i] != 0) return false;
    }
    return true;
}

} // namespace

// -----------------------------------------------------------------------------
// 1. Core Directory Serialization
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> serialize_core_directory(const std::vector<CoreDirectoryEntry>& entries) {
    const std::size_t total_size = 4 + entries.size() * 16;
    std::vector<std::uint8_t> buf(total_size);
    std::uint8_t* p = buf.data();

    trace::encode_u32_le(p, static_cast<std::uint32_t>(entries.size()));
    p += 4;

    for (const auto& entry : entries) {
        trace::encode_u32_le(p + 0, entry.core_id);
        trace::encode_u32_le(p + 4, static_cast<std::uint32_t>(entry.lifecycle));
        trace::encode_u32_le(p + 8, entry.active ? 1u : 0u);
        trace::encode_u32_le(p + 12, 0u); // reserved
        p += 16;
    }
    return buf;
}

SnapshotResult deserialize_core_directory(const std::uint8_t* src, std::size_t size,
                                          std::vector<CoreDirectoryEntry>& out_entries) {
    out_entries.clear();
    if (!src || size < 4) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for CoreDirectory");
    }

    const std::uint32_t count = trace::decode_u32_le(src);
    const std::size_t expected_size = 4 + static_cast<std::size_t>(count) * 16;
    if (size < expected_size) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer truncated for CoreDirectory entries");
    }

    const std::uint8_t* p = src + 4;
    out_entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        CoreDirectoryEntry entry;
        entry.core_id = trace::decode_u32_le(p + 0);
        entry.lifecycle = static_cast<CoreLifecycleState>(trace::decode_u32_le(p + 4));
        entry.active = (trace::decode_u32_le(p + 8) != 0);
        p += 16;
        out_entries.push_back(entry);
    }
    return SnapshotResult::ok();
}

// -----------------------------------------------------------------------------
// 2. Core Architectural State Serialization
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> serialize_core_architectural_state(const SmpCoreState& state) {
    // Exact layout size:
    // Metadata: 4 (core_id) + 4 (lifecycle) + 8 (retire) + 8 (budget) + 8 (exec) + 4 (irq) + 4 (res) = 40
    // GPRs: 31 * 8 (X0..30) = 248
    // PC, SPs: 8 (PC) + 4 * 8 (SP_EL0..3) = 40
    // PSTATE: 1 (nzcv) + 1 (daif) + 1 (sp_sel) + 1 (il) + 1 (pstate_ss) + 1 (current_el) + 2 (pad) = 8
    // SIMD: 32 * 16 = 512, fpcr: 4, fpsr: 4 = 520
    // EL1-3 regs:
    //   EL1: 8 + 4 + 4 + 8 + 8 = 32
    //   EL2: 8 + 4 + 4 + 8 + 8 + 8 = 40
    //   EL3: 8 + 4 + 4 + 8 + 8 + 8 = 40
    // Sysregs: 7 * 8 = 56
    // Timers: 8 + 4 + 4 + 8 + 4 + 4 = 32
    // Thread pointers: 3 * 8 = 24
    // Legacy total: 40 + 248 + 40 + 8 + 520 + 32 + 40 + 40 + 56 + 32 + 24 = 1080 bytes
    // Stage 39 debug block: 8 (MDSCR) + 8 (MDCCINT) + 4*16 (breakpoints) + 4*16 (watchpoints) = 144 bytes
    // Total: 1224 bytes
    constexpr std::size_t kPayloadSize = kCoreArchPayloadSize;
    std::vector<std::uint8_t> buf(kPayloadSize, 0u);
    std::uint8_t* p = buf.data();

    trace::encode_u32_le(p + 0, static_cast<std::uint32_t>(state.core_id));
    trace::encode_u32_le(p + 4, static_cast<std::uint32_t>(state.lifecycle));
    trace::encode_u64_le(p + 8, state.instruction_retirement_count);
    trace::encode_u64_le(p + 16, state.current_budget);
    trace::encode_u64_le(p + 24, state.executed_in_quantum);
    trace::encode_u32_le(p + 32, state.pending_interrupt ? 1u : 0u);
    trace::encode_u32_le(p + 36, 0u);
    p += 40;

    for (std::size_t i = 0; i < 31; ++i) {
        trace::encode_u64_le(p + i * 8, state.x[i]);
    }
    p += 248;

    trace::encode_u64_le(p + 0, state.pc);
    trace::encode_u64_le(p + 8, state.sp_el0);
    trace::encode_u64_le(p + 16, state.sp_el1);
    trace::encode_u64_le(p + 24, state.sp_el2);
    trace::encode_u64_le(p + 32, state.sp_el3);
    p += 40;

    p[0] = state.nzcv;
    p[1] = state.daif;
    p[2] = state.sp_sel ? 1u : 0u;
    p[3] = state.il ? 1u : 0u;
    p[4] = state.pstate_ss ? 1u : 0u;
    p[5] = static_cast<std::uint8_t>(state.current_el);
    p[6] = 0;
    p[7] = 0;
    p += 8;

    for (std::size_t i = 0; i < 32; ++i) {
        trace::encode_u64_le(p + i * 16 + 0, state.v[i].low);
        trace::encode_u64_le(p + i * 16 + 8, state.v[i].high);
    }
    p += 512;
    trace::encode_u32_le(p + 0, state.fpcr);
    trace::encode_u32_le(p + 4, state.fpsr);
    p += 8;

    // EL1
    trace::encode_u64_le(p + 0, state.elr_el1);
    trace::encode_u32_le(p + 8, state.spsr_el1);
    trace::encode_u32_le(p + 12, state.esr_el1);
    trace::encode_u64_le(p + 16, state.far_el1);
    trace::encode_u64_le(p + 24, state.vbar_el1);
    p += 32;

    // EL2
    trace::encode_u64_le(p + 0, state.elr_el2);
    trace::encode_u32_le(p + 8, state.spsr_el2);
    trace::encode_u32_le(p + 12, state.esr_el2);
    trace::encode_u64_le(p + 16, state.far_el2);
    trace::encode_u64_le(p + 24, state.vbar_el2);
    trace::encode_u64_le(p + 32, state.hcr_el2);
    p += 40;

    // EL3
    trace::encode_u64_le(p + 0, state.elr_el3);
    trace::encode_u32_le(p + 8, state.spsr_el3);
    trace::encode_u32_le(p + 12, state.esr_el3);
    trace::encode_u64_le(p + 16, state.far_el3);
    trace::encode_u64_le(p + 24, state.vbar_el3);
    trace::encode_u64_le(p + 32, state.scr_el3);
    p += 40;

    // Sysregs
    trace::encode_u64_le(p + 0, state.sctlr_el1);
    trace::encode_u64_le(p + 8, state.tcr_el1);
    trace::encode_u64_le(p + 16, state.ttbr0_el1);
    trace::encode_u64_le(p + 24, state.ttbr1_el1);
    trace::encode_u64_le(p + 32, state.mair_el1);
    trace::encode_u64_le(p + 40, state.cpacr_el1);
    trace::encode_u64_le(p + 48, state.cntvoff_el2);
    p += 56;

    // Timers
    trace::encode_u64_le(p + 0, state.cntp_cval_el0);
    trace::encode_u32_le(p + 8, state.cntp_ctl_el0);
    trace::encode_u32_le(p + 12, 0u);
    trace::encode_u64_le(p + 16, state.cntv_cval_el0);
    trace::encode_u32_le(p + 24, state.cntv_ctl_el0);
    trace::encode_u32_le(p + 28, 0u);
    p += 32;

    // Phase 36: Thread Pointer registers (3 * 8 = 24 bytes)
    trace::encode_u64_le(p + 0, state.tpidr_el0);
    trace::encode_u64_le(p + 8, state.tpidr_el1);
    trace::encode_u64_le(p + 16, state.tpidrro_el0);
    p += 24;

    // Stage 39: architectural debug block (112 bytes).
    trace::encode_u64_le(p + 0, state.debug.mdscr_el1);
    trace::encode_u64_le(p + 8, state.debug.mdccint_el1);
    p += 16;
    for (std::size_t i = 0; i < kDebugBreakpointCount; ++i) {
        trace::encode_u64_le(p + 0, state.debug.breakpoint_value[i]);
        trace::encode_u32_le(p + 8, state.debug.breakpoint_control[i]);
        trace::encode_u32_le(p + 12, 0u);
        p += 16;
    }
    for (std::size_t i = 0; i < kDebugWatchpointCount; ++i) {
        trace::encode_u64_le(p + 0, state.debug.watchpoint_value[i]);
        trace::encode_u32_le(p + 8, state.debug.watchpoint_control[i]);
        trace::encode_u32_le(p + 12, 0u);
        p += 16;
    }

    trace::encode_u32_le(p + 0, state.pmu.pmcr);
    trace::encode_u32_le(p + 4, state.pmu.counter_enable);
    trace::encode_u32_le(p + 8, state.pmu.overflow_status);
    trace::encode_u32_le(p + 12, state.pmu.interrupt_enable);
    trace::encode_u32_le(p + 16, state.pmu.selector);
    trace::encode_u32_le(p + 20, state.pmu.user_enable);
    trace::encode_u64_le(p + 24, state.pmu.cycle_counter);
    trace::encode_u32_le(p + 32, state.pmu.cycle_filter);
    trace::encode_u32_le(p + 36, 0u);
    trace::encode_u64_le(p + 40, state.pmu.cycle_prescaler);
    p += 48;
    for (std::size_t i = 0; i < kNumEventCounters; ++i) {
        trace::encode_u32_le(p + 0, state.pmu.event_counter[i]);
        trace::encode_u32_le(p + 4, state.pmu.event_type[i]);
        p += 8;
    }

    return buf;
}

SnapshotResult deserialize_core_architectural_state(const std::uint8_t* src, std::size_t size,
                                                     SmpCoreState& out_state) {
    constexpr std::size_t kPayloadSize = 1056;
    if (!src || size < kPayloadSize) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for CoreArchitecturalState");
    }

    const std::uint8_t* p = src;
    out_state.core_id = static_cast<std::size_t>(trace::decode_u32_le(p + 0));
    out_state.lifecycle = static_cast<CoreLifecycleState>(trace::decode_u32_le(p + 4));
    out_state.instruction_retirement_count = trace::decode_u64_le(p + 8);
    out_state.current_budget = trace::decode_u64_le(p + 16);
    out_state.executed_in_quantum = trace::decode_u64_le(p + 24);
    out_state.pending_interrupt = (trace::decode_u32_le(p + 32) != 0);
    p += 40;

    for (std::size_t i = 0; i < 31; ++i) {
        out_state.x[i] = trace::decode_u64_le(p + i * 8);
    }
    p += 248;

    out_state.pc = trace::decode_u64_le(p + 0);
    out_state.sp_el0 = trace::decode_u64_le(p + 8);
    out_state.sp_el1 = trace::decode_u64_le(p + 16);
    out_state.sp_el2 = trace::decode_u64_le(p + 24);
    out_state.sp_el3 = trace::decode_u64_le(p + 32);
    p += 40;

    out_state.nzcv = p[0];
    out_state.daif = p[1];
    out_state.sp_sel = (p[2] != 0);
    out_state.il = (p[3] != 0);
    out_state.pstate_ss = (p[4] != 0);
    out_state.current_el = p[5];
    p += 8;

    for (std::size_t i = 0; i < 32; ++i) {
        out_state.v[i].low = trace::decode_u64_le(p + i * 16 + 0);
        out_state.v[i].high = trace::decode_u64_le(p + i * 16 + 8);
    }
    p += 512;
    out_state.fpcr = trace::decode_u32_le(p + 0);
    out_state.fpsr = trace::decode_u32_le(p + 4);
    p += 8;

    out_state.elr_el1 = trace::decode_u64_le(p + 0);
    out_state.spsr_el1 = trace::decode_u32_le(p + 8);
    out_state.esr_el1 = trace::decode_u32_le(p + 12);
    out_state.far_el1 = trace::decode_u64_le(p + 16);
    out_state.vbar_el1 = trace::decode_u64_le(p + 24);
    p += 32;

    out_state.elr_el2 = trace::decode_u64_le(p + 0);
    out_state.spsr_el2 = trace::decode_u32_le(p + 8);
    out_state.esr_el2 = trace::decode_u32_le(p + 12);
    out_state.far_el2 = trace::decode_u64_le(p + 16);
    out_state.vbar_el2 = trace::decode_u64_le(p + 24);
    out_state.hcr_el2 = trace::decode_u64_le(p + 32);
    p += 40;

    out_state.elr_el3 = trace::decode_u64_le(p + 0);
    out_state.spsr_el3 = trace::decode_u32_le(p + 8);
    out_state.esr_el3 = trace::decode_u32_le(p + 12);
    out_state.far_el3 = trace::decode_u64_le(p + 16);
    out_state.vbar_el3 = trace::decode_u64_le(p + 24);
    out_state.scr_el3 = trace::decode_u64_le(p + 32);
    p += 40;

    out_state.sctlr_el1 = trace::decode_u64_le(p + 0);
    out_state.tcr_el1 = trace::decode_u64_le(p + 8);
    out_state.ttbr0_el1 = trace::decode_u64_le(p + 16);
    out_state.ttbr1_el1 = trace::decode_u64_le(p + 24);
    out_state.mair_el1 = trace::decode_u64_le(p + 32);
    out_state.cpacr_el1 = trace::decode_u64_le(p + 40);
    out_state.cntvoff_el2 = trace::decode_u64_le(p + 48);
    p += 56;

    out_state.cntp_cval_el0 = trace::decode_u64_le(p + 0);
    out_state.cntp_ctl_el0 = trace::decode_u32_le(p + 8);
    out_state.cntv_cval_el0 = trace::decode_u64_le(p + 16);
    out_state.cntv_ctl_el0 = trace::decode_u32_le(p + 24);
    p += 32;

    if (size >= kCoreArchLegacyPayloadSize) {
        out_state.tpidr_el0 = trace::decode_u64_le(p + 0);
        out_state.tpidr_el1 = trace::decode_u64_le(p + 8);
        out_state.tpidrro_el0 = trace::decode_u64_le(p + 16);
    } else {
        out_state.tpidr_el0 = 0;
        out_state.tpidr_el1 = 0;
        out_state.tpidrro_el0 = 0;
    }

    // Stage 39: trailing debug block.  A legacy 1080-byte payload leaves the
    // debug state at architectural reset values.
    if (size >= kCoreArchPayloadSize) {
        p += 24;
        out_state.debug.mdscr_el1 = trace::decode_u64_le(p + 0);
        out_state.debug.mdccint_el1 = trace::decode_u64_le(p + 8);
        p += 16;
        for (std::size_t i = 0; i < kDebugBreakpointCount; ++i) {
            out_state.debug.breakpoint_value[i] = trace::decode_u64_le(p + 0);
            out_state.debug.breakpoint_control[i] = trace::decode_u32_le(p + 8);
            p += 16;
        }
        for (std::size_t i = 0; i < kDebugWatchpointCount; ++i) {
            out_state.debug.watchpoint_value[i] = trace::decode_u64_le(p + 0);
            out_state.debug.watchpoint_control[i] = trace::decode_u32_le(p + 8);
            p += 16;
        }

        out_state.pmu.pmcr = trace::decode_u32_le(p + 0);
        out_state.pmu.counter_enable = trace::decode_u32_le(p + 4);
        out_state.pmu.overflow_status = trace::decode_u32_le(p + 8);
        out_state.pmu.interrupt_enable = trace::decode_u32_le(p + 12);
        out_state.pmu.selector = trace::decode_u32_le(p + 16);
        out_state.pmu.user_enable = trace::decode_u32_le(p + 20);
        out_state.pmu.cycle_counter = trace::decode_u64_le(p + 24);
        out_state.pmu.cycle_filter = trace::decode_u32_le(p + 32);
        out_state.pmu.cycle_prescaler = trace::decode_u64_le(p + 40);
        p += 48;
        for (std::size_t i = 0; i < kNumEventCounters; ++i) {
            out_state.pmu.event_counter[i] = trace::decode_u32_le(p + 0);
            out_state.pmu.event_type[i] = trace::decode_u32_le(p + 4);
            p += 8;
        }
    } else {
        out_state.debug = SmpDebugState{};
        out_state.pmu = SmpPmuState{};
    }

    return SnapshotResult::ok();
}

// -----------------------------------------------------------------------------
// 3. Shared Memory Serialization (Sparse 4KB Chunks)
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> serialize_shared_memory_state(const Memory& mem,
                                                        std::size_t chunk_size,
                                                        bool omit_zero_chunks) {
    if (chunk_size == 0) chunk_size = kDefaultSparseChunkSizeBytes;
    const std::size_t mem_size = mem.size();
    const std::uint32_t total_chunks = static_cast<std::uint32_t>((mem_size + chunk_size - 1) / chunk_size);
    const auto* raw_data = mem.raw_data();

    struct SparseChunk {
        std::uint32_t index;
        std::uint32_t length;
        const std::uint8_t* data;
    };
    std::vector<SparseChunk> chunks;
    chunks.reserve(total_chunks);

    for (std::uint32_t idx = 0; idx < total_chunks; ++idx) {
        const std::size_t offset = static_cast<std::size_t>(idx) * chunk_size;
        const std::size_t cur_len = std::min(chunk_size, mem_size - offset);
        const std::uint8_t* ptr = raw_data + offset;

        if (omit_zero_chunks && is_chunk_zero(ptr, cur_len)) {
            continue; // Skip all-zero chunk
        }
        chunks.push_back({idx, static_cast<std::uint32_t>(cur_len), ptr});
    }

    std::size_t total_size = 24; // 8 (size) + 4 (chunk_size) + 4 (total) + 4 (non_zero) + 4 (flags)
    for (const auto& ch : chunks) {
        total_size += 8 + ch.length; // 4 (idx) + 4 (len) + length
    }

    std::vector<std::uint8_t> buf(total_size);
    std::uint8_t* p = buf.data();

    trace::encode_u64_le(p + 0, mem_size);
    trace::encode_u32_le(p + 8, static_cast<std::uint32_t>(chunk_size));
    trace::encode_u32_le(p + 12, total_chunks);
    trace::encode_u32_le(p + 16, static_cast<std::uint32_t>(chunks.size()));
    trace::encode_u32_le(p + 20, omit_zero_chunks ? 1u : 0u);
    p += 24;

    for (const auto& ch : chunks) {
        trace::encode_u32_le(p + 0, ch.index);
        trace::encode_u32_le(p + 4, ch.length);
        std::memcpy(p + 8, ch.data, ch.length);
        p += 8 + ch.length;
    }

    return buf;
}

SnapshotResult deserialize_shared_memory_state(const std::uint8_t* src, std::size_t size,
                                                Memory& mem) {
    if (!src || size < 24) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for SharedMemory header");
    }

    const std::uint64_t mem_size = trace::decode_u64_le(src + 0);
    const std::uint32_t chunk_size = trace::decode_u32_le(src + 8);
    const std::uint32_t total_chunks = trace::decode_u32_le(src + 12);
    const std::uint32_t non_zero_chunks = trace::decode_u32_le(src + 16);

    if (chunk_size == 0) {
        return SnapshotResult::fail(SnapshotErrorCode::InvalidStateData, "SharedMemory chunk size cannot be zero");
    }

    if (mem.size() != mem_size) {
        return SnapshotResult::fail(SnapshotErrorCode::MemoryCapacityMismatch,
            "Target memory size (" + std::to_string(mem.size()) + ") does not match snapshot size (" + std::to_string(mem_size) + ")");
    }

    // Zero out entire target memory before restoring non-zero chunks
    std::memset(mem.raw_data(), 0, mem.size());

    const std::uint8_t* p = src + 24;
    const std::uint8_t* end = src + size;

    for (std::uint32_t i = 0; i < non_zero_chunks; ++i) {
        if (p + 8 > end) {
            return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer truncated reading chunk header");
        }
        const std::uint32_t chunk_idx = trace::decode_u32_le(p + 0);
        const std::uint32_t chunk_len = trace::decode_u32_le(p + 4);
        p += 8;

        if (chunk_idx >= total_chunks) {
            return SnapshotResult::fail(SnapshotErrorCode::CorruptedChunk, "Chunk index out of range");
        }
        if (p + chunk_len > end) {
            return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer truncated reading chunk payload");
        }

        const std::uint64_t dst_offset = static_cast<std::uint64_t>(chunk_idx) * chunk_size;
        if (dst_offset + chunk_len > mem.size()) {
            return SnapshotResult::fail(SnapshotErrorCode::CorruptedChunk, "Chunk write extends beyond memory bounds");
        }

        std::memcpy(mem.raw_data() + dst_offset, p, chunk_len);
        p += chunk_len;
    }

    return SnapshotResult::ok();
}

// -----------------------------------------------------------------------------
// 4. Scheduler Coordinator State Serialization
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> serialize_scheduler_state(const SchedulerSnapshotData& data) {
    const std::size_t total_size = 32 + data.slot_metrics.size() * 32;
    std::vector<std::uint8_t> buf(total_size);
    std::uint8_t* p = buf.data();

    trace::encode_u64_le(p + 0, data.quantum_instructions);
    trace::encode_u64_le(p + 8, data.global_retirement_sequence);
    trace::encode_u32_le(p + 16, data.registered_core_count);
    trace::encode_u32_le(p + 20, data.current_core_index);
    trace::encode_u32_le(p + 24, static_cast<std::uint32_t>(data.slot_metrics.size()));
    trace::encode_u32_le(p + 28, 0u); // reserved
    p += 32;

    for (const auto& sm : data.slot_metrics) {
        trace::encode_u32_le(p + 0, sm.core_id);
        trace::encode_u32_le(p + 4, sm.active ? 1u : 0u);
        trace::encode_u64_le(p + 8, sm.total_instructions_retired);
        trace::encode_u64_le(p + 16, sm.quantums_allocated);
        trace::encode_u64_le(p + 24, sm.early_truncations);
        p += 32;
    }
    return buf;
}

SnapshotResult deserialize_scheduler_state(const std::uint8_t* src, std::size_t size,
                                            SchedulerSnapshotData& out_data) {
    out_data.slot_metrics.clear();
    if (!src || size < 32) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for Scheduler header");
    }

    out_data.quantum_instructions = trace::decode_u64_le(src + 0);
    out_data.global_retirement_sequence = trace::decode_u64_le(src + 8);
    out_data.registered_core_count = trace::decode_u32_le(src + 16);
    out_data.current_core_index = trace::decode_u32_le(src + 20);
    const std::uint32_t slot_count = trace::decode_u32_le(src + 24);

    const std::size_t expected_size = 32 + static_cast<std::size_t>(slot_count) * 32;
    if (size < expected_size) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer truncated for Scheduler slot metrics");
    }

    const std::uint8_t* p = src + 32;
    out_data.slot_metrics.reserve(slot_count);
    for (std::uint32_t i = 0; i < slot_count; ++i) {
        SchedulerSnapshotData::SlotMetrics sm;
        sm.core_id = trace::decode_u32_le(p + 0);
        sm.active = (trace::decode_u32_le(p + 4) != 0);
        sm.total_instructions_retired = trace::decode_u64_le(p + 8);
        sm.quantums_allocated = trace::decode_u64_le(p + 16);
        sm.early_truncations = trace::decode_u64_le(p + 24);
        p += 32;
        out_data.slot_metrics.push_back(sm);
    }
    return SnapshotResult::ok();
}

// -----------------------------------------------------------------------------
// 5. Exclusive Monitor State Serialization
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> serialize_exclusive_monitor_state(const ExclusiveMonitorSnapshotData& data) {
    const std::size_t total_size = 8 + data.reservations.size() * 32;
    std::vector<std::uint8_t> buf(total_size);
    std::uint8_t* p = buf.data();

    trace::encode_u32_le(p + 0, data.max_cores);
    trace::encode_u32_le(p + 4, static_cast<std::uint32_t>(data.reservations.size()));
    p += 8;

    for (const auto& r : data.reservations) {
        trace::encode_u32_le(p + 0, static_cast<std::uint32_t>(r.core_id));
        trace::encode_u32_le(p + 4, r.valid ? 1u : 0u);
        trace::encode_u64_le(p + 8, r.granule_address);
        trace::encode_u64_le(p + 16, r.exact_address);
        trace::encode_u64_le(p + 24, static_cast<std::uint64_t>(r.size));
        p += 32;
    }
    return buf;
}

SnapshotResult deserialize_exclusive_monitor_state(const std::uint8_t* src, std::size_t size,
                                                    ExclusiveMonitorSnapshotData& out_data) {
    out_data.reservations.clear();
    if (!src || size < 8) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for ExclusiveMonitor header");
    }

    out_data.max_cores = trace::decode_u32_le(src + 0);
    const std::uint32_t count = trace::decode_u32_le(src + 4);

    const std::size_t expected_size = 8 + static_cast<std::size_t>(count) * 32;
    if (size < expected_size) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer truncated for ExclusiveMonitor reservations");
    }

    const std::uint8_t* p = src + 8;
    out_data.reservations.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        ReservationEntry r;
        r.core_id = static_cast<std::size_t>(trace::decode_u32_le(p + 0));
        r.valid = (trace::decode_u32_le(p + 4) != 0);
        r.granule_address = trace::decode_u64_le(p + 8);
        r.exact_address = trace::decode_u64_le(p + 16);
        r.size = static_cast<std::size_t>(trace::decode_u64_le(p + 24));
        p += 32;
        out_data.reservations.push_back(r);
    }
    return SnapshotResult::ok();
}

// -----------------------------------------------------------------------------
// 6. Cross-Core Interrupt Router State Serialization
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> serialize_interrupt_router_state(const InterruptRouterSnapshotData& data) {
    std::size_t total_size = 16; // 4 (max_cores) + 4 (pad) + 8 (next_seq)
    for (const auto& q : data.core_queues) {
        total_size += 8 + q.size() * 24; // 4 (core_id) + 4 (q_len) + requests * 24
    }

    std::vector<std::uint8_t> buf(total_size);
    std::uint8_t* p = buf.data();

    trace::encode_u32_le(p + 0, data.max_cores);
    trace::encode_u32_le(p + 4, 0u);
    trace::encode_u64_le(p + 8, data.next_sequence);
    p += 16;

    for (std::size_t c = 0; c < data.core_queues.size(); ++c) {
        const auto& q = data.core_queues[c];
        trace::encode_u32_le(p + 0, static_cast<std::uint32_t>(c));
        trace::encode_u32_le(p + 4, static_cast<std::uint32_t>(q.size()));
        p += 8;

        for (const auto& req : q) {
            trace::encode_u32_le(p + 0, static_cast<std::uint32_t>(req.target_core));
            trace::encode_u32_le(p + 4, static_cast<std::uint32_t>(req.source_core));
            trace::encode_u32_le(p + 8, req.intid);
            p[12] = req.priority;
            p[13] = static_cast<std::uint8_t>(req.type);
            p[14] = req.is_fiq ? 1u : 0u;
            p[15] = 0; // reserved
            trace::encode_u64_le(p + 16, req.request_sequence);
            p += 24;
        }
    }
    return buf;
}

SnapshotResult deserialize_interrupt_router_state(const std::uint8_t* src, std::size_t size,
                                                   InterruptRouterSnapshotData& out_data) {
    out_data.core_queues.clear();
    if (!src || size < 16) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for InterruptRouter header");
    }

    out_data.max_cores = trace::decode_u32_le(src + 0);
    out_data.next_sequence = trace::decode_u64_le(src + 8);
    out_data.core_queues.resize(out_data.max_cores);

    const std::uint8_t* p = src + 16;
    const std::uint8_t* end = src + size;

    for (std::uint32_t c = 0; c < out_data.max_cores && p < end; ++c) {
        if (p + 8 > end) {
            return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer truncated reading core queue header");
        }
        const std::uint32_t core_id = trace::decode_u32_le(p + 0);
        const std::uint32_t q_len = trace::decode_u32_le(p + 4);
        p += 8;

        if (core_id < out_data.core_queues.size()) {
            auto& q = out_data.core_queues[core_id];
            q.reserve(q_len);
            for (std::uint32_t i = 0; i < q_len; ++i) {
                if (p + 24 > end) {
                    return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer truncated reading InterruptRequest");
                }
                InterruptRequest req;
                req.target_core = static_cast<std::size_t>(trace::decode_u32_le(p + 0));
                req.source_core = static_cast<std::size_t>(trace::decode_u32_le(p + 4));
                req.intid = trace::decode_u32_le(p + 8);
                req.priority = p[12];
                req.type = static_cast<InterruptType>(p[13]);
                req.is_fiq = (p[14] != 0);
                req.request_sequence = trace::decode_u64_le(p + 16);
                p += 24;
                q.push_back(req);
            }
        }
    }
    return SnapshotResult::ok();
}

// -----------------------------------------------------------------------------
// 7. Integrity Section Serialization
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> serialize_integrity_section(const IntegritySnapshotData& data) {
    const std::size_t total_size = 24 + data.section_crcs.size() * 16;
    std::vector<std::uint8_t> buf(total_size);
    std::uint8_t* p = buf.data();

    trace::encode_u64_le(p + 0, data.canonical_cluster_hash);
    trace::encode_u64_le(p + 8, data.timestamp_ns);
    trace::encode_u32_le(p + 16, static_cast<std::uint32_t>(data.section_crcs.size()));
    trace::encode_u32_le(p + 20, 0u);
    p += 24;

    for (const auto& sc : data.section_crcs) {
        trace::encode_u32_le(p + 0, sc.section_type);
        trace::encode_u32_le(p + 4, sc.section_id);
        trace::encode_u32_le(p + 8, sc.crc32c);
        trace::encode_u32_le(p + 12, 0u);
        p += 16;
    }
    return buf;
}

SnapshotResult deserialize_integrity_section(const std::uint8_t* src, std::size_t size,
                                             IntegritySnapshotData& out_data) {
    out_data.section_crcs.clear();
    if (!src || size < 24) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for Integrity header");
    }

    out_data.canonical_cluster_hash = trace::decode_u64_le(src + 0);
    out_data.timestamp_ns = trace::decode_u64_le(src + 8);
    const std::uint32_t count = trace::decode_u32_le(src + 16);

    const std::size_t expected_size = 24 + static_cast<std::size_t>(count) * 16;
    if (size < expected_size) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer truncated for Integrity CRC entries");
    }

    const std::uint8_t* p = src + 24;
    out_data.section_crcs.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        IntegritySnapshotData::SectionCrcEntry sc;
        sc.section_type = trace::decode_u32_le(p + 0);
        sc.section_id = trace::decode_u32_le(p + 4);
        sc.crc32c = trace::decode_u32_le(p + 8);
        p += 16;
        out_data.section_crcs.push_back(sc);
    }
    return SnapshotResult::ok();
}

// -----------------------------------------------------------------------------
// 8. Cluster Metadata Serialization
// -----------------------------------------------------------------------------
std::vector<std::uint8_t> serialize_cluster_metadata(const std::string& desc,
                                                     const std::string& creator,
                                                     const std::string& topology) {
    const std::size_t total_size = 12 + desc.size() + creator.size() + topology.size();
    std::vector<std::uint8_t> buf(total_size);
    std::uint8_t* p = buf.data();

    trace::encode_u32_le(p + 0, static_cast<std::uint32_t>(desc.size()));
    p += 4;
    if (!desc.empty()) {
        std::memcpy(p, desc.data(), desc.size());
        p += desc.size();
    }

    trace::encode_u32_le(p + 0, static_cast<std::uint32_t>(creator.size()));
    p += 4;
    if (!creator.empty()) {
        std::memcpy(p, creator.data(), creator.size());
        p += creator.size();
    }

    trace::encode_u32_le(p + 0, static_cast<std::uint32_t>(topology.size()));
    p += 4;
    if (!topology.empty()) {
        std::memcpy(p, topology.data(), topology.size());
        p += topology.size();
    }

    return buf;
}

SnapshotResult deserialize_cluster_metadata(const std::uint8_t* src, std::size_t size,
                                            std::string& out_desc,
                                            std::string& out_creator,
                                            std::string& out_topology) {
    out_desc.clear();
    out_creator.clear();
    out_topology.clear();

    if (!src || size < 12) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for cluster metadata");
    }

    const std::uint8_t* p = src;
    const std::uint8_t* end = src + size;

    const std::uint32_t desc_len = trace::decode_u32_le(p);
    p += 4;
    if (p + desc_len > end) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Truncated description string in metadata");
    }
    out_desc.assign(reinterpret_cast<const char*>(p), desc_len);
    p += desc_len;

    if (p + 4 > end) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Truncated creator length in metadata");
    }
    const std::uint32_t creator_len = trace::decode_u32_le(p);
    p += 4;
    if (p + creator_len > end) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Truncated creator string in metadata");
    }
    out_creator.assign(reinterpret_cast<const char*>(p), creator_len);
    p += creator_len;

    if (p + 4 > end) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Truncated topology length in metadata");
    }
    const std::uint32_t topo_len = trace::decode_u32_le(p);
    p += 4;
    if (p + topo_len > end) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Truncated topology string in metadata");
    }
    out_topology.assign(reinterpret_cast<const char*>(p), topo_len);

    return SnapshotResult::ok();
}

SnapshotResult decode_smp_wsnap_header(const std::uint8_t* src, std::size_t size,
                                       WSnapHeader& out_header) noexcept {
    if (!src || size < kWSnapHeaderSize) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Buffer too small for WSnap header");
    }

    if (std::memcmp(src, kWSnapMagic.data(), 8) != 0) {
        return SnapshotResult::fail(SnapshotErrorCode::InvalidMagic, "Header magic does not match WSNAP specification");
    }

    const std::uint32_t expected_crc = trace::calculate_crc32c(src, 60);
    const std::uint32_t stored_crc = trace::decode_u32_le(src + 60);
    if (expected_crc != stored_crc) {
        return SnapshotResult::fail(SnapshotErrorCode::HeaderCrcMismatch, "Header CRC32C verification failed");
    }

    std::memcpy(out_header.magic.data(), src, 8);
    out_header.version = trace::decode_u16_le(src + 8);
    if (out_header.version != kWSnapVersion2) {
        return SnapshotResult::fail(SnapshotErrorCode::UnsupportedVersion,
            "Cluster snapshot requires format version 2, got: " + std::to_string(out_header.version));
    }

    out_header.core_count = trace::decode_u16_le(src + 10);
    out_header.compression_type = static_cast<SnapshotCompressionType>(trace::decode_u16_le(src + 12));
    out_header.flags = trace::decode_u16_le(src + 14);
    out_header.timestamp_ns = trace::decode_u64_le(src + 16);
    out_header.memory_capacity = trace::decode_u64_le(src + 24);
    out_header.section_dir_offset = trace::decode_u64_le(src + 32);
    out_header.section_count = trace::decode_u32_le(src + 40);
    out_header.total_file_size = trace::decode_u32_le(src + 44);
    out_header.reserved0 = trace::decode_u32_le(src + 48);
    out_header.reserved1 = trace::decode_u32_le(src + 52);
    out_header.reserved2 = trace::decode_u32_le(src + 56);
    out_header.header_crc32c = stored_crc;

    return SnapshotResult::ok();
}

std::vector<std::uint8_t> serialize_device_state(std::uint32_t device_id, const std::vector<std::uint8_t>& device_payload) {
    const std::size_t total_sz = 8 + device_payload.size() + 4;
    std::vector<std::uint8_t> buf(total_sz, 0);

    trace::encode_u32_le(buf.data() + 0, device_id);
    trace::encode_u32_le(buf.data() + 4, static_cast<std::uint32_t>(device_payload.size()));
    if (!device_payload.empty()) {
        std::memcpy(buf.data() + 8, device_payload.data(), device_payload.size());
    }

    const std::uint32_t crc = trace::calculate_crc32c(buf.data(), 8 + device_payload.size());
    trace::encode_u32_le(buf.data() + 8 + device_payload.size(), crc);

    return buf;
}

SnapshotResult deserialize_device_state(const std::uint8_t* src, std::size_t size,
                                        std::uint32_t& out_device_id,
                                        std::vector<std::uint8_t>& out_payload) {
    if (!src || size < 12) {
        return SnapshotResult::fail(SnapshotErrorCode::InvalidStateData, "Buffer too small for device state");
    }

    const std::uint32_t payload_len = trace::decode_u32_le(src + 4);
    if (size < 8 + static_cast<std::size_t>(payload_len) + 4) {
        return SnapshotResult::fail(SnapshotErrorCode::TruncatedFile, "Device state payload truncated");
    }

    const std::uint32_t expected_crc = trace::calculate_crc32c(src, 8 + payload_len);
    const std::uint32_t actual_crc = trace::decode_u32_le(src + 8 + payload_len);
    if (expected_crc != actual_crc) {
        return SnapshotResult::fail(SnapshotErrorCode::SectionCrcMismatch, "Device state CRC32C verification failed");
    }

    out_device_id = trace::decode_u32_le(src + 0);
    out_payload.resize(payload_len);
    if (payload_len > 0) {
        std::memcpy(out_payload.data(), src + 8, payload_len);
    }

    return SnapshotResult::ok();
}

} // namespace winios::snapshot

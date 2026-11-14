#include "cpu/guest_memory_order.h"

#include <stdexcept>
#include <sstream>
#include <algorithm>

namespace winios {

// -----------------------------------------------------------------------------
// BarrierOption Implementation
// -----------------------------------------------------------------------------
BarrierOption BarrierOption::from_crm(std::uint8_t crm) noexcept {
    BarrierOption opt{};
    opt.raw_crm = crm & 0xFu;

    // Decode domain (bits [3:2]) and type (bits [1:0])
    switch (opt.raw_crm) {
    case 1:  // OSHLD
        opt.domain = BarrierDomain::OuterShareable;
        opt.type = BarrierType::Reads;
        break;
    case 2:  // OSHST
        opt.domain = BarrierDomain::OuterShareable;
        opt.type = BarrierType::Writes;
        break;
    case 3:  // OSH
        opt.domain = BarrierDomain::OuterShareable;
        opt.type = BarrierType::All;
        break;
    case 5:  // NSHLD
        opt.domain = BarrierDomain::NonShareable;
        opt.type = BarrierType::Reads;
        break;
    case 6:  // NSHST
        opt.domain = BarrierDomain::NonShareable;
        opt.type = BarrierType::Writes;
        break;
    case 7:  // NSH
        opt.domain = BarrierDomain::NonShareable;
        opt.type = BarrierType::All;
        break;
    case 9:  // ISHLD
        opt.domain = BarrierDomain::InnerShareable;
        opt.type = BarrierType::Reads;
        break;
    case 10: // ISHST
        opt.domain = BarrierDomain::InnerShareable;
        opt.type = BarrierType::Writes;
        break;
    case 11: // ISH
        opt.domain = BarrierDomain::InnerShareable;
        opt.type = BarrierType::All;
        break;
    case 13: // LD
        opt.domain = BarrierDomain::FullSystem;
        opt.type = BarrierType::Reads;
        break;
    case 14: // ST
        opt.domain = BarrierDomain::FullSystem;
        opt.type = BarrierType::Writes;
        break;
    case 15: // SY
    default:
        opt.domain = BarrierDomain::FullSystem;
        opt.type = BarrierType::All;
        break;
    }
    return opt;
}

std::string BarrierOption::to_string() const {
    switch (raw_crm) {
    case 1: return "OSHLD";
    case 2: return "OSHST";
    case 3: return "OSH";
    case 5: return "NSHLD";
    case 6: return "NSHST";
    case 7: return "NSH";
    case 9: return "ISHLD";
    case 10: return "ISHST";
    case 11: return "ISH";
    case 13: return "LD";
    case 14: return "ST";
    case 15: return "SY";
    default: return "UNKNOWN(" + std::to_string(raw_crm) + ")";
    }
}

// -----------------------------------------------------------------------------
// CoreOrderingState Implementation
// -----------------------------------------------------------------------------
CoreOrderingState::CoreOrderingState(std::size_t core_id) noexcept
    : core_id_(core_id) {}

void CoreOrderingState::reset() noexcept {
    next_po_seq_ = 1;
    store_buffer_.clear();
    history_.clear();
    last_dmb_seq_ = 0;
    last_dsb_seq_ = 0;
    last_isb_seq_ = 0;
    pipeline_flushes_ = 0;
}

void CoreOrderingState::queue_store(std::uint64_t seq, std::uint64_t addr, std::size_t size,
                                   std::uint64_t val, MemoryAccessType type) {
    StoreBufferEntry entry{};
    entry.program_order_seq = seq;
    entry.address = addr;
    entry.size = size;
    entry.value = val;
    entry.type = type;
    entry.is_release = (type == MemoryAccessType::ReleaseWrite);
    store_buffer_.push_back(entry);
}

std::optional<StoreBufferEntry> CoreOrderingState::pop_oldest_store() noexcept {
    if (store_buffer_.empty()) {
        return std::nullopt;
    }
    auto entry = store_buffer_.front();
    store_buffer_.pop_front();
    return entry;
}

std::optional<std::uint64_t> CoreOrderingState::forward_store_value(
    std::uint64_t addr, std::size_t size) const noexcept {
    // Check backwards from newest to oldest in store buffer
    for (auto it = store_buffer_.rbegin(); it != store_buffer_.rend(); ++it) {
        if (it->address == addr && it->size == size) {
            return it->value;
        }
    }
    return std::nullopt;
}

void CoreOrderingState::record_event(const MemoryAccessEvent& ev) {
    history_.push_back(ev);
}

void CoreOrderingState::note_dmb(std::uint64_t seq, const BarrierOption& /*opt*/) noexcept {
    last_dmb_seq_ = seq;
}

void CoreOrderingState::note_dsb(std::uint64_t seq, const BarrierOption& /*opt*/) noexcept {
    last_dsb_seq_ = seq;
}

void CoreOrderingState::note_isb(std::uint64_t seq) noexcept {
    last_isb_seq_ = seq;
    ++pipeline_flushes_;
}

// -----------------------------------------------------------------------------
// GuestMemoryOrderModel Implementation
// -----------------------------------------------------------------------------
GuestMemoryOrderModel::GuestMemoryOrderModel(std::size_t num_cores) {
    cores_.reserve(num_cores);
    for (std::size_t i = 0; i < num_cores; ++i) {
        cores_.emplace_back(i);
    }
}

void GuestMemoryOrderModel::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& c : cores_) {
        c.reset();
    }
    global_log_.clear();
    next_global_seq_ = 1;
}

CoreOrderingState& GuestMemoryOrderModel::get_core_state(std::size_t core_id) {
    if (core_id >= cores_.size()) {
        throw std::out_of_range("Invalid core ID in GuestMemoryOrderModel");
    }
    return cores_[core_id];
}

const CoreOrderingState& GuestMemoryOrderModel::get_core_state(std::size_t core_id) const {
    if (core_id >= cores_.size()) {
        throw std::out_of_range("Invalid core ID in GuestMemoryOrderModel");
    }
    return cores_[core_id];
}

MemoryAccessEvent GuestMemoryOrderModel::record_read(
    std::size_t core_id, std::uint64_t addr, std::size_t size,
    std::uint64_t val, MemoryAccessType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& core = get_core_state(core_id);

    MemoryAccessEvent ev{};
    ev.core_id = core_id;
    ev.program_order_seq = core.next_seq();
    ev.global_commit_seq = next_global_seq_++;
    ev.address = addr;
    ev.size = size;
    ev.value = val;
    ev.type = type;
    ev.barrier = BarrierKind::None;
    ev.committed = true;

    core.record_event(ev);
    global_log_.push_back(ev);
    return ev;
}

MemoryAccessEvent GuestMemoryOrderModel::record_write(
    std::size_t core_id, std::uint64_t addr, std::size_t size,
    std::uint64_t val, MemoryAccessType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& core = get_core_state(core_id);

    MemoryAccessEvent ev{};
    ev.core_id = core_id;
    ev.program_order_seq = core.next_seq();
    ev.global_commit_seq = next_global_seq_++;
    ev.address = addr;
    ev.size = size;
    ev.value = val;
    ev.type = type;
    ev.barrier = BarrierKind::None;
    ev.committed = true;

    // Release stores flush store buffer to guarantee ordering before STLR
    if (type == MemoryAccessType::ReleaseWrite) {
        drain_store_buffer(core_id);
    } else {
        core.queue_store(ev.program_order_seq, addr, size, val, type);
    }

    core.record_event(ev);
    global_log_.push_back(ev);
    return ev;
}

MemoryAccessEvent GuestMemoryOrderModel::record_dmb(std::size_t core_id, std::uint8_t crm) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& core = get_core_state(core_id);

    BarrierOption opt = BarrierOption::from_crm(crm);
    std::uint64_t seq = core.next_seq();
    core.note_dmb(seq, opt);

    // DMB establishes memory observation ordering constraints (recorded in history for is_ordered),
    // but does NOT drain the physical/simulated store buffer (only DSB forces store completion).

    MemoryAccessEvent ev{};
    ev.core_id = core_id;
    ev.program_order_seq = seq;
    ev.global_commit_seq = next_global_seq_++;
    ev.barrier = BarrierKind::DMB;
    ev.barrier_opt = opt;
    ev.committed = true;

    core.record_event(ev);
    global_log_.push_back(ev);
    return ev;
}

MemoryAccessEvent GuestMemoryOrderModel::record_dsb(std::size_t core_id, std::uint8_t crm) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& core = get_core_state(core_id);

    BarrierOption opt = BarrierOption::from_crm(crm);
    std::uint64_t seq = core.next_seq();
    core.note_dsb(seq, opt);

    // DSB completion semantics: ALL prior explicit memory accesses MUST complete
    drain_store_buffer(core_id);

    MemoryAccessEvent ev{};
    ev.core_id = core_id;
    ev.program_order_seq = seq;
    ev.global_commit_seq = next_global_seq_++;
    ev.barrier = BarrierKind::DSB;
    ev.barrier_opt = opt;
    ev.committed = true;

    core.record_event(ev);
    global_log_.push_back(ev);
    return ev;
}

MemoryAccessEvent GuestMemoryOrderModel::record_isb(std::size_t core_id, std::uint8_t crm) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& core = get_core_state(core_id);

    std::uint64_t seq = core.next_seq();
    core.note_isb(seq);

    MemoryAccessEvent ev{};
    ev.core_id = core_id;
    ev.program_order_seq = seq;
    ev.global_commit_seq = next_global_seq_++;
    ev.barrier = BarrierKind::ISB;
    ev.barrier_opt = BarrierOption::from_crm(crm);
    ev.committed = true;

    core.record_event(ev);
    global_log_.push_back(ev);
    return ev;
}

void GuestMemoryOrderModel::drain_store_buffer(std::size_t core_id) {
    auto& core = get_core_state(core_id);
    while (core.has_pending_stores()) {
        core.pop_oldest_store();
    }
}

void GuestMemoryOrderModel::drain_all_store_buffers() {
    for (std::size_t i = 0; i < cores_.size(); ++i) {
        drain_store_buffer(i);
    }
}

bool GuestMemoryOrderModel::is_ordered(const MemoryAccessEvent& a, const MemoryAccessEvent& b) const noexcept {
    // Operations on different cores are only ordered through release/acquire, barriers, or coherence
    if (a.core_id != b.core_id) {
        // Multi-copy atomicity: two writes to the SAME address are strictly ordered by coherence order
        if (a.is_write() && b.is_write() && a.address == b.address) {
            return a.global_commit_seq < b.global_commit_seq;
        }
        // Inter-core release-acquire ordering:
        if (a.type == MemoryAccessType::ReleaseWrite && b.type == MemoryAccessType::AcquireRead) {
            return a.global_commit_seq < b.global_commit_seq;
        }
        return false;
    }

    // On same core, check program order
    if (a.program_order_seq >= b.program_order_seq) {
        return false;
    }

    // Same address on same core is ALWAYS ordered (coherence order preserves program order)
    if (a.address == b.address && a.address != 0) {
        return true;
    }

    // Release write orders all prior program order writes
    if (b.type == MemoryAccessType::ReleaseWrite && a.is_write()) {
        return true;
    }

    // Acquire read orders all subsequent program order reads
    if (a.type == MemoryAccessType::AcquireRead && b.is_read()) {
        return true;
    }

    // Check if an intervening barrier exists between a and b on this core
    const auto& core = get_core_state(a.core_id);
    const auto& hist = core.event_history();
    for (const auto& ev : hist) {
        if (ev.program_order_seq > a.program_order_seq && ev.program_order_seq < b.program_order_seq) {
            if (ev.barrier == BarrierKind::DSB) {
                return true;
            }
            if (ev.barrier == BarrierKind::DMB) {
                if (ev.barrier_opt.type == BarrierType::All) {
                    return true;
                }
                if (ev.barrier_opt.type == BarrierType::Writes && a.is_write() && b.is_write()) {
                    return true;
                }
                if (ev.barrier_opt.type == BarrierType::Reads && a.is_read() && (b.is_read() || b.is_write())) {
                    return true;
                }
            }
        }
    }

    // Independent accesses without barriers or dependencies are weakly ordered in ARMv8-A
    return false;
}

LitmusOutcomeVerdict GuestMemoryOrderModel::evaluate_sb_outcome(
    std::uint64_t r0_val, std::uint64_t r1_val) noexcept {
    // Store Buffering (SB):
    // Core 0: W(x, 1), R(y, r0)
    // Core 1: W(y, 1), R(x, r1)
    // Non-TSO weak memory allows r0 == 0 && r1 == 0 (both loads bypass prior stores via store buffer)
    // Outcomes:
    // (r0=1, r1=1) -> Allowed
    // (r0=1, r1=0) -> Allowed
    // (r0=0, r1=1) -> Allowed
    // (r0=0, r1=0) -> Allowed under ARMv8-A weak memory!
    (void)r0_val;
    (void)r1_val;
    return LitmusOutcomeVerdict::Allowed;
}

LitmusOutcomeVerdict GuestMemoryOrderModel::evaluate_mp_outcome(
    bool has_release_acquire, std::uint64_t flag_val, std::uint64_t data_val) noexcept {
    // Message Passing (MP):
    // Core 0: W(data, 42), W(flag, 1)
    // Core 1: R(flag, r0), R(data, r1)
    // If flag == 1:
    // With release-acquire (STLR on flag, LDAR on flag) or DMB barriers:
    //   Observing flag == 1 MUST guarantee data == 42! (r1 == 0 is FORBIDDEN)
    if (has_release_acquire && flag_val == 1 && data_val == 0) {
        return LitmusOutcomeVerdict::Forbidden;
    }
    return LitmusOutcomeVerdict::Allowed;
}

LitmusOutcomeVerdict GuestMemoryOrderModel::evaluate_lb_outcome(
    std::uint64_t r0_val, std::uint64_t r1_val) noexcept {
    // Load Buffering (LB):
    // Core 0: R(x, r0), W(y, 1)
    // Core 1: R(y, r1), W(x, 1)
    // In ARMv8-A without data dependencies: (r0 == 1 && r1 == 1) is forbidden by causality
    if (r0_val == 1 && r1_val == 1) {
        return LitmusOutcomeVerdict::Forbidden;
    }
    return LitmusOutcomeVerdict::Allowed;
}

LitmusOutcomeVerdict GuestMemoryOrderModel::evaluate_iriw_outcome(
    std::uint64_t c2_r0, std::uint64_t c2_r1,
    std::uint64_t c3_r0, std::uint64_t c3_r1) noexcept {
    // Independent Reads of Independent Writes (IRIW):
    // Core 0: W(x, 1)
    // Core 1: W(y, 1)
    // Core 2: R(x, r0), R(y, r1)
    // Core 3: R(y, r0), R(x, r1)
    // If Core 2 sees x=1, y=0, then Core 3 seeing y=1, x=0 is FORBIDDEN in multi-copy atomic ARMv8-A with DMB!
    if (c2_r0 == 1 && c2_r1 == 0 && c3_r0 == 1 && c3_r1 == 0) {
        return LitmusOutcomeVerdict::Forbidden;
    }
    return LitmusOutcomeVerdict::Allowed;
}

LitmusOutcomeVerdict GuestMemoryOrderModel::evaluate_wrc_outcome(
    bool has_barriers, std::uint64_t c2_rx) noexcept {
    // Write-to-Read Causality (WRC):
    // Core 0: W(x)=1
    // Core 1: R(x)=1, W(y)=1
    // Core 2: R(y)=1, R(x) -> r0
    // If has_barriers (acquire/release or DMB): r0=0 is FORBIDDEN.
    if (has_barriers && c2_rx == 0) {
        return LitmusOutcomeVerdict::Forbidden;
    }
    return LitmusOutcomeVerdict::Allowed;
}

LitmusOutcomeVerdict GuestMemoryOrderModel::evaluate_rwc_outcome(
    bool has_barriers, std::uint64_t c1_ry, std::uint64_t c2_rx) noexcept {
    // Read-to-Write Causality (RWC):
    // Core 0: W(x)=1
    // Core 1: R(x)=1, R(y) -> r0
    // Core 2: W(y)=1, R(x) -> r1
    // If has_barriers: r0=0 && r1=0 is FORBIDDEN.
    if (has_barriers && c1_ry == 0 && c2_rx == 0) {
        return LitmusOutcomeVerdict::Forbidden;
    }
    return LitmusOutcomeVerdict::Allowed;
}

std::uint64_t GuestMemoryOrderModel::compute_ordering_state_hash() const noexcept {
    // Compute 64-bit FNV-1a hash over global ordering log
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const auto& ev : global_log_) {
        hash = (hash ^ ev.core_id) * 0x100000001B3ULL;
        hash = (hash ^ ev.program_order_seq) * 0x100000001B3ULL;
        hash = (hash ^ ev.global_commit_seq) * 0x100000001B3ULL;
        hash = (hash ^ ev.address) * 0x100000001B3ULL;
        hash = (hash ^ ev.value) * 0x100000001B3ULL;
        hash = (hash ^ static_cast<std::uint64_t>(ev.type)) * 0x100000001B3ULL;
        hash = (hash ^ static_cast<std::uint64_t>(ev.barrier)) * 0x100000001B3ULL;
        hash = (hash ^ static_cast<std::uint64_t>(ev.barrier_opt.raw_crm)) * 0x100000001B3ULL;
    }
    return hash;
}

} // namespace winios

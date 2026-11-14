#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <optional>
#include <mutex>
#include <functional>

namespace winios {

// =============================================================================
// ARMv8-A Guest Memory Ordering Model
//
// Formally models ARMv8-A weak memory ordering rules independent of the host
// CPU's native memory architecture (e.g. x86-64 TSO).
// References: ARM Architecture Reference Manual ARMv8 (ARM DDI 0487)
//   - Section B2.1 & B2.2: Memory Model, Coherence, Multi-copy Atomicity
//   - Section B2.2.2: Acquire and Release Semantics
//   - Section B2.3: Memory Barriers (DMB, DSB, ISB) and Shareability Domains
// =============================================================================

enum class MemoryAccessType : std::uint8_t {
    NormalRead,
    NormalWrite,
    AcquireRead,     // LDAR, LDAXR, etc.
    ReleaseWrite,    // STLR, STLXR, etc.
    ExclusiveRead,   // LDXR, LDXP
    ExclusiveWrite,  // STXR, STXP
    AtomicRMW        // CAS, SWP, LDADD, etc.
};

enum class BarrierKind : std::uint8_t {
    None,
    DMB, // Data Memory Barrier
    DSB, // Data Synchronization Barrier
    ISB  // Instruction Synchronization Barrier
};

enum class BarrierDomain : std::uint8_t {
    NonShareable,
    InnerShareable,
    OuterShareable,
    FullSystem
};

enum class BarrierType : std::uint8_t {
    All,    // SY
    Reads,  // LD
    Writes  // ST
};

// Decoded representation of ARM64 DMB/DSB option (CRm[3:0])
struct BarrierOption {
    BarrierDomain domain{BarrierDomain::FullSystem};
    BarrierType type{BarrierType::All};
    std::uint8_t raw_crm{0xF};

    [[nodiscard]] static BarrierOption from_crm(std::uint8_t crm) noexcept;
    [[nodiscard]] std::string to_string() const;
};

// Single guest memory access or barrier event
struct MemoryAccessEvent {
    std::size_t core_id{0};
    std::uint64_t program_order_seq{0};
    std::uint64_t global_commit_seq{0};
    std::uint64_t address{0};
    std::size_t size{0};
    std::uint64_t value{0};
    MemoryAccessType type{MemoryAccessType::NormalRead};
    BarrierKind barrier{BarrierKind::None};
    BarrierOption barrier_opt{};
    bool committed{false};

    [[nodiscard]] bool is_read() const noexcept {
        return type == MemoryAccessType::NormalRead ||
               type == MemoryAccessType::AcquireRead ||
               type == MemoryAccessType::ExclusiveRead;
    }

    [[nodiscard]] bool is_write() const noexcept {
        return type == MemoryAccessType::NormalWrite ||
               type == MemoryAccessType::ReleaseWrite ||
               type == MemoryAccessType::ExclusiveWrite;
    }

    [[nodiscard]] bool is_atomic() const noexcept {
        return type == MemoryAccessType::AtomicRMW;
    }

    [[nodiscard]] bool is_barrier() const noexcept {
        return barrier != BarrierKind::None;
    }
};

// Store buffer entry for simulating deferred visibility
struct StoreBufferEntry {
    std::uint64_t program_order_seq{0};
    std::uint64_t address{0};
    std::size_t size{0};
    std::uint64_t value{0};
    MemoryAccessType type{MemoryAccessType::NormalWrite};
    bool is_release{false};
};

// Per-core memory ordering and event tracking state
class CoreOrderingState {
public:
    explicit CoreOrderingState(std::size_t core_id = 0) noexcept;

    void reset() noexcept;

    [[nodiscard]] std::size_t core_id() const noexcept { return core_id_; }
    [[nodiscard]] std::uint64_t current_po_seq() const noexcept { return next_po_seq_; }

    // Allocate next sequence number in program order
    std::uint64_t next_seq() noexcept { return next_po_seq_++; }

    // Store buffer operations (simulating out-of-order write visibility)
    void queue_store(std::uint64_t seq, std::uint64_t addr, std::size_t size, std::uint64_t val, MemoryAccessType type);
    [[nodiscard]] bool has_pending_stores() const noexcept { return !store_buffer_.empty(); }
    [[nodiscard]] std::size_t pending_store_count() const noexcept { return store_buffer_.size(); }
    std::optional<StoreBufferEntry> pop_oldest_store() noexcept;
    std::optional<std::uint64_t> forward_store_value(std::uint64_t addr, std::size_t size) const noexcept;

    // History of all events issued on this core
    void record_event(const MemoryAccessEvent& ev);
    [[nodiscard]] const std::vector<MemoryAccessEvent>& event_history() const noexcept { return history_; }

    // Barriers and synchronization tracking
    void note_dmb(std::uint64_t seq, const BarrierOption& opt) noexcept;
    void note_dsb(std::uint64_t seq, const BarrierOption& opt) noexcept;
    void note_isb(std::uint64_t seq) noexcept;

    [[nodiscard]] std::uint64_t last_dmb_seq() const noexcept { return last_dmb_seq_; }
    [[nodiscard]] std::uint64_t last_dsb_seq() const noexcept { return last_dsb_seq_; }
    [[nodiscard]] std::uint64_t last_isb_seq() const noexcept { return last_isb_seq_; }
    [[nodiscard]] std::uint64_t pipeline_flush_count() const noexcept { return pipeline_flushes_; }

private:
    std::size_t core_id_{0};
    std::uint64_t next_po_seq_{1};
    std::deque<StoreBufferEntry> store_buffer_{};
    std::vector<MemoryAccessEvent> history_{};
    std::uint64_t last_dmb_seq_{0};
    std::uint64_t last_dsb_seq_{0};
    std::uint64_t last_isb_seq_{0};
    std::uint64_t pipeline_flushes_{0};
};

// Global outcome validity for litmus tests
enum class LitmusOutcomeVerdict {
    Allowed,   // Permitted under ARMv8-A weak memory model
    Forbidden  // Strictly forbidden by ARMv8-A architectural ordering invariants
};

// The centralized guest memory ordering model
class GuestMemoryOrderModel {
public:
    explicit GuestMemoryOrderModel(std::size_t num_cores = 8);

    void reset();

    // Core event recording
    MemoryAccessEvent record_read(std::size_t core_id, std::uint64_t addr, std::size_t size,
                                 std::uint64_t val, MemoryAccessType type = MemoryAccessType::NormalRead);

    MemoryAccessEvent record_write(std::size_t core_id, std::uint64_t addr, std::size_t size,
                                  std::uint64_t val, MemoryAccessType type = MemoryAccessType::NormalWrite);

    MemoryAccessEvent record_dmb(std::size_t core_id, std::uint8_t crm);
    MemoryAccessEvent record_dsb(std::size_t core_id, std::uint8_t crm);
    MemoryAccessEvent record_isb(std::size_t core_id, std::uint8_t crm = 0xF);

    // Drain / Commit store buffers
    void drain_store_buffer(std::size_t core_id);
    void drain_all_store_buffers();

    // Check if an event B is strictly ordered after event A
    [[nodiscard]] bool is_ordered(const MemoryAccessEvent& a, const MemoryAccessEvent& b) const noexcept;

    // Evaluates whether a litmus test outcome is architecturally allowed or forbidden
    [[nodiscard]] static LitmusOutcomeVerdict evaluate_sb_outcome(
        std::uint64_t r0_val, std::uint64_t r1_val) noexcept;

    [[nodiscard]] static LitmusOutcomeVerdict evaluate_mp_outcome(
        bool has_release_acquire, std::uint64_t flag_val, std::uint64_t data_val) noexcept;

    [[nodiscard]] static LitmusOutcomeVerdict evaluate_lb_outcome(
        std::uint64_t r0_val, std::uint64_t r1_val) noexcept;

    [[nodiscard]] static LitmusOutcomeVerdict evaluate_iriw_outcome(
        std::uint64_t c2_r0, std::uint64_t c2_r1,
        std::uint64_t c3_r0, std::uint64_t c3_r1) noexcept;

    [[nodiscard]] static LitmusOutcomeVerdict evaluate_wrc_outcome(
        bool has_barriers, std::uint64_t c2_rx) noexcept;

    [[nodiscard]] static LitmusOutcomeVerdict evaluate_rwc_outcome(
        bool has_barriers, std::uint64_t c1_ry, std::uint64_t c2_rx) noexcept;

    // Per-core accessors
    [[nodiscard]] CoreOrderingState& get_core_state(std::size_t core_id);
    [[nodiscard]] const CoreOrderingState& get_core_state(std::size_t core_id) const;

    // Global observation log
    [[nodiscard]] const std::vector<MemoryAccessEvent>& global_log() const noexcept { return global_log_; }
    [[nodiscard]] std::size_t total_events() const noexcept { return global_log_.size(); }

    // Deterministic state hash of memory ordering execution
    [[nodiscard]] std::uint64_t compute_ordering_state_hash() const noexcept;

private:
    mutable std::mutex mutex_{};
    std::vector<CoreOrderingState> cores_{};
    std::vector<MemoryAccessEvent> global_log_{};
    std::uint64_t next_global_seq_{1};
};

} // namespace winios

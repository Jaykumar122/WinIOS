#pragma once

#include "sndl/sndl_types.h"
#include "sndl/sndl_writer.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace winios::sndl {

// -----------------------------------------------------------------------------
// SndlRecorder: Central Recording Engine for Nondeterministic Events
// -----------------------------------------------------------------------------
class SndlRecorder {
public:
    explicit SndlRecorder(std::size_t queue_capacity = kDefaultRecorderQueueCapacity);
    ~SndlRecorder();

    SndlRecorder(const SndlRecorder&) = delete;
    SndlRecorder& operator=(const SndlRecorder&) = delete;
    SndlRecorder(SndlRecorder&&) = delete;
    SndlRecorder& operator=(SndlRecorder&&) = delete;

    // --- Lifecycle Management ---
    [[nodiscard]] SndlResult start_recording(const std::filesystem::path& output_path,
                                             std::uint16_t core_count = 1,
                                             std::uint64_t base_snapshot_hash = 0,
                                             const std::array<std::uint8_t, 12>& snapshot_id = {});

    [[nodiscard]] SndlResult stop_recording(std::uint64_t total_instructions = 0);
    void abort_recording() noexcept;

    // --- Event Submission (Thread-Safe) ---
    [[nodiscard]] SndlResult submit_record(SndlRecord record);

    // Source-specific submission helpers
    void record_mmio_read(std::uint16_t core_id, std::uint64_t pc,
                          std::uint64_t ret_count, std::uint64_t paddr,
                          std::uint64_t value, unsigned width,
                          std::uint32_t source_id = 0);

    void record_interrupt_inject(std::uint16_t core_id, std::uint32_t intid,
                                 std::uint8_t priority, std::uint32_t flags = 0,
                                 std::uint32_t source_id = 0);

    void record_interrupt_delivery(std::uint16_t core_id, std::uint64_t pc,
                                   std::uint64_t ret_count, std::uint32_t intid,
                                   std::uint8_t priority, bool is_fiq,
                                   bool is_virtual, unsigned target_el);

    void record_timer_expiration(std::uint16_t core_id, std::uint64_t pc,
                                 std::uint64_t ret_count, std::uint8_t timer_type,
                                 std::uint64_t cval, std::uint64_t counter);

    void record_counter_read(std::uint16_t core_id, std::uint64_t pc,
                             std::uint64_t ret_count, std::uint32_t reg_key,
                             std::uint64_t counter_val, std::uint64_t frequency);

    void record_entropy(std::uint16_t core_id, std::uint64_t pc,
                        std::uint64_t ret_count, std::uint64_t value,
                        unsigned bit_width = 64, std::uint32_t source_id = 0);

    void record_host_input(std::uint16_t core_id, std::uint32_t channel_id,
                           std::uint64_t data_payload, std::size_t length);

    // --- Status and Statistics ---
    [[nodiscard]] SndlLifecycleState state() const noexcept { return state_.load(std::memory_order_acquire); }
    [[nodiscard]] bool is_recording() const noexcept { return state() == SndlLifecycleState::Recording; }
    [[nodiscard]] bool has_overflowed() const noexcept { return overflowed_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t total_recorded() const noexcept { return total_recorded_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t dropped_records() const noexcept { return dropped_records_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::size_t queue_capacity() const noexcept { return queue_capacity_; }
    [[nodiscard]] std::uint16_t core_count() const noexcept { return core_count_; }

private:
    std::size_t queue_capacity_{kDefaultRecorderQueueCapacity};
    std::atomic<SndlLifecycleState> state_{SndlLifecycleState::Idle};
    std::atomic<bool> overflowed_{false};
    std::atomic<std::uint64_t> sequence_counter_{0};
    std::atomic<std::uint64_t> total_recorded_{0};
    std::atomic<std::uint64_t> dropped_records_{0};
    std::uint16_t core_count_{1};

    // Sub-instruction ordinal tracking per core
    struct PerCoreTracking {
        std::uint64_t last_ret_count{0};
        std::uint16_t next_sub_ordinal{0};
    };
    std::mutex per_core_mutex_;
    std::vector<PerCoreTracking> core_tracking_;

    // Writer and buffer synchronization
    std::mutex queue_mutex_;
    SndlWriter writer_;
};

} // namespace winios::sndl

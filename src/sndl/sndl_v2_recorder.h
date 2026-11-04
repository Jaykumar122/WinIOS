#pragma once

#include "sndl/sndl_v2_types.h"
#include "sndl/sndl_v2_writer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace winios::sndl {

struct SndlV2RecorderOptions {
    std::uint16_t core_count{1};
    std::uint64_t memory_capacity{0};
    std::uint64_t base_snapshot_hash{0};
    std::uint64_t base_snapshot_file_size{0};
    std::uint64_t initial_global_sequence{0};
    std::uint32_t records_per_block{kDefaultV2RecordsPerBlock};
};

class SndlV2Recorder {
public:
    explicit SndlV2Recorder(SndlV2RecorderOptions options = {});
    ~SndlV2Recorder();

    // Start recording session to destination file
    [[nodiscard]] SndlV2Result start_recording(const std::filesystem::path& output_path);

    // Stop and finalize recording session
    [[nodiscard]] SndlV2Result stop_recording(SndlV2CompletionStatus status = SndlV2CompletionStatus::Completed);

    [[nodiscard]] bool is_recording() const noexcept { return is_recording_; }
    [[nodiscard]] std::uint16_t core_count() const noexcept { return options_.core_count; }
    [[nodiscard]] std::uint64_t global_event_sequence() const noexcept { return current_global_sequence_; }
    [[nodiscard]] std::uint64_t global_retirement_sequence() const noexcept { return global_retirement_sequence_; }
    [[nodiscard]] std::uint64_t core_retirement_count(std::size_t core_id) const noexcept {
        if (core_id < core_retirement_counts_.size()) {
            return core_retirement_counts_[core_id];
        }
        return 0;
    }

    // Monotonic sequence updates from execution engine
    void notify_instruction_retired(std::size_t core_id, std::uint64_t count = 1) noexcept;
    void sync_global_retirement_sequence(std::uint64_t seq) noexcept;
    void sync_core_retirement_count(std::size_t core_id, std::uint64_t count) noexcept;

    // --- High-level Event Recording APIs ---

    // Lifecycle
    void record_core_lifecycle(std::size_t core_id, SndlV2EventType event_type, std::uint64_t pc, std::uint64_t extra = 0);

    // Scheduler
    void record_quantum_start(std::size_t core_id, std::uint64_t quantum_budget, std::uint64_t pc);
    void record_quantum_end(std::size_t core_id, std::uint64_t instructions_executed, std::uint32_t reason, std::uint64_t pc);
    void record_early_truncation(std::size_t core_id, std::uint32_t reason, std::uint64_t pc);
    void record_core_selected(std::size_t core_id, std::uint32_t reason, std::uint64_t pc);
    void record_barrier_entry(std::size_t core_id, std::uint64_t barrier_id, std::uint64_t pc);
    void record_barrier_release(std::size_t core_id, std::uint64_t barrier_id, std::uint64_t pc);

    // Interrupts & Exceptions
    void record_external_interrupt_asserted(std::uint32_t source_id, std::uint32_t intid, std::size_t target_core);
    void record_interrupt_routed(std::uint32_t intid, std::size_t from_core, std::size_t to_core);
    void record_interrupt_accepted(std::size_t core_id, std::uint32_t intid, std::uint32_t exception_level);
    void record_exception_taken(std::size_t core_id, std::uint64_t syndrome, std::uint32_t target_el, std::uint64_t target_pc);
    void record_exception_returned(std::size_t core_id, std::uint32_t target_el, std::uint64_t target_pc);

    // Timers & Counters
    void record_cntpct_read(std::size_t core_id, std::uint64_t value, std::uint64_t pc);
    void record_cntvct_read(std::size_t core_id, std::uint64_t value, std::uint64_t pc);
    void record_pmccntr_read(std::size_t core_id, std::uint64_t value, std::uint64_t pc);
    void record_timer_expired(std::size_t core_id, std::uint32_t timer_id, std::uint64_t value);

    // Nondeterministic IO & Entropy
    void record_mmio_read(std::size_t core_id, std::uint64_t paddr, std::uint64_t value, std::uint32_t width_bytes, std::uint64_t pc);
    void record_entropy_read(std::size_t core_id, std::uint64_t entropy_val, std::uint64_t pc);
    void record_host_input(std::uint32_t source_id, std::uint64_t payload);

    // Shared Memory & Exclusives
    void record_memory_barrier(std::size_t core_id, std::uint32_t barrier_type, std::uint64_t pc);
    void record_reservation_set(std::size_t core_id, std::uint64_t paddr, std::uint64_t pc);
    void record_reservation_cleared(std::size_t core_id, std::uint64_t paddr, std::uint64_t pc);
    void record_store_exclusive_result(std::size_t core_id, std::uint64_t paddr, bool success, std::uint64_t pc);

    // Direct event injection (low-level)
    void record_event(SndlV2Record record);

    [[nodiscard]] std::uint64_t total_events_recorded() const noexcept { return writer_.total_events_written(); }
    [[nodiscard]] std::uint64_t dropped_events() const noexcept { return writer_.dropped_events(); }

private:
    [[nodiscard]] std::uint64_t next_sequence() noexcept;

    SndlV2RecorderOptions options_;
    SndlV2Writer writer_;
    std::vector<std::uint64_t> core_retirement_counts_;
    std::uint64_t current_global_sequence_{0};
    std::uint64_t global_retirement_sequence_{0};
    bool is_recording_{false};
    mutable std::mutex mutex_;
};

} // namespace winios::sndl

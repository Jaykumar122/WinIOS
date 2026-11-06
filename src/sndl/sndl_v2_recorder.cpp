#include <atomic>
#include "sndl/sndl_v2_recorder.h"
#include <algorithm>

namespace winios::sndl {

SndlV2Recorder::SndlV2Recorder(SndlV2RecorderOptions options)
    : options_(options),
      writer_(SndlV2WriterConfig{options_.records_per_block, kDefaultV2RecorderQueueCapacity}),
      core_retirement_counts_(options_.core_count > 0 ? options_.core_count : 1, 0),
      current_global_sequence_(options_.initial_global_sequence),
      global_retirement_sequence_(0) {}

SndlV2Recorder::~SndlV2Recorder() {
    if (is_recording_) {
        (void)stop_recording(SndlV2CompletionStatus::Aborted);
    }
}

SndlV2Result SndlV2Recorder::start_recording(const std::filesystem::path& output_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_recording_) {
        return SndlV2Result::fail(SndlV2ErrorCode::InvalidStateTransition, "Recorder already active");
    }

    if (options_.core_count == 0) {
        options_.core_count = 1;
    }
    core_retirement_counts_.assign(options_.core_count, 0);
    current_global_sequence_ = options_.initial_global_sequence;
    global_retirement_sequence_ = 0;

    session_id_ = options_.session_id ? options_.session_id
        : (options_.base_snapshot_hash ? (options_.base_snapshot_hash ^ 0x534E444C56325345ull) : 0xDEADBEEFCAFE0001ull);

    WSndlV2Header hdr{};
    hdr.magic = kWSndlV2Magic;
    hdr.version = kWSndlVersion2;
    hdr.core_count = options_.core_count;
    hdr.flags = 0;
    hdr.compression_type = 0;
    hdr.base_snapshot_hash = options_.base_snapshot_hash;
    hdr.base_snapshot_file_size = options_.base_snapshot_file_size;
    hdr.memory_capacity = options_.memory_capacity;
    hdr.initial_global_sequence = options_.initial_global_sequence;

    const auto res = writer_.open(output_path, hdr);
    if (res.is_failure()) {
        return res;
    }

    if (options_.emit_session_metadata || options_.session_id != 0) {
        // Emit session metadata record binding session_id and base snapshot
        SndlV2Record meta{};
        meta.global_event_sequence = next_sequence();
        meta.global_retirement_sequence = 0;
        meta.core_retirement_count = 0;
        meta.pc = 0;
        meta.event_type = SndlV2EventType::RecordingSessionMetadata;
        meta.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
        meta.core_id = 0;
        meta.source_id = 0;
        meta.payload1 = session_id_;
        meta.payload2 = options_.base_snapshot_hash;
        meta.payload3 = options_.memory_capacity;
        meta.flags = static_cast<std::uint32_t>(device::RecordingLifecycleState::Recording);
        meta.payload_size = 32;
        (void)writer_.write_record(meta);
    }

    const auto dma_res = dma_payload_store_.open_write(output_path.string() + ".dma", session_id_, options_.base_snapshot_hash);
    if (dma_res.is_failure()) {
        (void)writer_.close();
        return SndlV2Result::fail(SndlV2ErrorCode::FileWriteFailed, dma_res.message);
    }

    is_recording_ = true;
    return SndlV2Result::ok();
}

SndlV2Result SndlV2Recorder::stop_recording(SndlV2CompletionStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_recording_) {
        return SndlV2Result::ok();
    }

    const auto lifecycle = (status == SndlV2CompletionStatus::Completed)
        ? device::RecordingLifecycleState::Complete
        : device::RecordingLifecycleState::Aborted;

    const auto dma_res = dma_payload_store_.finalize(lifecycle);
    if (dma_res.is_failure() && status == SndlV2CompletionStatus::Completed) {
        status = SndlV2CompletionStatus::Incomplete;
    }

    is_recording_ = false;
    return writer_.finalize(status);
}

std::uint64_t SndlV2Recorder::next_sequence() noexcept {
    return ++current_global_sequence_;
}

void SndlV2Recorder::notify_instruction_retired(std::size_t core_id, std::uint64_t count) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (core_id < core_retirement_counts_.size()) {
        core_retirement_counts_[core_id] += count;
    }
    global_retirement_sequence_ += count;
}

void SndlV2Recorder::sync_global_retirement_sequence(std::uint64_t seq) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    global_retirement_sequence_ = std::max(global_retirement_sequence_, seq);
}

void SndlV2Recorder::sync_core_retirement_count(std::size_t core_id, std::uint64_t count) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (core_id >= core_retirement_counts_.size()) {
        core_retirement_counts_.resize(core_id + 1, 0);
    }
    core_retirement_counts_[core_id] = std::max(core_retirement_counts_[core_id], count);
}

void SndlV2Recorder::record_event(SndlV2Record record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_recording_) return;

    if (record.global_event_sequence == 0) {
        record.global_event_sequence = next_sequence();
    } else {
        current_global_sequence_ = std::max(current_global_sequence_, record.global_event_sequence);
    }

    if (record.global_retirement_sequence == 0) {
        record.global_retirement_sequence = global_retirement_sequence_;
    }

    if (record.core_id < core_retirement_counts_.size()) {
        if (record.core_retirement_count == 0) {
            record.core_retirement_count = core_retirement_counts_[record.core_id];
        }
    }

    (void)writer_.write_record(record);
}

void SndlV2Recorder::record_core_lifecycle(std::size_t core_id, SndlV2EventType event_type, std::uint64_t pc, std::uint64_t extra) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = event_type;
    rec.boundary_type = SndlV2BoundaryType::CoreLifecycleBoundary;
    rec.pc = pc;
    rec.payload1 = extra;
    record_event(rec);
}

void SndlV2Recorder::record_quantum_start(std::size_t core_id, std::uint64_t quantum_budget, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::QuantumStart;
    rec.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
    rec.pc = pc;
    rec.payload1 = quantum_budget;
    record_event(rec);
}

void SndlV2Recorder::record_quantum_end(std::size_t core_id, std::uint64_t instructions_executed, std::uint32_t reason, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::QuantumEnd;
    rec.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
    rec.pc = pc;
    rec.payload1 = instructions_executed;
    rec.payload2 = reason;
    record_event(rec);
}

void SndlV2Recorder::record_early_truncation(std::size_t core_id, std::uint32_t reason, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::EarlyTruncation;
    rec.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
    rec.pc = pc;
    rec.payload1 = reason;
    record_event(rec);
}

void SndlV2Recorder::record_core_selected(std::size_t core_id, std::uint32_t reason, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::CoreSelected;
    rec.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
    rec.pc = pc;
    rec.payload1 = reason;
    record_event(rec);
}

void SndlV2Recorder::record_barrier_entry(std::size_t core_id, std::uint64_t barrier_id, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::BarrierEntry;
    rec.boundary_type = SndlV2BoundaryType::BarrierBoundary;
    rec.pc = pc;
    rec.payload1 = barrier_id;
    record_event(rec);
}

void SndlV2Recorder::record_barrier_release(std::size_t core_id, std::uint64_t barrier_id, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::BarrierRelease;
    rec.boundary_type = SndlV2BoundaryType::BarrierBoundary;
    rec.pc = pc;
    rec.payload1 = barrier_id;
    record_event(rec);
}

void SndlV2Recorder::record_external_interrupt_asserted(std::uint32_t source_id, std::uint32_t intid, std::size_t target_core) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(target_core);
    rec.source_id = source_id;
    rec.event_type = SndlV2EventType::ExternalInterruptAsserted;
    rec.boundary_type = SndlV2BoundaryType::AsynchronousInput;
    rec.payload1 = intid;
    record_event(rec);
}

void SndlV2Recorder::record_interrupt_routed(std::uint32_t intid, std::size_t from_core, std::size_t to_core) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(to_core);
    rec.event_type = SndlV2EventType::InterruptRouted;
    rec.boundary_type = SndlV2BoundaryType::BetweenInstructions;
    rec.payload1 = intid;
    rec.payload2 = from_core;
    record_event(rec);
}

void SndlV2Recorder::record_interrupt_accepted(std::size_t core_id, std::uint32_t intid, std::uint32_t exception_level) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::InterruptAccepted;
    rec.boundary_type = SndlV2BoundaryType::ExceptionBoundary;
    rec.payload1 = intid;
    rec.payload2 = exception_level;
    record_event(rec);
}

void SndlV2Recorder::record_exception_taken(std::size_t core_id, std::uint64_t syndrome, std::uint32_t target_el, std::uint64_t target_pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::ExceptionTaken;
    rec.boundary_type = SndlV2BoundaryType::ExceptionBoundary;
    rec.pc = target_pc;
    rec.payload1 = syndrome;
    rec.payload2 = target_el;
    record_event(rec);
}

void SndlV2Recorder::record_exception_returned(std::size_t core_id, std::uint32_t target_el, std::uint64_t target_pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::ExceptionReturned;
    rec.boundary_type = SndlV2BoundaryType::ExceptionBoundary;
    rec.pc = target_pc;
    rec.payload1 = target_el;
    record_event(rec);
}

void SndlV2Recorder::record_cntpct_read(std::size_t core_id, std::uint64_t value, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::CntpctRead;
    rec.boundary_type = SndlV2BoundaryType::DuringInstruction;
    rec.pc = pc;
    rec.payload1 = value;
    record_event(rec);
}

void SndlV2Recorder::record_cntvct_read(std::size_t core_id, std::uint64_t value, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::CntvctRead;
    rec.boundary_type = SndlV2BoundaryType::DuringInstruction;
    rec.pc = pc;
    rec.payload1 = value;
    record_event(rec);
}

void SndlV2Recorder::record_pmccntr_read(std::size_t core_id, std::uint64_t value, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::PmccntrRead;
    rec.boundary_type = SndlV2BoundaryType::DuringInstruction;
    rec.pc = pc;
    rec.payload1 = value;
    record_event(rec);
}

void SndlV2Recorder::record_timer_expired(std::size_t core_id, std::uint32_t timer_id, std::uint64_t value) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::TimerComparatorExpired;
    rec.boundary_type = SndlV2BoundaryType::AsynchronousInput;
    rec.payload1 = timer_id;
    rec.payload2 = value;
    record_event(rec);
}

void SndlV2Recorder::record_mmio_read(std::size_t core_id, std::uint64_t paddr, std::uint64_t value, std::uint32_t width_bytes, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::MmioReadResult;
    rec.boundary_type = SndlV2BoundaryType::DuringInstruction;
    rec.pc = pc;
    rec.payload1 = paddr;
    rec.payload2 = value;
    rec.payload3 = width_bytes;
    rec.payload_size = static_cast<std::uint16_t>(width_bytes);
    record_event(rec);
}

void SndlV2Recorder::record_entropy_read(std::size_t core_id, std::uint64_t entropy_val, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::EntropyReadResult;
    rec.boundary_type = SndlV2BoundaryType::DuringInstruction;
    rec.pc = pc;
    rec.payload1 = entropy_val;
    record_event(rec);
}

void SndlV2Recorder::record_host_input(std::uint32_t source_id, std::uint64_t payload) {
    SndlV2Record rec{};
    rec.source_id = source_id;
    rec.event_type = SndlV2EventType::HostInputInjected;
    rec.boundary_type = SndlV2BoundaryType::AsynchronousInput;
    rec.payload1 = payload;
    record_event(rec);
}

void SndlV2Recorder::record_memory_barrier(std::size_t core_id, std::uint32_t barrier_type, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::MemoryBarrierSync;
    rec.boundary_type = SndlV2BoundaryType::BarrierBoundary;
    rec.pc = pc;
    rec.payload1 = barrier_type;
    record_event(rec);
}

void SndlV2Recorder::record_reservation_set(std::size_t core_id, std::uint64_t paddr, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::ReservationSet;
    rec.boundary_type = SndlV2BoundaryType::DuringInstruction;
    rec.pc = pc;
    rec.payload1 = paddr;
    record_event(rec);
}

void SndlV2Recorder::record_reservation_cleared(std::size_t core_id, std::uint64_t paddr, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::ReservationCleared;
    rec.boundary_type = SndlV2BoundaryType::DuringInstruction;
    rec.pc = pc;
    rec.payload1 = paddr;
    record_event(rec);
}

void SndlV2Recorder::record_store_exclusive_result(std::size_t core_id, std::uint64_t paddr, bool success, std::uint64_t pc) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.event_type = SndlV2EventType::StoreExclusiveResult;
    rec.boundary_type = SndlV2BoundaryType::DuringInstruction;
    rec.pc = pc;
    rec.payload1 = paddr;
    rec.payload2 = success ? 1 : 0;
    record_event(rec);
}

void SndlV2Recorder::record_device_register_read(std::size_t core_id, std::uint32_t device_id, std::uint64_t reg_offset, std::uint64_t val, std::uint32_t width) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.source_id = device_id;
    rec.event_type = SndlV2EventType::DeviceRegisterRead;
    rec.boundary_type = SndlV2BoundaryType::DuringInstruction;
    rec.payload1 = reg_offset;
    rec.payload2 = val;
    rec.payload3 = width;
    record_event(rec);
}

void SndlV2Recorder::record_device_register_write(std::size_t core_id, std::uint32_t device_id, std::uint64_t reg_offset, std::uint64_t val, std::uint32_t width) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.source_id = device_id;
    rec.event_type = SndlV2EventType::DeviceRegisterWrite;
    rec.boundary_type = SndlV2BoundaryType::DuringInstruction;
    rec.payload1 = reg_offset;
    rec.payload2 = val;
    rec.payload3 = width;
    record_event(rec);
}

void SndlV2Recorder::record_dma_submission_accepted(std::size_t core_id, std::uint32_t device_id, std::uint64_t desc_addr, std::uint64_t dst_paddr, std::uint64_t length, std::uint32_t op_type, std::uint32_t extra_flags) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.source_id = device_id;
    rec.event_type = SndlV2EventType::DmaSubmissionAccepted;
    rec.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
    rec.payload1 = desc_addr;
    rec.payload2 = dst_paddr;
    rec.payload3 = (length & 0xFFFFFFFFull) | ((static_cast<std::uint64_t>(op_type) & 0xFFull) << 32);
    rec.flags = extra_flags;
    record_event(rec);
}

void SndlV2Recorder::record_dma_submission_accepted(std::size_t core_id, std::uint32_t device_id, std::uint64_t desc_addr, std::uint64_t length, std::uint32_t op_type) {
    record_dma_submission_accepted(core_id, device_id, desc_addr, 0, length, op_type, 0);
}

void SndlV2Recorder::record_dma_submission_rejected(std::size_t core_id, std::uint32_t device_id, std::uint64_t desc_addr, std::uint32_t error_code) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.source_id = device_id;
    rec.event_type = SndlV2EventType::DmaSubmissionRejected;
    rec.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
    rec.payload1 = desc_addr;
    rec.payload2 = error_code;
    record_event(rec);
}

void SndlV2Recorder::record_dma_transfer_completed(std::size_t core_id, std::uint32_t device_id, std::uint64_t desc_addr, std::uint64_t bytes_transferred, std::uint32_t descriptors_processed) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.source_id = device_id;
    rec.event_type = SndlV2EventType::DmaTransferCompleted;
    rec.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
    rec.payload1 = desc_addr;
    rec.payload2 = bytes_transferred;
    rec.payload3 = descriptors_processed;
    record_event(rec);
}

void SndlV2Recorder::record_dma_transfer_failed(std::size_t core_id, std::uint32_t device_id, std::uint64_t desc_addr, std::uint32_t error_code) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.source_id = device_id;
    rec.event_type = SndlV2EventType::DmaTransferFailed;
    rec.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
    rec.payload1 = desc_addr;
    rec.payload2 = error_code;
    record_event(rec);
}

void SndlV2Recorder::record_device_interrupt_asserted(std::size_t core_id, std::uint32_t device_id, std::uint32_t irq_line) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.source_id = device_id;
    rec.event_type = SndlV2EventType::DeviceInterruptAsserted;
    rec.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
    rec.payload1 = irq_line;
    record_event(rec);
}

void SndlV2Recorder::record_dma_chunk(std::size_t core_id, std::uint32_t device_id, std::uint64_t chunk_dst_paddr, const std::uint8_t* chunk_data, std::uint16_t chunk_len, std::uint16_t chunk_idx) {
    SndlV2Record rec{};
    rec.core_id = static_cast<std::uint32_t>(core_id);
    rec.source_id = device_id;
    rec.event_type = SndlV2EventType::DmaDataChunk;
    rec.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
    rec.payload1 = chunk_dst_paddr;

    std::uint64_t p2 = 0;
    std::uint64_t p3 = 0;
    if (chunk_data && chunk_len > 0) {
        std::size_t n1 = std::min<std::size_t>(chunk_len, 8);
        std::memcpy(&p2, chunk_data, n1);
        if (chunk_len > 8) {
            std::size_t n2 = std::min<std::size_t>(chunk_len - 8, 8);
            std::memcpy(&p3, chunk_data + 8, n2);
        }
    }
    rec.payload2 = p2;
    rec.payload3 = p3;
    rec.flags = (static_cast<std::uint32_t>(chunk_idx) << 16) | chunk_len;
    rec.payload_size = chunk_len;
    record_event(rec);
}

void SndlV2Recorder::record_dma_copy_payload(std::uint64_t desc_id, std::uint64_t dst_paddr, const std::uint8_t* data, std::size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)dma_payload_store_.record_payload(current_global_sequence_, desc_id, dst_paddr, data, size);
}

} // namespace winios::sndl

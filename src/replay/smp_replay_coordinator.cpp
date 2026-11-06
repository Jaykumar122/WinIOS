#include "replay/smp_replay_coordinator.h"
#include "snapshot/smp_snapshot_manager.h"
#include "trace/trace_types.h"

#include <chrono>
#include <cstddef>
#include <iostream>

namespace winios::replay {

SmpReplayCoordinator::SmpReplayCoordinator(SmpReplayCapabilityConfig config)
    : config_(config) {
}

SmpReplayCoordinator::~SmpReplayCoordinator() {
    close();
}

void SmpReplayCoordinator::close() noexcept {
    cursor_.close();
    dma_payload_store_.clear();
    dma_device_.reset();
    device_bus_.reset();
    cpus_.clear();
    core_replay_states_.clear();
    scheduler_.reset();
    shared_memory_.reset();
    wsnap_path_.clear();
    sndl_path_.clear();
    active_core_count_ = 2;
    state_ = SmpReplayState::Uninitialized;
    divergence_ = {};
    stats_ = {};
    baseline_snapshot_hash_ = 0;
}

void SmpReplayCoordinator::trigger_divergence(SmpReplayDivergence div) {
    if (!divergence_.has_diverged()) {
        divergence_ = std::move(div);
        state_ = SmpReplayState::Diverged;
        stats_.final_state = SmpReplayState::Diverged;
        stats_.divergence = divergence_;
    }
}

ReplayResult SmpReplayCoordinator::prepare(
    const std::filesystem::path& wsnap_path,
    const std::filesystem::path& sndl_path) {

    close();
    wsnap_path_ = wsnap_path;
    sndl_path_ = sndl_path;
    state_ = SmpReplayState::Preparing;

    // 0. Configuration Sanity & Capability Validation
    if (config_.allow_unvirtualized_wall_clock) {
        state_ = SmpReplayState::Unsupported;
        return ReplayResult::fail(
            ReplayErrorCode::UnsupportedConfiguration,
            "BLK-SMP-04 violation: unvirtualized host wall-clock replay is unsupported."
        );
    }

    if (config_.clock_source == SmpReplayClockSource::UnsupportedHostClock) {
        state_ = SmpReplayState::Unsupported;
        return ReplayResult::fail(
            ReplayErrorCode::UnsupportedConfiguration,
            "Unsupported host clock source cannot be used for deterministic replay."
        );
    }

    if (config_.allow_uncaptured_dma) {
        state_ = SmpReplayState::Unsupported;
        return ReplayResult::fail(
            ReplayErrorCode::UnsupportedConfiguration,
            "BLK-SMP-03 violation: uncaptured host DMA replay is unsupported."
        );
    }

    if (config_.allow_dma_replay && !config_.allow_dma_recording) {
        state_ = SmpReplayState::Unsupported;
        return ReplayResult::fail(
            ReplayErrorCode::UnsupportedConfiguration,
            "Unsafe capability combination: allow_dma_replay requires allow_dma_recording."
        );
    }

    // 1. Snapshot Pre-Flight Validation
    const auto snap_val = snapshot::SmpSnapshotManager::validate_file(wsnap_path);
    if (!snap_val.valid) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::SnapshotMismatch;
        d.message = "Snapshot validation failed: " + snap_val.error_message;
        trigger_divergence(d);
        return ReplayResult::fail(ReplayErrorCode::SnapshotValidationFailed, d.message);
    }

    snapshot::SmpSnapshotInspection snap_info{};
    const auto inspect_res = snapshot::SmpSnapshotManager::inspect_file(wsnap_path, snap_info);
    if (!inspect_res.success) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::SnapshotMismatch;
        d.message = "Snapshot inspection failed: " + inspect_res.error_message;
        trigger_divergence(d);
        return ReplayResult::fail(ReplayErrorCode::SnapshotValidationFailed, d.message);
    }

    // 2. Strict Capability Gate on Snapshot Core Count
    const std::uint32_t snap_cores = snap_info.header.core_count;
    if (snap_cores == 8) {
        if (!config_.allow_experimental_smp || !config_.allow_eight_core_replay) {
            state_ = SmpReplayState::Unsupported;
            return ReplayResult::fail(
                ReplayErrorCode::UnsupportedConfiguration,
                "Eight-core SMP replay is experimental and requires allow_experimental_smp = true and allow_eight_core_replay = true in capability config."
            );
        }
    } else if (snap_cores == 4) {
        if (!config_.allow_four_core_replay) {
            state_ = SmpReplayState::Unsupported;
            return ReplayResult::fail(
                ReplayErrorCode::UnsupportedConfiguration,
                "Four-core SMP replay is experimental and requires allow_four_core_replay = true in capability config."
            );
        }
    } else if (snap_cores == 2 || snap_cores == 1) {
        // Supported
    } else {
        state_ = SmpReplayState::Unsupported;
        return ReplayResult::fail(
            ReplayErrorCode::UnsupportedConfiguration,
            "Multi-core SMP replay capability gate: unsupported core count (" +
            std::to_string(snap_cores) + " cores). Only 1-core, 2-core, experimental 4-core, and experimental 8-core replay are supported."
        );
    }

    // 3. Open SNDL v2 Event Cursor
    const auto sndl_res = cursor_.open(sndl_path, config_.enforce_strict_monotonicity);
    if (sndl_res.is_failure()) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::IncompleteRecording;
        d.message = "Failed to open SNDL v2 recording: " + sndl_res.message;
        trigger_divergence(d);
        return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, d.message);
    }

    // 4. Strict Capability Gate on SNDL Recording
    if (cursor_.header().core_count == 8) {
        if (!config_.allow_experimental_smp || !config_.allow_eight_core_replay) {
            state_ = SmpReplayState::Unsupported;
            return ReplayResult::fail(
                ReplayErrorCode::UnsupportedConfiguration,
                "Eight-core SMP recording is experimental and requires allow_eight_core_replay = true in capability config."
            );
        }
    }

    if (cursor_.header().core_count != snap_cores) {
        state_ = SmpReplayState::Unsupported;
        return ReplayResult::fail(
            ReplayErrorCode::UnsupportedConfiguration,
            "Core count mismatch: snapshot specifies " + std::to_string(snap_cores) +
            " cores but SNDL specifies " + std::to_string(cursor_.header().core_count) + " cores."
        );
    }

    if (config_.require_completed_footer) {
        if (!cursor_.has_footer()) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::IncompleteRecording;
            d.message = "SNDL v2 recording lacks a valid clean footer (incomplete recording)";
            trigger_divergence(d);
            return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, d.message);
        }

        if (cursor_.footer().completion_status != sndl::SndlV2CompletionStatus::Completed &&
            cursor_.footer().completion_status != sndl::SndlV2CompletionStatus::RecoveredIncomplete) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::IncompleteRecording;
            d.message = "SNDL v2 recording was not cleanly completed (status: " +
                         std::string(sndl::sndl_v2_completion_status_name(cursor_.footer().completion_status)) + ")";
            trigger_divergence(d);
            return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, d.message);
        }

        if (cursor_.footer().dropped_events > 0) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::IncompleteRecording;
            d.message = "SNDL v2 recording suffered buffer overflow with " +
                         std::to_string(cursor_.footer().dropped_events) + " dropped events";
            trigger_divergence(d);
            return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, d.message);
        }
    }

    // 5. Verify Canonical Cluster Hash Association
    const std::uint64_t snap_cluster_hash = snap_info.integrity_data.canonical_cluster_hash;
    if (config_.require_snapshot_association && cursor_.header().base_snapshot_hash != 0) {
        if (cursor_.header().base_snapshot_hash != snap_cluster_hash) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::SnapshotMismatch;
            d.message = "SNDL base snapshot hash does not match baseline snapshot canonical cluster hash";
            trigger_divergence(d);
            return ReplayResult::fail(ReplayErrorCode::SnapshotMismatch, d.message);
        }
    }

    if (cursor_.header().memory_capacity != 0 && cursor_.header().memory_capacity != snap_info.header.memory_capacity) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::SnapshotMismatch;
        d.message = "Memory capacity mismatch between SNDL recording and snapshot";
        trigger_divergence(d);
        return ReplayResult::fail(ReplayErrorCode::SnapshotMismatch, d.message);
    }

    // 6. Instantiate Multicore Execution Cluster
    active_core_count_ = snap_cores;
    const std::size_t mem_bytes = static_cast<std::size_t>(snap_info.header.memory_capacity);
    shared_memory_ = std::make_shared<SmpSharedMemory>(mem_bytes, active_core_count_);
    scheduler_ = std::make_unique<SmpScheduler>(shared_memory_, active_core_count_);

    device_bus_ = std::make_shared<device::VirtualDeviceBus>();
    dma_device_ = std::make_shared<device::VirtualDmaDevice>(0x1000, 0x40, 32);
    dma_device_->set_shared_memory(shared_memory_);
    device_bus_->register_device(dma_device_);
    scheduler_->attach_device_bus(device_bus_);

    std::uint64_t session_id = 0;
    sndl::SndlV2Record first_rec{};
    if (cursor_.peek(first_rec).is_success()) {
        if (first_rec.event_type == sndl::SndlV2EventType::RecordingSessionMetadata) {
            session_id = first_rec.payload1;
        }
    }
    if (session_id == 0 && cursor_.header().base_snapshot_hash != 0) {
        session_id = cursor_.header().base_snapshot_hash ^ 0x534E444C56325345ull;
    }

    std::filesystem::path dma_path = sndl_path;
    dma_path.concat(".dma");
    if (std::filesystem::exists(dma_path)) {
        auto dma_res = dma_payload_store_.open_read(dma_path, session_id, snap_cluster_hash);
        if (dma_res.is_failure()) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::IncompleteRecording;
            d.message = "Failed to open companion DMA file: " + dma_res.message;
            trigger_divergence(d);
            return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, d.message);
        }
    }

    std::string cross_err;
    if (!validate_cross_file_integrity(sndl_path_, cross_err)) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::DmaDataMismatch;
        d.message = "Cross-file validation failed: " + cross_err;
        trigger_divergence(d);
        return ReplayResult::fail(ReplayErrorCode::SndlValidationFailed, d.message);
    }

    cpus_.clear();
    for (std::size_t c = 0; c < active_core_count_; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpu->set_core_id(c);
        cpus_.push_back(cpu);
        scheduler_->register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // 7. Restore Architectural State from Snapshot
    const auto restore_res = snapshot::SmpSnapshotManager::restore_from_file(wsnap_path, *scheduler_);
    if (!restore_res.success) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::SnapshotMismatch;
        d.message = "Snapshot restoration failed: " + restore_res.error_message;
        trigger_divergence(d);
        return ReplayResult::fail(ReplayErrorCode::SnapshotRestoreFailed, d.message);
    }

    // 8. Re-verify Restored Cluster Canonical Hash
    const std::uint64_t restored_hash = scheduler_->compute_canonical_multicore_hash();
    if (snap_cluster_hash != 0 && restored_hash != snap_cluster_hash) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::SnapshotMismatch;
        d.message = "Restored cluster canonical hash (0x" +
                     std::to_string(restored_hash) + ") does not match snapshot header canonical hash (0x" +
                     std::to_string(snap_cluster_hash) + ")";
        trigger_divergence(d);
        return ReplayResult::fail(ReplayErrorCode::SnapshotMismatch, d.message);
    }
    baseline_snapshot_hash_ = restored_hash;

    state_ = SmpReplayState::SnapshotRestored;

    // 9. Initialize Core Replay Tracking
    core_replay_states_.resize(active_core_count_);
    for (std::size_t c = 0; c < active_core_count_; ++c) {
        core_replay_states_[c].reset(c);
        core_replay_states_[c].total_instructions_retired = scheduler_->get_slot(c).total_instructions_retired;
        core_replay_states_[c].lifecycle = scheduler_->get_slot(c).state.lifecycle;
    }

    stats_ = {};
    stats_.per_core_retired.resize(active_core_count_, 0);

    state_ = SmpReplayState::Ready;
    return ReplayResult::ok();
}

bool SmpReplayCoordinator::process_pending_external_events() {
    while (cursor_.has_next()) {
        sndl::SndlV2Record rec{};
        if (cursor_.peek(rec).is_failure()) {
            break;
        }

        switch (rec.event_type) {
        case sndl::SndlV2EventType::ExternalInterruptAsserted: {
            sndl::SndlV2Record consumed{};
            (void)cursor_.consume_expected_type(sndl::SndlV2EventType::ExternalInterruptAsserted, consumed);
            scheduler_->interrupt_router().request_interrupt(
                consumed.core_id,
                static_cast<std::uint32_t>(consumed.payload1),
                static_cast<std::uint8_t>(consumed.payload2),
                consumed.source_id
            );
            stats_.total_events_consumed++;
            break;
        }
        case sndl::SndlV2EventType::InterruptRouted:
        case sndl::SndlV2EventType::InterruptAccepted:
        case sndl::SndlV2EventType::TimerComparatorExpired:
        case sndl::SndlV2EventType::ReservationSet:
        case sndl::SndlV2EventType::ReservationCleared:
        case sndl::SndlV2EventType::StoreExclusiveResult:
        case sndl::SndlV2EventType::MemoryBarrierSync:
        case sndl::SndlV2EventType::MmioReadResult:
        case sndl::SndlV2EventType::EntropyReadResult:
        case sndl::SndlV2EventType::RecordingSessionMetadata:
        case sndl::SndlV2EventType::ReplayCheckpoint:
        case sndl::SndlV2EventType::PayloadStoreBinding: {
            sndl::SndlV2Record consumed{};
            (void)cursor_.consume_expected_type(rec.event_type, consumed);
            stats_.total_events_consumed++;
            break;
        }
        case sndl::SndlV2EventType::CntpctRead:
        case sndl::SndlV2EventType::CntvctRead:
        case sndl::SndlV2EventType::PmccntrRead: {
            if (config_.clock_source == SmpReplayClockSource::UnsupportedHostClock) {
                SmpReplayDivergence d;
                d.type = SmpReplayDivergenceType::UnsupportedExternalInput;
                d.message = "Counter read encountered with UnsupportedHostClock clock source in capability config";
                d.global_event_sequence = rec.global_event_sequence;
                d.actual_event_type = rec.event_type;
                trigger_divergence(d);
                return false;
            }
            sndl::SndlV2Record consumed{};
            (void)cursor_.consume_expected_type(rec.event_type, consumed);
            stats_.total_events_consumed++;
            break;
        }
        case sndl::SndlV2EventType::HostInputInjected: {
            if (!config_.allow_uncaptured_dma) {
                SmpReplayDivergence d;
                d.type = SmpReplayDivergenceType::UnsupportedExternalInput;
                d.message = "HostInputInjected encountered but BLK-SMP-03 host DMA is not enabled in capability gate";
                d.global_event_sequence = rec.global_event_sequence;
                d.actual_event_type = rec.event_type;
                trigger_divergence(d);
                return false;
            }
            sndl::SndlV2Record consumed{};
            (void)cursor_.consume_expected_type(rec.event_type, consumed);
            stats_.total_events_consumed++;
            break;
        }
        case sndl::SndlV2EventType::DmaSubmissionAccepted: {
            if (!config_.allow_dma_replay) {
                SmpReplayDivergence d;
                d.type = SmpReplayDivergenceType::UnsupportedExternalInput;
                d.message = "DMA event encountered in recording but allow_dma_replay is false in capability config";
                d.global_event_sequence = rec.global_event_sequence;
                d.actual_event_type = rec.event_type;
                trigger_divergence(d);
                return false;
            }
            sndl::SndlV2Record consumed{};
            (void)cursor_.consume_expected_type(rec.event_type, consumed);
            stats_.total_events_consumed++;

            const std::uint64_t desc_addr = consumed.payload1;
            const std::uint64_t dst_paddr = consumed.payload2;
            const std::uint64_t length = consumed.payload3 & 0xFFFFFFFFull;
            const std::uint32_t op_type = static_cast<std::uint32_t>((consumed.payload3 >> 32) & 0xFFull);
            const std::uint32_t extra = consumed.flags;

            if (op_type == static_cast<std::uint32_t>(device::VirtualDmaOpType::Fill)) {
                std::vector<std::uint8_t> fill_buf(static_cast<std::size_t>(length));
                const std::uint32_t pat = extra;
                const std::uint8_t pat_bytes[4] = {
                    static_cast<std::uint8_t>(pat & 0xFFu),
                    static_cast<std::uint8_t>((pat >> 8u) & 0xFFu),
                    static_cast<std::uint8_t>((pat >> 16u) & 0xFFu),
                    static_cast<std::uint8_t>((pat >> 24u) & 0xFFu)
                };
                for (std::size_t b = 0; b < fill_buf.size(); ++b) {
                    fill_buf[b] = pat_bytes[b % 4];
                }
                shared_memory_->write_bytes(consumed.core_id, dst_paddr, fill_buf.data(), fill_buf.size());
                if (shared_memory_->exclusive_monitor()) {
                    shared_memory_->exclusive_monitor()->invalidate_reservations(
                        shared_memory_->exclusive_monitor()->max_cores(),
                        dst_paddr,
                        fill_buf.size()
                    );
                }

                const std::uint32_t success_status = static_cast<std::uint32_t>(device::VirtualDmaDescriptorStatus::Success);
                shared_memory_->write_bytes(consumed.core_id, desc_addr + offsetof(device::VirtualDmaDescriptor, status), reinterpret_cast<const std::uint8_t*>(&success_status), 4);
            } else if (op_type == static_cast<std::uint32_t>(device::VirtualDmaOpType::Copy)) {
                const auto* p_rec = dma_payload_store_.find_payload(consumed.global_event_sequence);
                if (!p_rec) {
                    SmpReplayDivergence d;
                    d.type = SmpReplayDivergenceType::DmaDataMismatch;
                    d.message = "Missing companion DMA payload during replay for event sequence " + std::to_string(consumed.global_event_sequence);
                    d.global_event_sequence = consumed.global_event_sequence;
                    d.actual_event_type = consumed.event_type;
                    trigger_divergence(d);
                    return false;
                }
                if (!p_rec->verify_crc()) {
                    SmpReplayDivergence d;
                    d.type = SmpReplayDivergenceType::DmaDataMismatch;
                    d.message = "Companion DMA payload CRC32C mismatch during replay";
                    d.global_event_sequence = consumed.global_event_sequence;
                    d.actual_event_type = consumed.event_type;
                    trigger_divergence(d);
                    return false;
                }
                shared_memory_->write_bytes(consumed.core_id, p_rec->dst_paddr, p_rec->data.data(), p_rec->data.size());
                if (shared_memory_->exclusive_monitor()) {
                    shared_memory_->exclusive_monitor()->invalidate_reservations(
                        shared_memory_->exclusive_monitor()->max_cores(),
                        p_rec->dst_paddr,
                        p_rec->data.size()
                    );
                }
                const std::uint32_t success_status = static_cast<std::uint32_t>(device::VirtualDmaDescriptorStatus::Success);
                shared_memory_->write_bytes(consumed.core_id, desc_addr + offsetof(device::VirtualDmaDescriptor, status), reinterpret_cast<const std::uint8_t*>(&success_status), 4);
            }
            break;
        }
        case sndl::SndlV2EventType::DmaDataChunk: {
            if (!config_.allow_dma_replay) {
                SmpReplayDivergence d;
                d.type = SmpReplayDivergenceType::UnsupportedExternalInput;
                d.message = "DMA data chunk encountered but allow_dma_replay is false in capability config";
                d.global_event_sequence = rec.global_event_sequence;
                d.actual_event_type = rec.event_type;
                trigger_divergence(d);
                return false;
            }
            sndl::SndlV2Record consumed{};
            (void)cursor_.consume_expected_type(rec.event_type, consumed);
            stats_.total_events_consumed++;

            const std::uint64_t chunk_dst = consumed.payload1;
            const std::uint16_t chunk_len = consumed.flags & 0xFFFFu;
            if (chunk_len > 0) {
                std::uint8_t chunk_buf[16] = {0};
                const std::size_t n1 = std::min<std::size_t>(chunk_len, 8);
                std::memcpy(chunk_buf, &consumed.payload2, n1);
                if (chunk_len > 8) {
                    const std::size_t n2 = std::min<std::size_t>(chunk_len - 8, 8);
                    std::memcpy(chunk_buf + 8, &consumed.payload3, n2);
                }
                shared_memory_->write_bytes(consumed.core_id, chunk_dst, chunk_buf, chunk_len);
                if (shared_memory_->exclusive_monitor()) {
                    shared_memory_->exclusive_monitor()->invalidate_reservations(
                        shared_memory_->exclusive_monitor()->max_cores(),
                        chunk_dst,
                        chunk_len
                    );
                }
            }
            break;
        }
        case sndl::SndlV2EventType::DmaTransferCompleted: {
            if (!config_.allow_dma_replay) {
                SmpReplayDivergence d;
                d.type = SmpReplayDivergenceType::UnsupportedExternalInput;
                d.message = "DMA transfer completion encountered but allow_dma_replay is false in capability config";
                d.global_event_sequence = rec.global_event_sequence;
                d.actual_event_type = rec.event_type;
                trigger_divergence(d);
                return false;
            }
            sndl::SndlV2Record consumed{};
            (void)cursor_.consume_expected_type(rec.event_type, consumed);
            stats_.total_events_consumed++;

            const std::uint64_t desc_addr = consumed.payload1;
            const std::uint32_t success_status = static_cast<std::uint32_t>(device::VirtualDmaDescriptorStatus::Success);
            shared_memory_->write_bytes(consumed.core_id, desc_addr + offsetof(device::VirtualDmaDescriptor, status), reinterpret_cast<const std::uint8_t*>(&success_status), 4);
            break;
        }
        case sndl::SndlV2EventType::DmaSubmissionRejected:
        case sndl::SndlV2EventType::DmaTransferFailed: {
            if (!config_.allow_dma_replay) {
                SmpReplayDivergence d;
                d.type = SmpReplayDivergenceType::UnsupportedExternalInput;
                d.message = "DMA failure event encountered but allow_dma_replay is false in capability config";
                d.global_event_sequence = rec.global_event_sequence;
                d.actual_event_type = rec.event_type;
                trigger_divergence(d);
                return false;
            }
            sndl::SndlV2Record consumed{};
            (void)cursor_.consume_expected_type(rec.event_type, consumed);
            stats_.total_events_consumed++;
            break;
        }
        case sndl::SndlV2EventType::DeviceInterruptAsserted: {
            sndl::SndlV2Record consumed{};
            (void)cursor_.consume_expected_type(rec.event_type, consumed);
            stats_.total_events_consumed++;

            scheduler_->interrupt_router().request_interrupt(
                consumed.core_id,
                static_cast<std::uint32_t>(consumed.payload1),
                0x80u,
                consumed.source_id
            );
            break;
        }
        case sndl::SndlV2EventType::DeviceRegisterRead:
        case sndl::SndlV2EventType::DeviceRegisterWrite:
        case sndl::SndlV2EventType::DeviceStateTransition: {
            sndl::SndlV2Record consumed{};
            (void)cursor_.consume_expected_type(rec.event_type, consumed);
            stats_.total_events_consumed++;
            break;
        }
        case sndl::SndlV2EventType::CoreSelected:
        case sndl::SndlV2EventType::QuantumStart:
            // Hand off to quantum scheduler execution
            return true;
        default:
            // Non-external event encountered
            return true;
        }
    }
    return true;
}

bool SmpReplayCoordinator::execute_core_quantum(std::size_t core_id, std::uint64_t quantum_budget) {
    auto& slot = scheduler_->get_slot(core_id);
    auto& core_state = core_replay_states_[core_id];

    core_state.quantums_allocated++;
    core_state.active_quantum_budget = quantum_budget;
    core_state.active_quantum_executed = 0;

    std::uint64_t executed = 0;
    QuantumTruncationReason reason = QuantumTruncationReason::QuantumExhausted;

    for (std::uint64_t q = 0; q < quantum_budget; ++q) {
        if (slot.cpu->is_halted()) {
            reason = QuantumTruncationReason::CoreHalted;
            break;
        }
        if (slot.cpu->is_waiting_for_interrupt()) {
            reason = QuantumTruncationReason::CoreWaitingForInterrupt;
            break;
        }
        if (slot.cpu->is_waiting_for_event()) {
            reason = QuantumTruncationReason::CoreWaitingForEvent;
            break;
        }

        slot.cpu->set_exception_taken_in_step(false);

        // Step instruction
        slot.cpu->step(shared_memory_->raw_memory());

        if (slot.cpu->exception_taken_in_step()) {
            reason = QuantumTruncationReason::ExceptionRaised;

            // Verify recorded ExceptionTaken event
            sndl::SndlV2Record exc_rec{};
            const auto exc_res = cursor_.consume_expected_type(sndl::SndlV2EventType::ExceptionTaken, exc_rec);
            if (exc_res.is_failure()) {
                SmpReplayDivergence d;
                d.type = SmpReplayDivergenceType::UnexpectedEvent;
                d.message = "Exception taken during replay step, but SNDL v2 lacks matching ExceptionTaken: " + exc_res.message;
                d.core_id = static_cast<std::uint32_t>(core_id);
                d.global_event_sequence = cursor_.current_sequence();
                trigger_divergence(d);
                return false;
            }
            stats_.total_events_consumed++;
            break; // Exceptions truncate quantum; instruction does not retire
        }

        executed++;
        scheduler_->set_global_retirement_sequence(scheduler_->global_retirement_sequence() + 1);
        slot.total_instructions_retired++;
        core_state.total_instructions_retired++;
        stats_.total_instructions_retired++;
        stats_.per_core_retired[core_id]++;

        if (slot.cpu->is_halted()) {
            reason = QuantumTruncationReason::CoreHalted;
            break;
        }
        if (slot.cpu->is_waiting_for_interrupt()) {
            reason = QuantumTruncationReason::CoreWaitingForInterrupt;
            break;
        }
        if (slot.cpu->is_waiting_for_event()) {
            reason = QuantumTruncationReason::CoreWaitingForEvent;
            break;
        }
    }

    core_state.active_quantum_executed = executed;
    slot.state.capture_from_cpu(*slot.cpu);
    core_state.lifecycle = slot.state.lifecycle;

    // Validate QuantumEnd record
    sndl::SndlV2Record qend_rec{};
    const auto qend_res = cursor_.consume_expected_type(sndl::SndlV2EventType::QuantumEnd, qend_rec);
    if (qend_res.is_failure()) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::QuantumMismatch;
        d.message = "Failed to consume QuantumEnd: " + qend_res.message;
        d.core_id = static_cast<std::uint32_t>(core_id);
        d.global_event_sequence = cursor_.current_sequence();
        trigger_divergence(d);
        return false;
    }
    stats_.total_events_consumed++;

    if (qend_rec.core_id != core_id) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::CoreMismatch;
        d.message = "QuantumEnd core (" + std::to_string(qend_rec.core_id) + ") != executing core (" + std::to_string(core_id) + ")";
        d.core_id = static_cast<std::uint32_t>(core_id);
        trigger_divergence(d);
        return false;
    }

    if (config_.enforce_retirement_parity && qend_rec.payload1 != executed) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::RetirementMismatch;
        d.message = "Quantum instruction retirement mismatch: recorded=" + std::to_string(qend_rec.payload1) +
                     ", replayed=" + std::to_string(executed);
        d.expected_val = qend_rec.payload1;
        d.actual_val = executed;
        d.core_id = static_cast<std::uint32_t>(core_id);
        trigger_divergence(d);
        return false;
    }

    if (config_.enforce_quantum_matching && qend_rec.payload2 != static_cast<std::uint64_t>(reason)) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::QuantumMismatch;
        d.message = "Quantum truncation reason mismatch: recorded=" + std::to_string(qend_rec.payload2) +
                     ", replayed=" + std::to_string(static_cast<std::uint64_t>(reason));
        d.expected_val = qend_rec.payload2;
        d.actual_val = static_cast<std::uint64_t>(reason);
        d.core_id = static_cast<std::uint32_t>(core_id);
        trigger_divergence(d);
        return false;
    }

    // Check early truncation record if non-exhausted
    if (reason != QuantumTruncationReason::QuantumExhausted) {
        core_state.early_truncations++;
        sndl::SndlV2Record et_rec{};
        const auto et_res = cursor_.consume_expected_type(sndl::SndlV2EventType::EarlyTruncation, et_rec);
        if (et_res.is_failure()) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::QuantumMismatch;
            d.message = "Early truncation expected in SNDL stream: " + et_res.message;
            d.core_id = static_cast<std::uint32_t>(core_id);
            trigger_divergence(d);
            return false;
        }
        stats_.total_events_consumed++;
    }

    return true;
}

bool SmpReplayCoordinator::step_quantum() {
    if (state_ != SmpReplayState::Ready && state_ != SmpReplayState::Running) {
        return false;
    }
    state_ = SmpReplayState::Running;

    if (!process_pending_external_events()) {
        return false;
    }

    if (!cursor_.has_next()) {
        return false;
    }

    sndl::SndlV2Record next_event{};
    if (cursor_.peek(next_event).is_failure()) {
        return false;
    }

    if (next_event.event_type == sndl::SndlV2EventType::CoreSelected) {
        sndl::SndlV2Record sel_rec{};
        auto res = cursor_.consume_expected_type(sndl::SndlV2EventType::CoreSelected, sel_rec);
        if (res.is_failure()) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::SchedulerMismatch;
            d.message = "Failed to consume CoreSelected event: " + res.message;
            trigger_divergence(d);
            return false;
        }
        stats_.total_events_consumed++;

        const std::size_t core_id = sel_rec.core_id;
        if (core_id >= active_core_count_) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::CoreMismatch;
            d.message = "CoreSelected core ID (" + std::to_string(core_id) + ") exceeds active limit (" +
                         std::to_string(active_core_count_) + ")";
            trigger_divergence(d);
            return false;
        }

        sndl::SndlV2Record qstart_rec{};
        res = cursor_.consume_expected_type(sndl::SndlV2EventType::QuantumStart, qstart_rec);
        if (res.is_failure()) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::QuantumMismatch;
            d.message = "Expected QuantumStart event after CoreSelected: " + res.message;
            trigger_divergence(d);
            return false;
        }
        stats_.total_events_consumed++;

        if (qstart_rec.core_id != core_id) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::CoreMismatch;
            d.message = "QuantumStart core (" + std::to_string(qstart_rec.core_id) +
                         ") != CoreSelected core (" + std::to_string(core_id) + ")";
            trigger_divergence(d);
            return false;
        }

        const std::uint64_t budget = qstart_rec.payload1;
        if (!execute_core_quantum(core_id, budget)) {
            return false;
        }

        stats_.total_quantums_executed++;
        return true;
    }

    // Unexpected event
    SmpReplayDivergence d;
    d.type = SmpReplayDivergenceType::UnexpectedEvent;
    d.message = "Unexpected event at quantum boundary: " + std::string(sndl::sndl_v2_event_type_name(next_event.event_type));
    d.actual_event_type = next_event.event_type;
    d.global_event_sequence = next_event.global_event_sequence;
    trigger_divergence(d);
    return false;
}

bool SmpReplayCoordinator::verify_completion_invariants() {
    // 1. All mandatory events must be consumed
    if (cursor_.has_next()) {
        sndl::SndlV2Record unconsumed{};
        (void)cursor_.peek(unconsumed);
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::MissingEvent;
        d.message = "Replay terminated with unconsumed events in SNDL stream (next seq: " +
                     std::to_string(unconsumed.global_event_sequence) + ")";
        d.global_event_sequence = unconsumed.global_event_sequence;
        d.actual_event_type = unconsumed.event_type;
        trigger_divergence(d);
        return false;
    }

    // 2. Global retirement sequence must match footer
    if (cursor_.has_footer() && config_.enforce_retirement_parity) {
        if (scheduler_->global_retirement_sequence() != cursor_.footer().last_global_retirement_sequence) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::RetirementMismatch;
            d.message = "Final global retirement sequence mismatch: recorded=" +
                         std::to_string(cursor_.footer().last_global_retirement_sequence) +
                         ", replayed=" + std::to_string(scheduler_->global_retirement_sequence());
            d.expected_val = cursor_.footer().last_global_retirement_sequence;
            d.actual_val = scheduler_->global_retirement_sequence();
            trigger_divergence(d);
            return false;
        }
    }

    return true;
}

ReplayResult SmpReplayCoordinator::run(std::uint64_t max_total_instructions) {
    if (state_ != SmpReplayState::Ready && state_ != SmpReplayState::Running) {
        return ReplayResult::fail(
            ReplayErrorCode::InvalidConfiguration,
            "SmpReplayCoordinator is not ready (state: " + std::string(smp_replay_state_name(state_)) + ")"
        );
    }

    state_ = SmpReplayState::Running;
    const auto start_time = std::chrono::steady_clock::now();

    while (state_ == SmpReplayState::Running && stats_.total_instructions_retired < max_total_instructions) {
        const bool progressed = step_quantum();
        if (!progressed || state_ != SmpReplayState::Running) {
            break;
        }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_time
    ).count();
    stats_.execution_microseconds = static_cast<std::uint64_t>(elapsed);

    if (state_ == SmpReplayState::Diverged) {
        stats_.final_state = SmpReplayState::Diverged;
        return ReplayResult::fail(ReplayErrorCode::Diverged, divergence_.message);
    }

    // Drain any remaining pending external events before verifying completion invariants
    if (!process_pending_external_events()) {
        state_ = SmpReplayState::Diverged;
        stats_.final_state = SmpReplayState::Diverged;
        return ReplayResult::fail(ReplayErrorCode::Diverged, divergence_.message);
    }

    // Completion verification
    if (!verify_completion_invariants()) {
        state_ = SmpReplayState::Diverged;
        stats_.final_state = SmpReplayState::Diverged;
        return ReplayResult::fail(ReplayErrorCode::Diverged, divergence_.message);
    }

    state_ = SmpReplayState::Completed;
    stats_.final_state = SmpReplayState::Completed;
    stats_.final_canonical_hash = scheduler_->compute_canonical_multicore_hash();

    return ReplayResult::ok();
}


bool SmpReplayCoordinator::validate_cross_file_integrity(const std::filesystem::path& sndl_path, std::string& out_error) {
    SndlV2ReplayReader scanner;
    const auto open_res = scanner.open(sndl_path);
    if (open_res.is_failure()) {
        out_error = "Failed to scan SNDL stream: " + open_res.message;
        return false;
    }

    sndl::SndlV2Record rec{};
    while (scanner.has_next()) {
        if (scanner.read_next(rec).is_failure()) break;
        if (rec.event_type == sndl::SndlV2EventType::DmaSubmissionAccepted) {
            const std::uint32_t op_type = static_cast<std::uint32_t>((rec.payload3 >> 32) & 0xFFull);
            if (op_type == static_cast<std::uint32_t>(device::VirtualDmaOpType::Copy)) {
                if (!dma_payload_store_.is_open()) {
                    out_error = "Companion DMA store required but not loaded for event sequence " +
                                std::to_string(rec.global_event_sequence);
                    return false;
                }
                const auto* p_rec = dma_payload_store_.find_payload(rec.global_event_sequence);
                if (!p_rec) {
                    out_error = "Missing companion DMA payload for sequence " +
                                std::to_string(rec.global_event_sequence);
                    return false;
                }
                if (!p_rec->verify_crc()) {
                    out_error = "Corrupted companion DMA payload CRC for sequence " +
                                std::to_string(rec.global_event_sequence);
                    return false;
                }
            }
        }
    }
    return true;
}

SmpReplayCheckpoint SmpReplayCoordinator::create_checkpoint() const {
    SmpReplayCheckpoint cp{};
    cp.magic = SmpReplayCheckpoint::kMagic;
    cp.version = SmpReplayCheckpoint::kVersion;
    cp.session_id = dma_payload_store_.session_id();
    cp.base_snapshot_hash = baseline_snapshot_hash_;
    cp.global_event_sequence = cursor_.current_sequence();
    cp.global_retirement_sequence = scheduler_ ? scheduler_->global_retirement_sequence() : 0;
    cp.checkpoint_cluster_hash = scheduler_ ? scheduler_->compute_canonical_multicore_hash() : 0;
    cp.active_core_count = active_core_count_;

    cp.per_core_retired.resize(active_core_count_, 0);
    cp.per_core_pc.resize(active_core_count_, 0);
    cp.per_core_states.resize(active_core_count_);

    for (std::size_t c = 0; c < active_core_count_; ++c) {
        if (scheduler_) {
            const auto& slot = scheduler_->get_slot(c);
            cp.per_core_retired[c] = slot.total_instructions_retired;
            cp.per_core_states[c] = slot.state;
            if (slot.cpu) {
                cp.per_core_states[c].capture_from_cpu(*slot.cpu);
                cp.per_core_pc[c] = slot.cpu->get_program_counter();
            }
        }
    }

    if (shared_memory_) {
        const auto& raw = shared_memory_->raw_memory();
        cp.memory_content.assign(raw.raw_data(), raw.raw_data() + raw.size());
    }

    cp.dma_payload_store_offset = dma_payload_store_.index_offset();

    if (dma_device_) {
        cp.dma_device_state = dma_device_->serialize_state();
    }

    cp.crc32c = cp.compute_crc32c();
    return cp;
}

bool SmpReplayCoordinator::restore_checkpoint(const SmpReplayCheckpoint& checkpoint) {
    if (!checkpoint.verify_crc()) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::SnapshotMismatch;
        d.message = "Checkpoint CRC32C verification failed";
        trigger_divergence(d);
        return false;
    }

    if (checkpoint.base_snapshot_hash != baseline_snapshot_hash_) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::SnapshotMismatch;
        d.message = "Checkpoint base snapshot hash mismatch";
        trigger_divergence(d);
        return false;
    }

    if (checkpoint.active_core_count != active_core_count_) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::CoreMismatch;
        d.message = "Checkpoint core count mismatch";
        trigger_divergence(d);
        return false;
    }

    // 1. Restore shared memory
    if (shared_memory_ && !checkpoint.memory_content.empty()) {
        if (checkpoint.memory_content.size() <= shared_memory_->size()) {
            shared_memory_->write_bytes(0, 0, checkpoint.memory_content.data(), checkpoint.memory_content.size());
        }
    }

    // 2. Restore core states
    for (std::size_t c = 0; c < active_core_count_; ++c) {
        if (scheduler_ && c < checkpoint.per_core_states.size()) {
            auto& slot = scheduler_->get_slot(c);
            slot.state = checkpoint.per_core_states[c];
            slot.total_instructions_retired = checkpoint.per_core_retired[c];
            if (slot.cpu) {
                checkpoint.per_core_states[c].apply_to_cpu(*slot.cpu);
            }
        }
        if (c < core_replay_states_.size()) {
            core_replay_states_[c].total_instructions_retired = checkpoint.per_core_retired[c];
        }
    }

    // 3. Restore global retirement
    if (scheduler_) {
        scheduler_->set_global_retirement_sequence(checkpoint.global_retirement_sequence);
    }

    // 4. Restore DMA device state
    if (dma_device_ && !checkpoint.dma_device_state.empty()) {
        (void)dma_device_->deserialize_state(checkpoint.dma_device_state.data(), checkpoint.dma_device_state.size());
    }

    // 5. Verify restored cluster hash matches checkpoint hash
    if (scheduler_) {
        const std::uint64_t restored_hash = scheduler_->compute_canonical_multicore_hash();
        if (checkpoint.checkpoint_cluster_hash != 0 && restored_hash != checkpoint.checkpoint_cluster_hash) {
            SmpReplayDivergence d;
            d.type = SmpReplayDivergenceType::FinalStateMismatch;
            d.message = "Restored checkpoint canonical cluster hash mismatch";
            trigger_divergence(d);
            return false;
        }
    }

    // 6. Reposition SNDL cursor to checkpoint sequence
    const auto seek_res = cursor_.seek_to_sequence(sndl_path_, checkpoint.global_event_sequence);
    if (seek_res.is_failure()) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::SequenceMismatch;
        d.message = "Failed to seek SNDL cursor to checkpoint sequence: " + seek_res.message;
        trigger_divergence(d);
        return false;
    }

    state_ = SmpReplayState::Ready;
    return true;
}

} // namespace winios::replay

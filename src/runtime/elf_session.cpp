#include "runtime/elf_session.h"
#include "runtime/artifact_publication.h"

#include "snapshot/snapshot_manager.h"
#include "sndl/sndl_source_hooks.h"
#include "sndl/sndl_validation.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace {
using namespace winios;
constexpr std::size_t kMetadataAllowance = 1024u * 1024u;
ExecutionResult failure(ExecutionStatus status, const std::string& message) {
    ExecutionResult result;
    result.status = status;
    result.error = message;
    return result;
}
bool complete_snapshot(const winios::snapshot::SnapshotInspection& info) {
    using namespace winios::snapshot;
    if (info.header.version != kWSnapVersion1 || info.header.core_count != 1) return false;
    for (const auto type : std::array{SnapshotSectionType::CpuState,
            SnapshotSectionType::MemoryState, SnapshotSectionType::TimerState,
            SnapshotSectionType::GicState, SnapshotSectionType::PmuState,
            SnapshotSectionType::DebugState}) {
        const auto count = std::count_if(info.sections.begin(), info.sections.end(),
            [type](const auto& section) { return section.section_type == type; });
        if (count != 1) return false;
        const auto section = std::find_if(info.sections.begin(), info.sections.end(),
            [type](const auto& value) { return value.section_type == type; });
        if (section->section_id != 0) return false;
    }
    return true;
}
void require_bounded_file(const std::filesystem::path& path, std::size_t limit) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > limit) {
        throw std::invalid_argument("Missing, non-regular, or oversized session input");
    }
}
}

struct ElfSession::Machine {
    CPU cpu;
    Memory memory;
    winios::VirtualTime time;

    explicit Machine(std::size_t size) : memory(size) { cpu.bind_virtual_time(&time, 0); }
    winios::snapshot::SnapshotRestoreContext context() {
        return {&cpu, &memory, &time, &cpu.get_gic(), {}};
    }
};

ElfSession::ElfSession(const RuntimeConfig& config) : config_(config) {
    if (config.memory_size == 0 || config.memory_size > kMaxMemory
        || config.max_instructions == 0 || config.tracing) {
        throw std::invalid_argument("Session requires bounded nonzero RAM/budget and tracing disabled");
    }
    machine_ = std::make_unique<Machine>(config.memory_size);
}
ElfSession::~ElfSession() = default;

ExecutionResult ElfSession::load(const std::filesystem::path& path) {
    try {
        auto candidate = std::make_unique<Machine>(config_.memory_size);
        auto result = GuestRuntime::prepare_elf(path, config_, candidate->cpu, candidate->memory);
        if (result.status != ExecutionStatus::completed) return result;
        candidate->cpu.bind_virtual_time(&candidate->time, 0);
        machine_.swap(candidate);
        loaded_bytes_ = result.loaded_bytes;
        ready_ = true;
        return result;
    } catch (const std::exception& error) {
        return failure(ExecutionStatus::load_error, error.what());
    }
}

ExecutionResult ElfSession::run_impl(std::size_t budget) {
    if (!ready_ || budget == 0 || budget > config_.max_instructions
        || budget > std::numeric_limits<std::uint64_t>::max() - retired()) {
        return failure(ExecutionStatus::invalid_configuration, "Session not loaded or invalid instruction budget");
    }
    ExecutionResult result;
    result.loaded_bytes = loaded_bytes_;
    result.status = ExecutionStatus::instruction_limit_reached;
    auto& cpu = machine_->cpu;
    while (result.instructions_executed < budget && !cpu.is_halted()) {
        if (cpu.is_waiting()) break; // No live asynchronous inputs in this isolated runtime.
        try {
            cpu.step(machine_->memory);
            ++result.instructions_executed;
        } catch (const std::out_of_range& error) {
            result.status = ExecutionStatus::memory_fault;
            result.error = error.what();
            break;
        } catch (const std::exception& error) {
            result.status = ExecutionStatus::unsupported_instruction;
            result.error = error.what();
            break;
        }
    }
    if (cpu.is_halted()) result.status = ExecutionStatus::completed;
    result.final_pc = cpu.get_program_counter();
    result.final_stack_pointer = cpu.get_stack_pointer();
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) result.final_registers[i] = cpu.read_register(i);
    if (machine_->memory.size() >= 0x108) result.memory_at_0x100 = machine_->memory.read_u64(0x100);
    return result;
}
ExecutionResult ElfSession::run(std::size_t budget) {
    winios::sndl::ScopedThreadRecorder mute(nullptr);
    return run_impl(budget);
}
std::uint64_t ElfSession::retired() const noexcept {
    return machine_->cpu.get_instruction_retirement_count();
}
winios::replay::ReplayComponentHashes ElfSession::hashes() const {
    return winios::replay::ReplayStateHash::compute_state_hashes(
        machine_->cpu, machine_->memory, &machine_->time, &machine_->cpu.get_gic());
}

winios::snapshot::SnapshotResult ElfSession::capture(const std::filesystem::path& path) const {
    using namespace winios::snapshot;
    try {
        if (!ready_ || std::filesystem::exists(path)) {
            return SnapshotResult::fail(SnapshotErrorCode::InvalidArgument, "Session not ready or snapshot output exists");
        }
        std::vector<std::uint8_t> bytes;
        const auto captured = SnapshotManager::capture_to_buffer(machine_->context(), bytes);
        if (!captured.success) return captured;
        ArtifactPublication publication(path);
        publication.write(bytes);
        if (!SnapshotManager::validate_file(publication.temporary_path()).valid) {
            return SnapshotResult::fail(SnapshotErrorCode::InvalidStateData, "Staged snapshot invalid");
        }
        publication.commit();
        return SnapshotResult::ok();
    } catch (const std::exception& error) {
        return SnapshotResult::fail(SnapshotErrorCode::WriteError, error.what());
    }
}
winios::snapshot::SnapshotResult ElfSession::restore(const std::filesystem::path& path) {
    using namespace winios::snapshot;
    try {
        require_bounded_file(path, config_.memory_size + kMetadataAllowance);
        SnapshotInspection info;
        auto result = SnapshotManager::inspect_file(path, info);
        if (!result.success) return result;
        if (!complete_snapshot(info) || info.header.memory_capacity != config_.memory_size) {
            return SnapshotResult::fail(SnapshotErrorCode::MemoryCapacityMismatch, "Session requires matching single-core RAM");
        }
        auto candidate = std::make_unique<Machine>(config_.memory_size);
        auto context = candidate->context();
        result = SnapshotManager::restore_from_file(path, context);
        if (!result.success) return result;
        machine_.swap(candidate);
        ready_ = true;
        loaded_bytes_ = 0; // The generic snapshot has no ELF provenance metadata.
        return SnapshotResult::ok();
    } catch (const std::exception& error) {
        return SnapshotResult::fail(SnapshotErrorCode::ReadError, error.what());
    }
}

ElfRecordingResult ElfSession::record(const std::filesystem::path& snapshot,
                                     const std::filesystem::path& log, std::size_t budget) {
    ElfRecordingResult result;
    try {
        if (!ready_ || budget == 0 || budget > config_.max_instructions

            || budget > std::numeric_limits<std::uint64_t>::max() - retired()
            || machine_->cpu.is_halted() || machine_->cpu.is_waiting()
            || std::filesystem::exists(snapshot) || std::filesystem::exists(log)
            || std::filesystem::absolute(snapshot).lexically_normal()
                == std::filesystem::absolute(log).lexically_normal()) {
            result.error = "Recording requires runnable state, valid budget, distinct unused outputs";
            return result;
        }
        ArtifactPublication publication(log);
        const auto captured = capture(snapshot);
        if (!captured.success) { result.error = captured.error_message; return result; }
        winios::snapshot::SnapshotInspection info;
        const auto inspected = winios::snapshot::SnapshotManager::inspect_file(snapshot, info);
        if (!inspected.success) { result.error = inspected.error_message; return result; }
        winios::sndl::SndlRecorder recorder;
        const auto started = recorder.start_recording(publication.temporary_path(), 1, info.header.header_crc32c);
        if (started.is_failure()) { result.error = started.message; return result; }
        {
            winios::sndl::ScopedThreadRecorder binding(&recorder);
            result.execution = run_impl(budget);
        }
        result.records = recorder.total_recorded();
        result.final_hashes = hashes();
        if (result.execution.status != ExecutionStatus::completed
            && result.execution.status != ExecutionStatus::instruction_limit_reached) {
            recorder.abort_recording();
            result.error = result.execution.error;
            return result;
        }
        const auto stopped = recorder.stop_recording(result.execution.instructions_executed);
        if (stopped.is_failure()) { result.error = stopped.message; return result; }
        if (!winios::sndl::SndlValidation::validate_file(publication.temporary_path(), snapshot).is_valid) {
            result.error = "Staged SNDL failed validation";
            return result;
        }
        publication.commit();
        result.success = true;
    } catch (const std::exception& error) { result.error = error.what(); }
    return result;
}

winios::replay::ReplayResult ElfSession::verify(
    const std::filesystem::path& snapshot, const std::filesystem::path& log,
    const winios::replay::ReplayComponentHashes& expected, std::size_t budget,
    winios::replay::ReplayExecutionStats& stats) {
    using namespace winios::replay;
    stats = {};
    if (budget == 0) return ReplayResult::fail(ReplayErrorCode::InvalidConfiguration);
    try {
        require_bounded_file(snapshot, kMaxMemory + kMetadataAllowance);
        require_bounded_file(log, 64u * 1024u * 1024u);
        winios::snapshot::SnapshotInspection info;
        if (!winios::snapshot::SnapshotManager::inspect_file(snapshot, info).success
            || !complete_snapshot(info) || info.header.memory_capacity == 0
            || info.header.memory_capacity > kMaxMemory) {
            return ReplayResult::fail(ReplayErrorCode::SnapshotValidationFailed);
        }
        const auto validated = ReplayEngine::validate(snapshot, log);
        if (validated.is_failure()) return validated;
        winios::sndl::SndlInspectionInfo log_info;
        if (winios::sndl::SndlValidation::inspect_file(log, log_info).is_failure()
            || log_info.header.core_count != 1 || log_info.header.base_snapshot_hash == 0
            || log_info.header.base_snapshot_hash != info.header.header_crc32c) {
            return ReplayResult::fail(ReplayErrorCode::SnapshotMismatch, "ELF replay requires exact nonzero snapshot association");
        }
        RuntimeConfig limits;
        limits.memory_size = static_cast<std::size_t>(info.header.memory_capacity);
        limits.max_instructions = budget;
        ElfSession baseline(limits);
        if (!baseline.restore(snapshot).success
            || budget > std::numeric_limits<std::uint64_t>::max() - baseline.retired()) {
            return ReplayResult::fail(ReplayErrorCode::InvalidConfiguration, "Invalid restore state or retirement budget overflow");
        }
        ReplayConfig config;
        config.max_instructions = budget;
        config.max_replay_duration_ms = 30000;
        config.require_exact_event_consumption = true;
        config.divergence_report_path.clear();
        winios::sndl::ScopedThreadRecorder mute(nullptr);
        const auto replayed = ReplayEngine::run(snapshot, log, config, stats);
        if (replayed.is_failure()) return replayed;
        if (stats.final_state_hashes != expected) {
            return ReplayResult::fail(ReplayErrorCode::StateVerificationFailed, "ELF recording/replay state mismatch");
        }
        return ReplayResult::ok();
    } catch (const std::exception& error) {
        return ReplayResult::fail(ReplayErrorCode::FileIOError, error.what());
    }
}

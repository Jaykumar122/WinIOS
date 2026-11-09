#include "runtime/execution_checkpoint.h"
#include "runtime/artifact_publication.h"
#include "snapshot/snapshot_manager.h"
#include "trace/trace_serializer.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {

static std::atomic<std::uint64_t> s_checkpoint_seq{1};

std::string make_unique_temp_name(std::string_view prefix, std::string_view ext) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto seq = s_checkpoint_seq.fetch_add(1);
    std::ostringstream ss;
    ss << prefix << now << "-" << std::this_thread::get_id() << "-" << seq << ext;
    return ss.str();
}

std::uint64_t current_time_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::uint32_t compute_header_crc(const CheckpointMetadata& m) {
    CheckpointMetadata copy = m;
    copy.header_crc32c = 0;
    return winios::trace::calculate_crc32c(
        reinterpret_cast<const std::uint8_t*>(&copy), sizeof(copy));
}

} // namespace

ExecutionCheckpoint CheckpointManager::capture(const ElfSession& session, std::uint64_t budget) {
    ExecutionCheckpoint cp;
    auto& m = cp.header;

    m.magic = CheckpointMetadata::kMagic;
    m.version = CheckpointMetadata::kVersion;
    m.timestamp_ms = current_time_ms();
    m.instruction_budget = budget;
    m.retired_instructions = session.retired();
    m.memory_capacity = session.memory_size();
    m.component_hashes = session.hashes();

    // Capture snapshot payload using a unique temporary file
    const auto temp_dir = std::filesystem::temp_directory_path();
    const auto temp_snap = temp_dir / make_unique_temp_name(".winios-chk-cap-", ".wsnap");

    const auto snap_res = session.capture(temp_snap);
    if (!snap_res.success) {
        throw std::runtime_error("Failed to capture snapshot for checkpoint: " + snap_res.error_message);
    }

    try {
        const auto size = std::filesystem::file_size(temp_snap);
        cp.snapshot_payload.resize(static_cast<std::size_t>(size));
        std::ifstream input(temp_snap, std::ios::binary);
        if (!input.read(reinterpret_cast<char*>(cp.snapshot_payload.data()), static_cast<std::streamsize>(size))) {
            throw std::runtime_error("Failed to read captured snapshot into memory payload");
        }
        std::error_code ec;
        std::filesystem::remove(temp_snap, ec);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp_snap, ec);
        throw;
    }

    cp.payload_crc32c = winios::trace::calculate_crc32c(
        cp.snapshot_payload.data(), cp.snapshot_payload.size());
    m.snapshot_crc32c = cp.payload_crc32c;
    m.snapshot_size = cp.snapshot_payload.size();

    // Verify snapshot inspection
    winios::snapshot::SnapshotInspection info;
    if (winios::snapshot::SnapshotManager::inspect_file(temp_snap, info).success) {
        m.pc = 0; // The snapshot internal structure encapsulates PC
    }

    m.header_crc32c = compute_header_crc(m);
    return cp;
}

std::vector<std::uint8_t> CheckpointManager::serialize(const ExecutionCheckpoint& cp) {
    if (!validate(cp)) {
        throw std::invalid_argument("Cannot serialize invalid checkpoint");
    }

    std::vector<std::uint8_t> buffer;
    const std::size_t total_size = sizeof(CheckpointMetadata) + sizeof(std::uint32_t) * 3 + cp.snapshot_payload.size();
    buffer.reserve(total_size);

    // 1. Header
    const auto* h_ptr = reinterpret_cast<const std::uint8_t*>(&cp.header);
    buffer.insert(buffer.end(), h_ptr, h_ptr + sizeof(CheckpointMetadata));

    // 2. Payload size & CRC
    const std::uint32_t payload_size = static_cast<std::uint32_t>(cp.snapshot_payload.size());
    const auto* s_ptr = reinterpret_cast<const std::uint8_t*>(&payload_size);
    buffer.insert(buffer.end(), s_ptr, s_ptr + sizeof(std::uint32_t));

    const auto* c_ptr = reinterpret_cast<const std::uint8_t*>(&cp.payload_crc32c);
    buffer.insert(buffer.end(), c_ptr, c_ptr + sizeof(std::uint32_t));

    // 3. Payload
    buffer.insert(buffer.end(), cp.snapshot_payload.begin(), cp.snapshot_payload.end());

    // 4. Trailing overall CRC
    const auto overall_crc = winios::trace::calculate_crc32c(buffer.data(), buffer.size());
    const auto* o_ptr = reinterpret_cast<const std::uint8_t*>(&overall_crc);
    buffer.insert(buffer.end(), o_ptr, o_ptr + sizeof(std::uint32_t));

    return buffer;
}

ExecutionCheckpoint CheckpointManager::deserialize(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < sizeof(CheckpointMetadata) + sizeof(std::uint32_t) * 3) {
        throw std::invalid_argument("Checkpoint buffer too small");
    }
    if (bytes.size() > kMaxCheckpointBytes) {
        throw std::invalid_argument("Checkpoint buffer exceeds maximum size limit");
    }

    // Verify overall trailing CRC32C
    const std::size_t data_len = bytes.size() - sizeof(std::uint32_t);
    std::uint32_t expected_crc = 0;
    std::memcpy(&expected_crc, bytes.data() + data_len, sizeof(std::uint32_t));
    const auto actual_crc = winios::trace::calculate_crc32c(bytes.data(), data_len);
    if (actual_crc != expected_crc) {
        throw std::invalid_argument("Checkpoint overall CRC32C mismatch (corrupted data)");
    }

    ExecutionCheckpoint cp;
    std::size_t offset = 0;

    // 1. Header
    std::memcpy(&cp.header, bytes.data() + offset, sizeof(CheckpointMetadata));
    offset += sizeof(CheckpointMetadata);

    if (cp.header.magic != CheckpointMetadata::kMagic) {
        throw std::invalid_argument("Invalid checkpoint magic number");
    }
    if (cp.header.version != CheckpointMetadata::kVersion) {
        throw std::invalid_argument("Unsupported checkpoint version");
    }
    if (cp.header.header_crc32c != compute_header_crc(cp.header)) {
        throw std::invalid_argument("Checkpoint header CRC32C corrupted");
    }

    // 2. Payload size & CRC
    std::uint32_t payload_size = 0;
    std::memcpy(&payload_size, bytes.data() + offset, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);

    std::memcpy(&cp.payload_crc32c, bytes.data() + offset, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);

    if (offset + payload_size != data_len) {
        throw std::invalid_argument("Checkpoint payload size mismatch");
    }

    // 3. Payload
    cp.snapshot_payload.assign(bytes.data() + offset, bytes.data() + offset + payload_size);

    if (winios::trace::calculate_crc32c(cp.snapshot_payload.data(), cp.snapshot_payload.size()) != cp.payload_crc32c) {
        throw std::invalid_argument("Checkpoint snapshot payload CRC32C mismatch");
    }

    return cp;
}

bool CheckpointManager::validate(const ExecutionCheckpoint& cp) {
    if (cp.header.magic != CheckpointMetadata::kMagic) return false;
    if (cp.header.version != CheckpointMetadata::kVersion) return false;
    if (cp.header.memory_capacity == 0 || cp.header.memory_capacity > ElfSession::kMaxMemory) return false;
    if (cp.header.header_crc32c != compute_header_crc(cp.header)) return false;
    if (cp.snapshot_payload.empty()) return false;
    if (cp.snapshot_payload.size() != cp.header.snapshot_size) return false;
    if (winios::trace::calculate_crc32c(cp.snapshot_payload.data(), cp.snapshot_payload.size()) != cp.payload_crc32c) {
        return false;
    }
    return true;
}

bool CheckpointManager::save(const std::filesystem::path& path, const ExecutionCheckpoint& cp) {
    const auto bytes = serialize(cp);
    ArtifactPublication pub(path);
    pub.write(bytes);
    pub.commit();
    return true;
}

ExecutionCheckpoint CheckpointManager::load(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::invalid_argument("Checkpoint file missing or not regular");
    }
    const auto size = std::filesystem::file_size(path);
    if (size == 0 || size > kMaxCheckpointBytes) {
        throw std::invalid_argument("Checkpoint file size out of bounds");
    }
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size))) {
        throw std::runtime_error("Failed to read checkpoint file");
    }
    return deserialize(buffer);
}

bool CheckpointManager::restore(ElfSession& session, const ExecutionCheckpoint& cp) {
    if (!validate(cp)) {
        throw std::invalid_argument("Cannot restore from invalid checkpoint");
    }
    if (session.memory_size() != cp.header.memory_capacity) {
        throw std::invalid_argument("Checkpoint memory capacity mismatch with target session");
    }

    // Write payload to a temporary snapshot file to feed ElfSession::restore
    const auto temp_dir = std::filesystem::temp_directory_path();
    const auto temp_snap = temp_dir / make_unique_temp_name(".winios-chk-res-", ".wsnap");

    {
        std::ofstream out(temp_snap, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(cp.snapshot_payload.data()), cp.snapshot_payload.size());
        out.flush();
    }

    bool success = false;
    try {
        const auto res = session.restore(temp_snap);
        std::error_code ec;
        std::filesystem::remove(temp_snap, ec);

        if (!res.success) return false;

        // Verify that restored session matches the component hashes of checkpoint
        const auto restored_hashes = session.hashes();
        if (restored_hashes != cp.header.component_hashes) {
            return false;
        }
        if (session.retired() != cp.header.retired_instructions) {
            return false;
        }
        success = true;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp_snap, ec);
        throw;
    }

    return success;
}

DivergenceReport CheckpointManager::compare_state(const ElfSession& a, const ElfSession& b) {
    DivergenceReport rep;
    if (a.retired() != b.retired()) {
        rep.has_divergence = true;
        rep.component = "RETIREMENT";
        rep.expected_value = a.retired();
        rep.actual_value = b.retired();
        rep.details = "Retirement instruction count mismatch";
        return rep;
    }

    const auto ha = a.hashes();
    const auto hb = b.hashes();

    if (ha.cpu_registers_hash != hb.cpu_registers_hash) {
        rep.has_divergence = true;
        rep.component = "CPU_REGISTERS";
        rep.expected_value = ha.cpu_registers_hash;
        rep.actual_value = hb.cpu_registers_hash;
        rep.details = "CPU architectural register state hash mismatch";
        return rep;
    }
    if (ha.memory_hash != hb.memory_hash) {
        rep.has_divergence = true;
        rep.component = "MEMORY";
        rep.expected_value = ha.memory_hash;
        rep.actual_value = hb.memory_hash;
        rep.details = "Memory contents state hash mismatch";
        return rep;
    }
    if (ha.timer_hash != hb.timer_hash) {
        rep.has_divergence = true;
        rep.component = "TIMER";
        rep.expected_value = ha.timer_hash;
        rep.actual_value = hb.timer_hash;
        rep.details = "Timer state hash mismatch";
        return rep;
    }
    if (ha.gic_hash != hb.gic_hash) {
        rep.has_divergence = true;
        rep.component = "GIC";
        rep.expected_value = ha.gic_hash;
        rep.actual_value = hb.gic_hash;
        rep.details = "GIC interrupt state hash mismatch";
        return rep;
    }
    if (ha.combined_hash != hb.combined_hash) {
        rep.has_divergence = true;
        rep.component = "COMBINED_STATE";
        rep.expected_value = ha.combined_hash;
        rep.actual_value = hb.combined_hash;
        rep.details = "Combined system state hash mismatch";
        return rep;
    }

    rep.has_divergence = false;
    return rep;
}

DivergenceReport CheckpointManager::compare_state(const ElfSession& session, const ExecutionCheckpoint& cp) {
    DivergenceReport rep;
    if (session.retired() != cp.header.retired_instructions) {
        rep.has_divergence = true;
        rep.component = "RETIREMENT";
        rep.expected_value = cp.header.retired_instructions;
        rep.actual_value = session.retired();
        rep.details = "Session retirement differs from checkpoint";
        return rep;
    }

    const auto sh = session.hashes();
    const auto& ch = cp.header.component_hashes;

    if (sh.cpu_registers_hash != ch.cpu_registers_hash) {
        rep.has_divergence = true;
        rep.component = "CPU_REGISTERS";
        rep.expected_value = ch.cpu_registers_hash;
        rep.actual_value = sh.cpu_registers_hash;
        rep.details = "CPU register hash diverges from checkpoint";
        return rep;
    }
    if (sh.memory_hash != ch.memory_hash) {
        rep.has_divergence = true;
        rep.component = "MEMORY";
        rep.expected_value = ch.memory_hash;
        rep.actual_value = sh.memory_hash;
        rep.details = "Memory contents hash diverges from checkpoint";
        return rep;
    }
    if (sh.combined_hash != ch.combined_hash) {
        rep.has_divergence = true;
        rep.component = "COMBINED_STATE";
        rep.expected_value = ch.combined_hash;
        rep.actual_value = sh.combined_hash;
        rep.details = "Combined state hash diverges from checkpoint";
        return rep;
    }

    rep.has_divergence = false;
    return rep;
}

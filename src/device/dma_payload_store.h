#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace winios::device {

inline constexpr std::uint64_t kDmaDataFileMagic  = 0x56444D4144415441ull; // 'VDMADATA'
inline constexpr std::uint64_t kDmaDataEntryMagic = 0x444D41454E545259ull; // 'DMAENTRY'
inline constexpr std::uint64_t kDmaDataIndexMagic = 0x444D41494E444558ull; // 'DMAINDEX'
inline constexpr std::uint16_t kDmaDataVersion1   = 1;
inline constexpr std::uint16_t kDmaDataVersion2   = 2;
inline constexpr std::size_t   kDmaDataHeaderSize = 64;
inline constexpr std::size_t   kDmaDataEntryHeaderSize = 48;
inline constexpr std::size_t   kDmaDataIndexEntrySize  = 48;

// -----------------------------------------------------------------------------
// RecordingLifecycleState: Explicit State Machine for Crash Consistency
// -----------------------------------------------------------------------------
enum class RecordingLifecycleState : std::uint8_t {
    Created            = 0,
    Recording          = 1,
    Finalizing         = 2,
    Complete           = 3,
    Aborted            = 4,
    RecoveryRequired   = 5,
    RecoveredIncomplete= 6
};

[[nodiscard]] constexpr std::string_view recording_lifecycle_state_name(RecordingLifecycleState state) noexcept {
    switch (state) {
    case RecordingLifecycleState::Created:            return "Created";
    case RecordingLifecycleState::Recording:          return "Recording";
    case RecordingLifecycleState::Finalizing:         return "Finalizing";
    case RecordingLifecycleState::Complete:           return "Complete";
    case RecordingLifecycleState::Aborted:            return "Aborted";
    case RecordingLifecycleState::RecoveryRequired:   return "RecoveryRequired";
    case RecordingLifecycleState::RecoveredIncomplete:return "RecoveredIncomplete";
    default:                                          return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// DmaPayloadRecord: Single Captured Transfer Data Payload
// -----------------------------------------------------------------------------
struct DmaPayloadRecord {
    std::uint64_t global_sequence{0};
    std::uint64_t descriptor_id{0};
    std::uint64_t dst_paddr{0};
    std::vector<std::uint8_t> data;
    std::uint32_t payload_crc32c{0};

    [[nodiscard]] bool verify_crc() const noexcept;
};

// -----------------------------------------------------------------------------
// DmaPayloadIndexEntry: Fixed 48-Byte Payload Index Table Entry
// -----------------------------------------------------------------------------
struct alignas(8) DmaPayloadIndexEntry {
    std::uint64_t global_sequence{0};
    std::uint64_t descriptor_id{0};
    std::uint64_t dst_paddr{0};
    std::uint64_t file_offset{0};
    std::uint64_t length{0};
    std::uint32_t payload_crc32c{0};
    std::uint32_t reserved{0};
};
static_assert(sizeof(DmaPayloadIndexEntry) == 48, "DmaPayloadIndexEntry must be 48 bytes");

// -----------------------------------------------------------------------------
// DmaPayloadStoreResult: Status outcome for companion store operations
// -----------------------------------------------------------------------------
enum class DmaPayloadStoreError : std::uint8_t {
    Success = 0,
    FileNotFound,
    FileOpenFailed,
    FileWriteFailed,
    FileReadFailed,
    InvalidMagic,
    UnsupportedVersion,
    HeaderCrcMismatch,
    EntryCrcMismatch,
    PayloadCrcMismatch,
    TruncatedPayload,
    SequenceNotFound,
    AlreadyFinalized,
    SessionMismatch,
    SnapshotMismatch,
    IncompleteStore,
    CorruptIndex,
    InvalidLength
};

[[nodiscard]] constexpr std::string_view dma_payload_error_name(DmaPayloadStoreError err) noexcept {
    switch (err) {
    case DmaPayloadStoreError::InvalidLength:      return "Companion DMA payload length exceeds maximum limit";
    case DmaPayloadStoreError::Success:            return "Success";
    case DmaPayloadStoreError::FileNotFound:       return "Companion DMA file not found";
    case DmaPayloadStoreError::FileOpenFailed:     return "Failed to open companion DMA file";
    case DmaPayloadStoreError::FileWriteFailed:    return "Failed to write companion DMA data";
    case DmaPayloadStoreError::FileReadFailed:     return "Failed to read companion DMA data";
    case DmaPayloadStoreError::InvalidMagic:       return "Invalid companion DMA file magic";
    case DmaPayloadStoreError::UnsupportedVersion: return "Unsupported companion DMA version";
    case DmaPayloadStoreError::HeaderCrcMismatch:  return "Companion DMA header CRC32C mismatch";
    case DmaPayloadStoreError::EntryCrcMismatch:   return "Companion DMA entry CRC32C mismatch";
    case DmaPayloadStoreError::PayloadCrcMismatch: return "Companion DMA payload CRC32C mismatch";
    case DmaPayloadStoreError::TruncatedPayload:   return "Truncated companion DMA payload";
    case DmaPayloadStoreError::SequenceNotFound:   return "No payload found for requested sequence";
    case DmaPayloadStoreError::AlreadyFinalized:   return "Companion DMA store already finalized";
    case DmaPayloadStoreError::SessionMismatch:    return "Companion DMA session identifier mismatch";
    case DmaPayloadStoreError::SnapshotMismatch:   return "Companion DMA base snapshot hash mismatch";
    case DmaPayloadStoreError::IncompleteStore:    return "Companion DMA store was interrupted or not finalized";
    case DmaPayloadStoreError::CorruptIndex:       return "Companion DMA payload index table corrupted";
    default:                                       return "Unknown error";
    }
}

struct DmaPayloadStoreResult {
    DmaPayloadStoreError code{DmaPayloadStoreError::Success};
    std::string message;

    [[nodiscard]] bool is_success() const noexcept { return code == DmaPayloadStoreError::Success; }
    [[nodiscard]] bool is_failure() const noexcept { return code != DmaPayloadStoreError::Success; }

    static DmaPayloadStoreResult ok() noexcept { return {DmaPayloadStoreError::Success, {}}; }
    static DmaPayloadStoreResult fail(DmaPayloadStoreError c, std::string msg = {}) {
        return {c, msg.empty() ? std::string(dma_payload_error_name(c)) : std::move(msg)};
    }
};

// -----------------------------------------------------------------------------
// DmaPayloadStore: Companion Storage for Deterministic DMA Replay Data
// -----------------------------------------------------------------------------
class DmaPayloadStore {
public:
    DmaPayloadStore() = default;
    ~DmaPayloadStore();

    DmaPayloadStore(const DmaPayloadStore&) = delete;
    DmaPayloadStore& operator=(const DmaPayloadStore&) = delete;
    DmaPayloadStore(DmaPayloadStore&&) noexcept;
    DmaPayloadStore& operator=(DmaPayloadStore&&) noexcept;

    // Recording operations
    [[nodiscard]] DmaPayloadStoreResult open_write(
        const std::filesystem::path& file_path,
        std::uint64_t session_id = 0,
        std::uint64_t base_snapshot_hash = 0
    );
    [[nodiscard]] DmaPayloadStoreResult record_payload(
        std::uint64_t global_sequence,
        std::uint64_t descriptor_id,
        std::uint64_t dst_paddr,
        const std::uint8_t* data,
        std::size_t size
    );
    [[nodiscard]] DmaPayloadStoreResult finalize(
        RecordingLifecycleState final_state = RecordingLifecycleState::Complete
    );

    // Replay & Validation operations
    [[nodiscard]] DmaPayloadStoreResult open_read(
        const std::filesystem::path& file_path,
        std::uint64_t expected_session_id = 0,
        std::uint64_t expected_snapshot_hash = 0
    );
    [[nodiscard]] const DmaPayloadRecord* find_payload(std::uint64_t global_sequence) const noexcept;
    [[nodiscard]] bool has_payload(std::uint64_t global_sequence) const noexcept;

    // In-memory direct operations (useful for unit tests without filesystem IO)
    void insert_in_memory_payload(DmaPayloadRecord record);

    void clear() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return is_writing_ || is_reading_; }
    [[nodiscard]] std::size_t record_count() const noexcept { return records_.size(); }
    [[nodiscard]] std::uint64_t total_payload_bytes() const noexcept { return total_bytes_; }
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept { return file_path_; }
    [[nodiscard]] std::uint64_t session_id() const noexcept { return session_id_; }
    [[nodiscard]] std::uint64_t base_snapshot_hash() const noexcept { return base_snapshot_hash_; }
    [[nodiscard]] RecordingLifecycleState lifecycle_state() const noexcept { return lifecycle_state_; }
    [[nodiscard]] std::uint64_t index_offset() const noexcept { return index_offset_; }
    [[nodiscard]] const std::unordered_map<std::uint64_t, DmaPayloadRecord>& all_records() const noexcept { return records_; }
    [[nodiscard]] const std::vector<DmaPayloadIndexEntry>& index_entries() const noexcept { return index_entries_; }

private:
    std::filesystem::path file_path_;
    std::ofstream write_file_;
    std::ifstream read_file_;
    bool is_writing_{false};
    bool is_reading_{false};
    bool finalized_{false};

    std::uint64_t session_id_{0};
    std::uint64_t base_snapshot_hash_{0};
    RecordingLifecycleState lifecycle_state_{RecordingLifecycleState::Created};
    std::uint64_t index_offset_{0};

    std::uint64_t total_records_{0};
    std::uint64_t total_bytes_{0};

    // Stored records indexed by global event sequence
    std::unordered_map<std::uint64_t, DmaPayloadRecord> records_;
    std::vector<DmaPayloadIndexEntry> index_entries_;
};

} // namespace winios::device

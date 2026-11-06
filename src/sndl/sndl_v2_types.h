#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace winios::sndl {

// -----------------------------------------------------------------------------
// SNDL v2 Versioning & Format Constants
// -----------------------------------------------------------------------------
inline constexpr std::array<std::uint8_t, 8> kWSndlV2Magic = {
    'W', 'S', 'N', 'D', '2', '\0', '\1', '\0'
};
inline constexpr std::array<std::uint8_t, 4> kWSndlV2BlockMagic = {
    'S', '2', 'B', 'K'
};
inline constexpr std::array<std::uint8_t, 8> kWSndlV2FooterMagic = {
    'W', 'S', 'N', 'D', '2', 'F', 'T', 'R'
};

inline constexpr std::uint16_t kWSndlVersion2 = 2;
inline constexpr std::size_t kWSndlV2HeaderSize = 64;
inline constexpr std::size_t kWSndlV2BlockHeaderSize = 48;
inline constexpr std::size_t kWSndlV2RecordSize = 80;
inline constexpr std::size_t kWSndlV2FooterSize = 64;

inline constexpr std::uint32_t kDefaultV2RecordsPerBlock = 512;
inline constexpr std::size_t kDefaultV2RecorderQueueCapacity = 65536; // 64K records = ~5.1 MiB buffer

// -----------------------------------------------------------------------------
// SNDL v2 Event Types (Structured Registry)
// -----------------------------------------------------------------------------
enum class SndlV2EventType : std::uint16_t {
    Unknown                     = 0,

    // Core Lifecycle Events (10..19)
    CoreCreated                 = 10,
    CoreStarted                 = 11,
    CoreHalted                  = 12,
    CoreEnteredWfi              = 13,
    CoreEnteredWfe              = 14,
    CoreResumed                 = 15,
    CoreStopped                 = 16,

    // Scheduler Decisions & Boundaries (20..29)
    QuantumStart                = 20,
    QuantumEnd                  = 21,
    EarlyTruncation             = 22,
    CoreSelected                = 23,
    BarrierEntry                = 24,
    BarrierRelease              = 25,

    // Interrupts & Exceptions (30..39)
    ExternalInterruptAsserted   = 30,
    InterruptRouted             = 31,
    InterruptAccepted           = 32,
    ExceptionTaken              = 33,
    ExceptionReturned           = 34,

    // Architectural Timers & Counters (40..49)
    CntpctRead                  = 40,
    CntvctRead                  = 41,
    PmccntrRead                 = 42,
    TimerComparatorExpired      = 43,

    // Nondeterministic IO & Entropy (50..59)
    MmioReadResult              = 50,
    EntropyReadResult           = 51,
    HostInputInjected           = 52,

    // Shared Memory & Exclusive Monitors (60..69)
    MemoryBarrierSync           = 60,
    ReservationSet              = 61,
    ReservationCleared          = 62,
    StoreExclusiveResult        = 63,

    // Virtual Device & DMA Operations (70..79)
    DeviceRegisterRead          = 70,
    DeviceRegisterWrite         = 71,
    DmaSubmissionAccepted       = 72,
    DmaSubmissionRejected       = 73,
    DmaTransferCompleted        = 74,
    DmaTransferFailed           = 75,
    DeviceInterruptAsserted     = 76,
    DeviceStateTransition       = 77,
    DmaDataChunk                = 78,

    // Session Identity & Replay Checkpoints (80..89)
    RecordingSessionMetadata    = 80,
    ReplayCheckpoint            = 81,
    PayloadStoreBinding         = 82
};

[[nodiscard]] constexpr std::string_view sndl_v2_event_type_name(SndlV2EventType t) noexcept {
    switch (t) {
    case SndlV2EventType::CoreCreated:               return "CoreCreated";
    case SndlV2EventType::CoreStarted:               return "CoreStarted";
    case SndlV2EventType::CoreHalted:                return "CoreHalted";
    case SndlV2EventType::CoreEnteredWfi:            return "CoreEnteredWfi";
    case SndlV2EventType::CoreEnteredWfe:            return "CoreEnteredWfe";
    case SndlV2EventType::CoreResumed:               return "CoreResumed";
    case SndlV2EventType::CoreStopped:               return "CoreStopped";
    case SndlV2EventType::QuantumStart:              return "QuantumStart";
    case SndlV2EventType::QuantumEnd:                return "QuantumEnd";
    case SndlV2EventType::EarlyTruncation:           return "EarlyTruncation";
    case SndlV2EventType::CoreSelected:              return "CoreSelected";
    case SndlV2EventType::BarrierEntry:              return "BarrierEntry";
    case SndlV2EventType::BarrierRelease:            return "BarrierRelease";
    case SndlV2EventType::ExternalInterruptAsserted: return "ExternalInterruptAsserted";
    case SndlV2EventType::InterruptRouted:           return "InterruptRouted";
    case SndlV2EventType::InterruptAccepted:         return "InterruptAccepted";
    case SndlV2EventType::ExceptionTaken:            return "ExceptionTaken";
    case SndlV2EventType::ExceptionReturned:         return "ExceptionReturned";
    case SndlV2EventType::CntpctRead:                return "CntpctRead";
    case SndlV2EventType::CntvctRead:                return "CntvctRead";
    case SndlV2EventType::PmccntrRead:               return "PmccntrRead";
    case SndlV2EventType::TimerComparatorExpired:    return "TimerComparatorExpired";
    case SndlV2EventType::MmioReadResult:            return "MmioReadResult";
    case SndlV2EventType::EntropyReadResult:         return "EntropyReadResult";
    case SndlV2EventType::HostInputInjected:         return "HostInputInjected";
    case SndlV2EventType::MemoryBarrierSync:         return "MemoryBarrierSync";
    case SndlV2EventType::ReservationSet:            return "ReservationSet";
    case SndlV2EventType::ReservationCleared:        return "ReservationCleared";
    case SndlV2EventType::StoreExclusiveResult:      return "StoreExclusiveResult";
    case SndlV2EventType::DeviceRegisterRead:        return "DeviceRegisterRead";
    case SndlV2EventType::DeviceRegisterWrite:       return "DeviceRegisterWrite";
    case SndlV2EventType::DmaSubmissionAccepted:     return "DmaSubmissionAccepted";
    case SndlV2EventType::DmaSubmissionRejected:     return "DmaSubmissionRejected";
    case SndlV2EventType::DmaTransferCompleted:      return "DmaTransferCompleted";
    case SndlV2EventType::DmaTransferFailed:         return "DmaTransferFailed";
    case SndlV2EventType::DeviceInterruptAsserted:   return "DeviceInterruptAsserted";
    case SndlV2EventType::DeviceStateTransition:     return "DeviceStateTransition";
    case SndlV2EventType::DmaDataChunk:              return "DmaDataChunk";
    case SndlV2EventType::RecordingSessionMetadata:  return "RecordingSessionMetadata";
    case SndlV2EventType::ReplayCheckpoint:          return "ReplayCheckpoint";
    case SndlV2EventType::PayloadStoreBinding:       return "PayloadStoreBinding";
    default:                                         return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// SNDL v2 Boundary Types
// -----------------------------------------------------------------------------
enum class SndlV2BoundaryType : std::uint8_t {
    Unknown               = 0,
    BeforeInstruction     = 1,
    DuringInstruction     = 2,
    AfterInstruction      = 3,
    BetweenInstructions   = 4,
    AsynchronousInput     = 5,
    ExceptionBoundary     = 6,
    SchedulerBoundary     = 7,
    BarrierBoundary       = 8,
    CoreLifecycleBoundary = 9,
    CheckpointBoundary    = 10
};

[[nodiscard]] constexpr std::string_view sndl_v2_boundary_type_name(SndlV2BoundaryType b) noexcept {
    switch (b) {
    case SndlV2BoundaryType::BeforeInstruction:     return "BeforeInstruction";
    case SndlV2BoundaryType::DuringInstruction:     return "DuringInstruction";
    case SndlV2BoundaryType::AfterInstruction:      return "AfterInstruction";
    case SndlV2BoundaryType::BetweenInstructions:   return "BetweenInstructions";
    case SndlV2BoundaryType::AsynchronousInput:     return "AsynchronousInput";
    case SndlV2BoundaryType::ExceptionBoundary:     return "ExceptionBoundary";
    case SndlV2BoundaryType::SchedulerBoundary:     return "SchedulerBoundary";
    case SndlV2BoundaryType::BarrierBoundary:       return "BarrierBoundary";
    case SndlV2BoundaryType::CoreLifecycleBoundary: return "CoreLifecycleBoundary";
    case SndlV2BoundaryType::CheckpointBoundary:    return "CheckpointBoundary";
    default:                                        return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// SNDL v2 Completion Status
// -----------------------------------------------------------------------------
enum class SndlV2CompletionStatus : std::uint16_t {
    Incomplete          = 0,
    Completed           = 1,
    Aborted             = 2,
    Overflow            = 3,
    RecoveredIncomplete = 4
};

[[nodiscard]] constexpr std::string_view sndl_v2_completion_status_name(SndlV2CompletionStatus s) noexcept {
    switch (s) {
    case SndlV2CompletionStatus::Completed:           return "Completed";
    case SndlV2CompletionStatus::Aborted:             return "Aborted";
    case SndlV2CompletionStatus::Overflow:            return "Overflow";
    case SndlV2CompletionStatus::RecoveredIncomplete: return "RecoveredIncomplete";
    default:                                          return "Incomplete";
    }
}

// -----------------------------------------------------------------------------
// SNDL v2 Error Codes
// -----------------------------------------------------------------------------
enum class SndlV2ErrorCode : std::uint32_t {
    Success                 = 0,
    FileOpenFailed          = 1,
    FileWriteFailed         = 2,
    FileReadFailed          = 3,
    FileTooShort            = 4,
    InvalidMagic            = 5,
    UnsupportedVersion      = 6,
    HeaderCrcMismatch       = 7,
    BlockHeaderCrcMismatch  = 8,
    BlockPayloadCrcMismatch = 9,
    FooterCrcMismatch       = 10,
    TruncatedBlock          = 11,
    InvalidSequence         = 12,
    MalformedRecord         = 13,
    BufferOverflow          = 14,
    SnapshotNotFound        = 15,
    SnapshotMismatch        = 16,
    IncompleteRecording     = 17,
    InvalidCoreId           = 18,
    InvalidStateTransition  = 19,
    RecordCrcMismatch       = 20,
    InvalidPayload          = 21,
    ResourceLimitExceeded   = 22,
    TrailingGarbage         = 23,
    SessionMismatch         = 24,
    CompanionMissing        = 25,
    CompanionCorrupt        = 26,
    UnrecoverableTransaction= 27
};

[[nodiscard]] constexpr std::string_view sndl_v2_error_name(SndlV2ErrorCode code) noexcept {
    switch (code) {
    case SndlV2ErrorCode::Success:                 return "Success";
    case SndlV2ErrorCode::FileOpenFailed:          return "File open failed";
    case SndlV2ErrorCode::FileWriteFailed:         return "File write failed";
    case SndlV2ErrorCode::FileReadFailed:          return "File read failed";
    case SndlV2ErrorCode::FileTooShort:            return "File too short";
    case SndlV2ErrorCode::InvalidMagic:            return "Invalid magic identifier";
    case SndlV2ErrorCode::UnsupportedVersion:      return "Unsupported format version";
    case SndlV2ErrorCode::HeaderCrcMismatch:       return "Header CRC32C mismatch";
    case SndlV2ErrorCode::BlockHeaderCrcMismatch:  return "Block header CRC32C mismatch";
    case SndlV2ErrorCode::BlockPayloadCrcMismatch: return "Block payload CRC32C mismatch";
    case SndlV2ErrorCode::FooterCrcMismatch:       return "Footer CRC32C mismatch";
    case SndlV2ErrorCode::TruncatedBlock:          return "Truncated block data";
    case SndlV2ErrorCode::InvalidSequence:         return "Sequence monotonicity violation";
    case SndlV2ErrorCode::MalformedRecord:         return "Malformed record structure";
    case SndlV2ErrorCode::BufferOverflow:          return "Recording buffer overflow";
    case SndlV2ErrorCode::SnapshotNotFound:        return "Associated snapshot not found";
    case SndlV2ErrorCode::SnapshotMismatch:        return "Snapshot hash/metadata mismatch";
    case SndlV2ErrorCode::IncompleteRecording:     return "Incomplete recording";
    case SndlV2ErrorCode::InvalidCoreId:           return "Invalid core identifier";
    case SndlV2ErrorCode::InvalidStateTransition:  return "Invalid lifecycle state transition";
    case SndlV2ErrorCode::RecordCrcMismatch:       return "Record CRC32C mismatch";
    case SndlV2ErrorCode::InvalidPayload:          return "Invalid event payload semantics";
    case SndlV2ErrorCode::ResourceLimitExceeded:   return "Resource allocation limit exceeded";
    case SndlV2ErrorCode::TrailingGarbage:         return "Unexpected trailing data after footer";
    case SndlV2ErrorCode::SessionMismatch:         return "Recording session identity mismatch";
    case SndlV2ErrorCode::CompanionMissing:        return "Companion payload store missing or incomplete";
    case SndlV2ErrorCode::CompanionCorrupt:        return "Companion payload store checksum mismatch";
    case SndlV2ErrorCode::UnrecoverableTransaction:return "Incomplete or unrecoverable transaction in recording";
    default:                                       return "Unknown error";
    }
}

// -----------------------------------------------------------------------------
// SndlV2Record: Uniform 80-Byte Multi-Core Event Record
// -----------------------------------------------------------------------------
struct alignas(8) SndlV2Record {
    std::uint64_t global_event_sequence{0};       // Strictly monotonic global sequence
    std::uint64_t global_retirement_sequence{0};  // Cluster-wide instruction retirement count
    std::uint64_t core_retirement_count{0};       // Per-core instruction retirement count
    std::uint64_t pc{0};                          // Architectural PC at event boundary
    std::uint64_t payload1{0};                    // Primary payload (addr, intid, reg, quantum)
    std::uint64_t payload2{0};                    // Secondary payload (val, prio, count, reason)
    std::uint64_t payload3{0};                    // Tertiary payload (width, status, extra)
    std::uint32_t core_id{0};                     // Originating/targeted core index
    std::uint32_t source_id{0};                   // Source hardware/device ID
    std::uint32_t flags{0};                       // Event-specific flags
    std::uint16_t sub_instruction_ordinal{0};     // Sub-instruction index (0 = primary)
    SndlV2EventType event_type{SndlV2EventType::Unknown};
    SndlV2BoundaryType boundary_type{SndlV2BoundaryType::Unknown};
    std::uint8_t reserved{0};
    std::uint16_t payload_size{0};                // Size of active payload bytes
    std::uint32_t record_crc32c{0};               // CRC32C over bytes [0..75]

    bool operator==(const SndlV2Record& other) const noexcept {
        return global_event_sequence == other.global_event_sequence &&
               global_retirement_sequence == other.global_retirement_sequence &&
               core_retirement_count == other.core_retirement_count &&
               pc == other.pc &&
               payload1 == other.payload1 &&
               payload2 == other.payload2 &&
               payload3 == other.payload3 &&
               core_id == other.core_id &&
               source_id == other.source_id &&
               flags == other.flags &&
               sub_instruction_ordinal == other.sub_instruction_ordinal &&
               event_type == other.event_type &&
               boundary_type == other.boundary_type &&
               payload_size == other.payload_size;
    }
    bool operator!=(const SndlV2Record& other) const noexcept {
        return !(*this == other);
    }
};
static_assert(sizeof(SndlV2Record) == 80, "SndlV2Record must be exactly 80 bytes");

// -----------------------------------------------------------------------------
// WSndlV2Header: Fixed 64-Byte File Header
// -----------------------------------------------------------------------------
struct alignas(8) WSndlV2Header {
    std::array<std::uint8_t, 8> magic{kWSndlV2Magic};
    std::uint16_t version{kWSndlVersion2};
    std::uint16_t core_count{1};
    std::uint16_t flags{0};
    std::uint16_t compression_type{0};            // 0 = none
    std::uint64_t base_snapshot_hash{0};          // Canonical multicore hash of .wsnap v2
    std::uint64_t base_snapshot_file_size{0};     // File size of .wsnap v2 in bytes
    std::uint64_t memory_capacity{0};             // Guest physical memory capacity in bytes
    std::uint64_t initial_global_sequence{0};     // Starting global event sequence
    std::uint32_t total_events{0};                // Total events written on finalization
    std::uint32_t total_blocks{0};                // Total blocks written on finalization
    std::uint32_t header_crc32c{0};               // CRC32C over bytes [0..59]
};
static_assert(sizeof(WSndlV2Header) == 64, "WSndlV2Header must be exactly 64 bytes");

// -----------------------------------------------------------------------------
// WSndlV2BlockHeader: Fixed 48-Byte Block Header
// -----------------------------------------------------------------------------
struct alignas(8) WSndlV2BlockHeader {
    std::array<std::uint8_t, 4> magic{kWSndlV2BlockMagic};
    std::uint16_t version{kWSndlVersion2};
    std::uint16_t flags{0};
    std::uint64_t first_global_sequence{0};       // First global sequence in block
    std::uint64_t last_global_sequence{0};        // Last global sequence in block
    std::uint32_t block_index{0};                 // 0-based block index
    std::uint32_t record_count{0};                // Records in this block
    std::uint32_t payload_bytes{0};               // Byte size of records payload
    std::uint32_t payload_crc32c{0};              // CRC32C over payload bytes
    std::uint32_t reserved{0};                    // Alignment padding
    std::uint32_t header_crc32c{0};               // CRC32C over bytes [0..43]
};
static_assert(sizeof(WSndlV2BlockHeader) == 48, "WSndlV2BlockHeader must be exactly 48 bytes");

// -----------------------------------------------------------------------------
// WSndlV2Footer: Fixed 64-Byte File Footer
// -----------------------------------------------------------------------------
struct alignas(8) WSndlV2Footer {
    std::array<std::uint8_t, 8> magic{kWSndlV2FooterMagic};
    std::uint64_t last_global_sequence{0};
    std::uint64_t last_global_retirement_sequence{0};
    std::uint64_t total_events{0};
    std::uint64_t dropped_events{0};
    std::uint32_t total_blocks{0};
    std::uint16_t version{kWSndlVersion2};
    SndlV2CompletionStatus completion_status{SndlV2CompletionStatus::Incomplete};
    std::array<std::uint8_t, 12> reserved{};
    std::uint32_t footer_crc32c{0};               // CRC32C over bytes [0..59]
};
static_assert(sizeof(WSndlV2Footer) == 64, "WSndlV2Footer must be exactly 64 bytes");

// -----------------------------------------------------------------------------
// Result and Validation Structures
// -----------------------------------------------------------------------------
struct SndlV2Result {
    SndlV2ErrorCode code{SndlV2ErrorCode::Success};
    std::string message;

    [[nodiscard]] bool is_success() const noexcept { return code == SndlV2ErrorCode::Success; }
    [[nodiscard]] bool is_failure() const noexcept { return code != SndlV2ErrorCode::Success; }

    static SndlV2Result ok() noexcept { return {SndlV2ErrorCode::Success, {}}; }
    static SndlV2Result fail(SndlV2ErrorCode c, std::string msg = {}) {
        return {c, msg.empty() ? std::string(sndl_v2_error_name(c)) : std::move(msg)};
    }
};

struct SndlV2InspectionInfo {
    WSndlV2Header header{};
    WSndlV2Footer footer{};
    bool has_valid_footer{false};
    std::uint64_t file_size_bytes{0};
    std::uint64_t total_records{0};
    std::uint32_t total_blocks{0};
    std::uint64_t min_global_sequence{0};
    std::uint64_t max_global_sequence{0};
    std::uint64_t max_global_retirement_sequence{0};
    std::uint64_t dropped_events{0};
    std::string formatted_summary;
};

struct SndlV2ValidationOptions {
    bool enable_semantic_validation{false};
    bool check_reserved_fields{false};
    bool enforce_strict_monotonicity{false};
    bool check_trailing_garbage{true};
    std::size_t max_block_payload_bytes{64 * 1024 * 1024}; // 64 MiB limit
};

struct SndlV2ValidationReport {
    bool is_valid{false};
    SndlV2ErrorCode code{SndlV2ErrorCode::Success};
    std::string message;
    std::uint64_t total_events{0};
    std::uint32_t total_blocks{0};
    std::uint64_t first_global_sequence{0};
    std::uint64_t last_global_sequence{0};
    std::uint64_t last_global_retirement_sequence{0};
    std::uint16_t core_count{0};
    std::uint64_t base_snapshot_hash{0};
    bool snapshot_checked{false};
    bool snapshot_matched{false};
    SndlV2CompletionStatus completion_status{SndlV2CompletionStatus::Incomplete};
    std::vector<std::string> diagnostics;
};

} // namespace winios::sndl

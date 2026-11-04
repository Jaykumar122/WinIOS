#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace winios::sndl {

// -----------------------------------------------------------------------------
// Versioning & Format Constants
// -----------------------------------------------------------------------------
inline constexpr std::array<std::uint8_t, 8> kWSndlMagic = {
    'W', 'S', 'N', 'D', 'L', '\0', '\1', '\0'
};
inline constexpr std::array<std::uint8_t, 4> kWSndlBlockMagic = {
    'S', 'B', 'L', 'K'
};
inline constexpr std::array<std::uint8_t, 8> kWSndlFooterMagic = {
    'W', 'S', 'N', 'D', 'L', 'F', 'T', 'R'
};

inline constexpr std::uint16_t kWSndlCurrentVersion = 1;
inline constexpr std::size_t kWSndlHeaderSize = 64;
inline constexpr std::size_t kWSndlBlockHeaderSize = 48;
inline constexpr std::size_t kWSndlRecordDiskSize = 64;
inline constexpr std::size_t kWSndlFooterSize = 64;

inline constexpr std::uint32_t kDefaultMaxRecordsPerBlock = 1024;
inline constexpr std::size_t kDefaultRecorderQueueCapacity = 65536; // 64K records = 4 MiB buffer

// -----------------------------------------------------------------------------
// Event Types
// -----------------------------------------------------------------------------
enum class SndlEventType : std::uint8_t {
    Unknown                 = 0,
    MmioReadResult          = 1, // Exact value returned by MMIO read
    ExternalInterruptInject = 2, // External interrupt asserted/injected
    InterruptDelivery       = 3, // Accepted interrupt delivery metadata
    TimerExpiration         = 4, // Nondeterministic timer expiration
    CounterReadResult       = 5, // Architectural counter read value consumed
    EntropyResult           = 6, // Exact random or entropy value returned
    HostInput               = 7, // Host input payload consumed by execution
    ExternalDeviceInput     = 8, // Device-provided input
    OtherExternalInput      = 9  // Other explicitly identified external input
};

[[nodiscard]] constexpr std::string_view sndl_event_type_name(SndlEventType t) noexcept {
    switch (t) {
    case SndlEventType::MmioReadResult:          return "MMIO_READ_RESULT";
    case SndlEventType::ExternalInterruptInject: return "EXTERNAL_INTERRUPT_INJECT";
    case SndlEventType::InterruptDelivery:       return "INTERRUPT_DELIVERY";
    case SndlEventType::TimerExpiration:         return "TIMER_EXPIRATION";
    case SndlEventType::CounterReadResult:       return "COUNTER_READ_RESULT";
    case SndlEventType::EntropyResult:           return "ENTROPY_RESULT";
    case SndlEventType::HostInput:               return "HOST_INPUT";
    case SndlEventType::ExternalDeviceInput:     return "EXTERNAL_DEVICE_INPUT";
    case SndlEventType::OtherExternalInput:      return "OTHER_EXTERNAL_INPUT";
    default:                                     return "UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// Boundary Types
// -----------------------------------------------------------------------------
enum class SndlBoundaryType : std::uint8_t {
    Unknown              = 0,
    BeforeInstruction    = 1, // Prior to instruction decoding/execution
    DuringInstruction    = 2, // Synchronous event during instruction execution
    AfterInstruction     = 3, // Immediately upon instruction retirement
    BetweenInstructions  = 4, // Between instruction retirement boundaries
    AsynchronousInput    = 5, // Asynchronous external arrival
    ExceptionBoundary    = 6  // Exception or interrupt vector entry boundary
};

[[nodiscard]] constexpr std::string_view sndl_boundary_type_name(SndlBoundaryType b) noexcept {
    switch (b) {
    case SndlBoundaryType::BeforeInstruction:   return "BeforeInstruction";
    case SndlBoundaryType::DuringInstruction:   return "DuringInstruction";
    case SndlBoundaryType::AfterInstruction:    return "AfterInstruction";
    case SndlBoundaryType::BetweenInstructions: return "BetweenInstructions";
    case SndlBoundaryType::AsynchronousInput:   return "AsynchronousInput";
    case SndlBoundaryType::ExceptionBoundary:   return "ExceptionBoundary";
    default:                                    return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// Lifecycle & Finalization States
// -----------------------------------------------------------------------------
enum class SndlLifecycleState : std::uint8_t {
    Idle       = 0,
    Starting   = 1,
    Recording  = 2,
    Stopping   = 3,
    Flushing   = 4,
    Finalizing = 5,
    Completed  = 6,
    Failed     = 7
};

enum class SndlFinalizationStatus : std::uint16_t {
    Incomplete = 0,
    Completed  = 1,
    Aborted    = 2,
    Overflow   = 3
};

// -----------------------------------------------------------------------------
// Error Codes
// -----------------------------------------------------------------------------
enum class SndlErrorCode : std::uint32_t {
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
    UnknownRequiredEvent    = 18,
    InvalidStateTransition  = 19
};

[[nodiscard]] constexpr std::string_view sndl_error_name(SndlErrorCode code) noexcept {
    switch (code) {
    case SndlErrorCode::Success:                 return "Success";
    case SndlErrorCode::FileOpenFailed:          return "File open failed";
    case SndlErrorCode::FileWriteFailed:         return "File write failed";
    case SndlErrorCode::FileReadFailed:          return "File read failed";
    case SndlErrorCode::FileTooShort:            return "File too short";
    case SndlErrorCode::InvalidMagic:            return "Invalid magic identifier";
    case SndlErrorCode::UnsupportedVersion:      return "Unsupported format version";
    case SndlErrorCode::HeaderCrcMismatch:       return "Header CRC32C mismatch";
    case SndlErrorCode::BlockHeaderCrcMismatch:  return "Block header CRC32C mismatch";
    case SndlErrorCode::BlockPayloadCrcMismatch: return "Block payload CRC32C mismatch";
    case SndlErrorCode::FooterCrcMismatch:       return "Footer CRC32C mismatch";
    case SndlErrorCode::TruncatedBlock:          return "Truncated block data";
    case SndlErrorCode::InvalidSequence:         return "Sequence monotonicity violation";
    case SndlErrorCode::MalformedRecord:         return "Malformed record structure";
    case SndlErrorCode::BufferOverflow:          return "Recording buffer overflow";
    case SndlErrorCode::SnapshotNotFound:        return "Associated snapshot not found";
    case SndlErrorCode::SnapshotMismatch:        return "Snapshot hash/metadata mismatch";
    case SndlErrorCode::IncompleteRecording:     return "Incomplete recording";
    case SndlErrorCode::UnknownRequiredEvent:    return "Unknown required event type";
    case SndlErrorCode::InvalidStateTransition:  return "Invalid lifecycle state transition";
    default:                                     return "Unknown error";
    }
}

// -----------------------------------------------------------------------------
// Uniform 64-Byte Record Structure
// -----------------------------------------------------------------------------
struct alignas(8) SndlRecord {
    std::uint64_t sequence_num{0};               // Global monotonic sequence number
    std::uint64_t instruction_retirement_count{0};// Per-core retirement counter
    std::uint64_t pc{0};                         // Architectural PC at event
    std::uint64_t payload1{0};                   // Primary event value (address / intid / reg)
    std::uint64_t payload2{0};                   // Secondary event value (data / priority / cval)
    std::uint64_t payload3{0};                   // Tertiary event value (width / counter / freq)
    std::uint32_t source_id{0};                  // Source device/channel ID
    std::uint32_t flags{0};                      // Event-specific flags
    std::uint16_t core_id{0};                    // Originating CPU core ID
    std::uint16_t sub_instruction_ordinal{0};    // Sub-instruction event index (0, 1, 2...)
    SndlEventType event_type{SndlEventType::Unknown};
    SndlBoundaryType boundary_type{SndlBoundaryType::Unknown};
    std::uint16_t payload_size{0};               // Payload byte count (up to 24 bytes inlined)

    bool operator==(const SndlRecord& other) const noexcept {
        return sequence_num == other.sequence_num &&
               instruction_retirement_count == other.instruction_retirement_count &&
               pc == other.pc &&
               payload1 == other.payload1 &&
               payload2 == other.payload2 &&
               payload3 == other.payload3 &&
               source_id == other.source_id &&
               flags == other.flags &&
               core_id == other.core_id &&
               sub_instruction_ordinal == other.sub_instruction_ordinal &&
               event_type == other.event_type &&
               boundary_type == other.boundary_type &&
               payload_size == other.payload_size;
    }
    bool operator!=(const SndlRecord& other) const noexcept {
        return !(*this == other);
    }
};
static_assert(sizeof(SndlRecord) == 64, "SndlRecord must be exactly 64 bytes");

// -----------------------------------------------------------------------------
// Operation Result Wrappers
// -----------------------------------------------------------------------------
struct SndlResult {
    SndlErrorCode code{SndlErrorCode::Success};
    std::string message;

    [[nodiscard]] bool is_success() const noexcept { return code == SndlErrorCode::Success; }
    [[nodiscard]] bool is_failure() const noexcept { return code != SndlErrorCode::Success; }

    static SndlResult ok() noexcept { return {SndlErrorCode::Success, {}}; }
    static SndlResult fail(SndlErrorCode c, std::string msg = {}) {
        return {c, msg.empty() ? std::string(sndl_error_name(c)) : std::move(msg)};
    }
};

struct SndlValidationResult {
    bool is_valid{false};
    SndlErrorCode code{SndlErrorCode::Success};
    std::string message;
    std::uint64_t total_records{0};
    std::uint32_t total_blocks{0};
    std::uint64_t first_sequence{0};
    std::uint64_t last_sequence{0};
    std::uint64_t total_instructions{0};
    std::uint16_t core_count{0};
    std::uint64_t base_snapshot_hash{0};
    bool snapshot_checked{false};
    bool snapshot_matched{false};
    SndlFinalizationStatus finalization_status{SndlFinalizationStatus::Incomplete};
};

} // namespace winios::sndl

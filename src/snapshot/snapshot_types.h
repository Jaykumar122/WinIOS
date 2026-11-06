#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace winios::snapshot {

enum class SnapshotSectionType : std::uint32_t {
    Invalid     = 0x00u,
    CpuState    = 0x01u,
    MemoryState = 0x02u,
    TimerState  = 0x03u,
    GicState    = 0x04u,
    PmuState    = 0x05u,
    DebugState  = 0x06u,
    Metadata    = 0x07u,
    // Phase 52 Stage 23: Multi-Core Cluster Sections
    CoreDirectory          = 0x11u,
    CoreArchitecturalState = 0x12u,
    SharedMemory           = 0x13u,
    Scheduler              = 0x14u,
    ExclusiveMonitor       = 0x15u,
    InterruptRouter        = 0x16u,
    Integrity              = 0x17u,
    DeviceState            = 0x18u,
    Custom      = 0x80u
};

[[nodiscard]] constexpr std::string_view section_type_name(SnapshotSectionType type) noexcept {
    switch (type) {
    case SnapshotSectionType::CpuState:               return "CPU_STATE";
    case SnapshotSectionType::MemoryState:            return "MEMORY_STATE";
    case SnapshotSectionType::TimerState:             return "TIMER_STATE";
    case SnapshotSectionType::GicState:               return "GIC_STATE";
    case SnapshotSectionType::PmuState:               return "PMU_STATE";
    case SnapshotSectionType::DebugState:             return "DEBUG_STATE";
    case SnapshotSectionType::Metadata:               return "METADATA";
    case SnapshotSectionType::CoreDirectory:          return "CORE_DIRECTORY";
    case SnapshotSectionType::CoreArchitecturalState: return "CORE_ARCHITECTURAL_STATE";
    case SnapshotSectionType::SharedMemory:           return "SHARED_MEMORY";
    case SnapshotSectionType::Scheduler:              return "SCHEDULER";
    case SnapshotSectionType::ExclusiveMonitor:       return "EXCLUSIVE_MONITOR";
    case SnapshotSectionType::InterruptRouter:        return "INTERRUPT_ROUTER";
    case SnapshotSectionType::Integrity:              return "INTEGRITY";
    case SnapshotSectionType::DeviceState:            return "DEVICE_STATE";
    case SnapshotSectionType::Custom:                 return "CUSTOM";
    default:                                          return "UNKNOWN";
    }
}

enum class SnapshotCompressionType : std::uint16_t {
    None = 0x0000u,
    LZ4  = 0x0001u,
    Zstd = 0x0002u
};

enum class SnapshotErrorCode : std::uint32_t {
    Success                 = 0,
    FileNotFound            = 1,
    ReadError               = 2,
    WriteError              = 3,
    InvalidMagic            = 4,
    UnsupportedVersion      = 5,
    HeaderCrcMismatch       = 6,
    SectionCrcMismatch      = 7,
    TruncatedFile           = 8,
    InvalidSectionDirectory = 9,
    OverlappingSections     = 10,
    OversizedSection        = 11,
    InvalidStateData        = 12,
    MemoryCapacityMismatch  = 13,
    CorruptedChunk          = 14,
    MissingRequiredSection  = 15,
    RestorationFailed       = 16,
    InvalidArgument         = 17,
    DeviceBusy              = 18
};

[[nodiscard]] constexpr std::string_view error_code_name(SnapshotErrorCode code) noexcept {
    switch (code) {
    case SnapshotErrorCode::Success:                 return "Success";
    case SnapshotErrorCode::FileNotFound:            return "File not found";
    case SnapshotErrorCode::ReadError:               return "Read error";
    case SnapshotErrorCode::WriteError:              return "Write error";
    case SnapshotErrorCode::InvalidMagic:            return "Invalid magic identifier";
    case SnapshotErrorCode::UnsupportedVersion:      return "Unsupported format version";
    case SnapshotErrorCode::HeaderCrcMismatch:       return "Header CRC32C mismatch";
    case SnapshotErrorCode::SectionCrcMismatch:      return "Section CRC32C mismatch";
    case SnapshotErrorCode::TruncatedFile:           return "Truncated snapshot file";
    case SnapshotErrorCode::InvalidSectionDirectory: return "Invalid section directory";
    case SnapshotErrorCode::OverlappingSections:     return "Overlapping section ranges detected";
    case SnapshotErrorCode::OversizedSection:        return "Section exceeds size boundaries";
    case SnapshotErrorCode::InvalidStateData:        return "Invalid architectural state data";
    case SnapshotErrorCode::MemoryCapacityMismatch:  return "Guest memory capacity mismatch";
    case SnapshotErrorCode::CorruptedChunk:          return "Corrupted memory chunk payload";
    case SnapshotErrorCode::MissingRequiredSection:  return "Missing required section";
    case SnapshotErrorCode::RestorationFailed:       return "Snapshot restoration failed";
    case SnapshotErrorCode::InvalidArgument:         return "Invalid argument supplied";
    case SnapshotErrorCode::DeviceBusy:              return "Device busy during snapshot capture";
    default:                                         return "Unknown error";
    }
}

struct SnapshotResult {
    bool success{false};
    SnapshotErrorCode error_code{SnapshotErrorCode::Success};
    std::string error_message;

    [[nodiscard]] static SnapshotResult ok() noexcept {
        return SnapshotResult{true, SnapshotErrorCode::Success, ""};
    }

    [[nodiscard]] static SnapshotResult fail(SnapshotErrorCode code, std::string message) {
        return SnapshotResult{false, code, std::move(message)};
    }
};

struct SnapshotValidationResult {
    bool valid{false};
    SnapshotErrorCode error_code{SnapshotErrorCode::Success};
    std::string error_message;
    std::vector<std::string> diagnostics;

    [[nodiscard]] static SnapshotValidationResult ok() noexcept {
        return SnapshotValidationResult{true, SnapshotErrorCode::Success, "", {}};
    }

    [[nodiscard]] static SnapshotValidationResult fail(SnapshotErrorCode code, std::string message, std::vector<std::string> diags = {}) {
        return SnapshotValidationResult{false, code, std::move(message), std::move(diags)};
    }
};

} // namespace winios::snapshot

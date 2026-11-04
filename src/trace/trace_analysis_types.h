#pragma once

#include "trace_event.h"
#include "trace_serializer.h"
#include "trace_types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winios::trace {

// -----------------------------------------------------------------------------
// TraceFileIntegrityStatus: High-level classification of trace file validity
// -----------------------------------------------------------------------------
enum class TraceFileIntegrityStatus : std::uint32_t {
    ValidComplete          = 1,  // Complete, properly finalized trace with valid blocks and footer
    ValidNoEvents          = 2,  // Valid header and footer, 0 blocks and 0 events recorded
    TruncatedTrace         = 3,  // File truncated prematurely (incomplete block or payload)
    InvalidHeader          = 4,  // Header magic, version, size, or header CRC is corrupt/invalid
    InvalidBlockHeader     = 5,  // Block magic, header size, or block sequence invalid
    CrcMismatch            = 6,  // Checksum mismatch in block payload, header, or footer
    InvalidFooter          = 7,  // Footer missing, corrupted, or footer CRC invalid
    UnsupportedVersion     = 8,  // Format version or schema major version unsupported
    UnknownEventType       = 9,  // File contains unallocated or unrecognized event types
    ContainsDroppedMarkers = 10  // File contains DroppedMarker events indicating buffer drops
};

constexpr inline std::string_view to_string(TraceFileIntegrityStatus status) noexcept {
    switch (status) {
        case TraceFileIntegrityStatus::ValidComplete:          return "ValidComplete";
        case TraceFileIntegrityStatus::ValidNoEvents:          return "ValidNoEvents";
        case TraceFileIntegrityStatus::TruncatedTrace:         return "TruncatedTrace";
        case TraceFileIntegrityStatus::InvalidHeader:          return "InvalidHeader";
        case TraceFileIntegrityStatus::InvalidBlockHeader:     return "InvalidBlockHeader";
        case TraceFileIntegrityStatus::CrcMismatch:            return "CrcMismatch";
        case TraceFileIntegrityStatus::InvalidFooter:          return "InvalidFooter";
        case TraceFileIntegrityStatus::UnsupportedVersion:     return "UnsupportedVersion";
        case TraceFileIntegrityStatus::UnknownEventType:       return "UnknownEventType";
        case TraceFileIntegrityStatus::ContainsDroppedMarkers: return "ContainsDroppedMarkers";
        default:                                               return "UnknownStatus";
    }
}

// -----------------------------------------------------------------------------
// Diagnostic Severity & Error Codes
// -----------------------------------------------------------------------------
enum class DiagnosticSeverity : std::uint8_t {
    Info    = 0,
    Warning = 1,
    Error   = 2
};

constexpr inline std::string_view to_string(DiagnosticSeverity s) noexcept {
    switch (s) {
        case DiagnosticSeverity::Info:    return "Info";
        case DiagnosticSeverity::Warning: return "Warning";
        case DiagnosticSeverity::Error:   return "Error";
        default:                          return "Unknown";
    }
}

enum class DiagnosticCode : std::uint32_t {
    Ok                             = 0,
    FileTruncated                  = 1,
    HeaderMagicMismatch            = 2,
    HeaderVersionUnsupported       = 3,
    HeaderCrcMismatch              = 4,
    BlockMagicMismatch             = 5,
    BlockSequenceMismatch          = 6,
    BlockPayloadCrcMismatch        = 7,
    BlockHeaderCrcMismatch         = 8,
    BlockTruncated                 = 9,
    FooterMissing                  = 10,
    FooterMagicMismatch            = 11,
    FooterCrcMismatch              = 12,
    EventCountMismatch             = 13,
    BlockCountMismatch             = 14,
    DroppedMarkerEncountered       = 15,
    UnknownEventTypeEncountered    = 16,
    SequenceRegression             = 17,
    SequenceGap                    = 18,
    ExceptionReturnWithoutEntry    = 19,
    UnmatchedExceptionEntry        = 20,
    InterruptWithoutAssertion      = 21,
    TimerAssertWithoutExpired      = 22,
    TimerExpirationWithoutInterrupt= 23,
    CPUStoppedWithoutStart         = 24,
    DuplicateCPUStart              = 25,
    DuplicateCPUStop               = 26,
    ContextChangeIncompleteMetadata= 27
};

constexpr inline std::string_view to_string(DiagnosticCode c) noexcept {
    switch (c) {
        case DiagnosticCode::Ok:                              return "Ok";
        case DiagnosticCode::FileTruncated:                   return "FileTruncated";
        case DiagnosticCode::HeaderMagicMismatch:             return "HeaderMagicMismatch";
        case DiagnosticCode::HeaderVersionUnsupported:        return "HeaderVersionUnsupported";
        case DiagnosticCode::HeaderCrcMismatch:               return "HeaderCrcMismatch";
        case DiagnosticCode::BlockMagicMismatch:              return "BlockMagicMismatch";
        case DiagnosticCode::BlockSequenceMismatch:           return "BlockSequenceMismatch";
        case DiagnosticCode::BlockPayloadCrcMismatch:         return "BlockPayloadCrcMismatch";
        case DiagnosticCode::BlockHeaderCrcMismatch:          return "BlockHeaderCrcMismatch";
        case DiagnosticCode::BlockTruncated:                  return "BlockTruncated";
        case DiagnosticCode::FooterMissing:                   return "FooterMissing";
        case DiagnosticCode::FooterMagicMismatch:             return "FooterMagicMismatch";
        case DiagnosticCode::FooterCrcMismatch:               return "FooterCrcMismatch";
        case DiagnosticCode::EventCountMismatch:              return "EventCountMismatch";
        case DiagnosticCode::BlockCountMismatch:              return "BlockCountMismatch";
        case DiagnosticCode::DroppedMarkerEncountered:        return "DroppedMarkerEncountered";
        case DiagnosticCode::UnknownEventTypeEncountered:     return "UnknownEventTypeEncountered";
        case DiagnosticCode::SequenceRegression:              return "SequenceRegression";
        case DiagnosticCode::SequenceGap:                     return "SequenceGap";
        case DiagnosticCode::ExceptionReturnWithoutEntry:     return "ExceptionReturnWithoutEntry";
        case DiagnosticCode::UnmatchedExceptionEntry:         return "UnmatchedExceptionEntry";
        case DiagnosticCode::InterruptWithoutAssertion:       return "InterruptWithoutAssertion";
        case DiagnosticCode::TimerAssertWithoutExpired:       return "TimerAssertWithoutExpired";
        case DiagnosticCode::TimerExpirationWithoutInterrupt: return "TimerExpirationWithoutInterrupt";
        case DiagnosticCode::CPUStoppedWithoutStart:          return "CPUStoppedWithoutStart";
        case DiagnosticCode::DuplicateCPUStart:               return "DuplicateCPUStart";
        case DiagnosticCode::DuplicateCPUStop:                return "DuplicateCPUStop";
        case DiagnosticCode::ContextChangeIncompleteMetadata: return "ContextChangeIncompleteMetadata";
        default:                                              return "UnknownDiagnostic";
    }
}

struct TraceDiagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Info};
    DiagnosticCode code{DiagnosticCode::Ok};
    std::uint16_t core_id{0};
    std::uint64_t sequence_number{0};
    std::uint64_t file_offset{0};
    std::uint16_t event_type{0};
    std::string message{};
    bool is_definitely_invalid{false}; // true = physical corruption / invariant violation; false = suspicious / incomplete sequence
};

// -----------------------------------------------------------------------------
// Timer and Interrupt Causal Relationship
// -----------------------------------------------------------------------------
enum class CausalCorrelationStatus : std::uint8_t {
    ConfirmedSequence   = 0, // Expired -> InterruptAsserted -> InterruptDelivered -> ExceptionEntered -> ContextChanged verified
    PossibleCorrelation = 1, // Likely correlated by timestamp & INTID, but intermediate steps unconfirmed
    IncompleteSequence  = 2, // Fired/asserted, but interrupted, masked, or trace truncated before completion
    UncorrelatedEvent   = 3  // Interrupt delivered or asserted without matching timer context
};

constexpr inline std::string_view to_string(CausalCorrelationStatus s) noexcept {
    switch (s) {
        case CausalCorrelationStatus::ConfirmedSequence:   return "ConfirmedSequence";
        case CausalCorrelationStatus::PossibleCorrelation: return "PossibleCorrelation";
        case CausalCorrelationStatus::IncompleteSequence:  return "IncompleteSequence";
        case CausalCorrelationStatus::UncorrelatedEvent:   return "UncorrelatedEvent";
        default:                                           return "UnknownCorrelation";
    }
}

struct TimerInterruptChain {
    std::uint16_t core_id{0};
    CausalCorrelationStatus status{CausalCorrelationStatus::UncorrelatedEvent};
    std::uint8_t timer_type{0}; // 0 = Physical, 1 = Virtual, 2 = HypervisorPhysical
    std::uint64_t expire_seq{0};
    std::uint64_t expire_time{0};
    std::uint64_t assert_seq{0};
    std::uint64_t assert_time{0};
    std::uint64_t deliver_seq{0};
    std::uint64_t deliver_time{0};
    std::uint32_t intid{0};
    std::uint64_t entry_seq{0};
    std::uint64_t entry_time{0};
    std::uint64_t entry_vector{0};
    std::uint64_t context_change_seq{0};
    std::uint64_t context_change_time{0};
    std::string explanation{};
};

// -----------------------------------------------------------------------------
// AddressFilterMode & TraceFilter
// -----------------------------------------------------------------------------
enum class AddressFilterMode : std::uint8_t {
    All               = 0, // Match address against all event types
    MemoryOnly        = 1, // Match address only for MemoryRead / MemoryWrite
    BranchTargetOnly  = 2, // Match address only for BranchTaken
    TimerCompareOnly  = 3, // Match address only for TimerEvent (cval)
    VectorTargetOnly  = 4  // Match address only for ExceptionEntered / InterruptDelivered
};

constexpr inline std::string_view to_string(AddressFilterMode m) noexcept {
    switch (m) {
        case AddressFilterMode::All:              return "All";
        case AddressFilterMode::MemoryOnly:       return "MemoryOnly";
        case AddressFilterMode::BranchTargetOnly: return "BranchTargetOnly";
        case AddressFilterMode::TimerCompareOnly: return "TimerCompareOnly";
        case AddressFilterMode::VectorTargetOnly: return "VectorTargetOnly";
        default:                                  return "UnknownMode";
    }
}

struct TraceFilter {
    std::optional<std::vector<TraceEventType>> event_types{};
    std::optional<std::vector<std::uint16_t>> core_ids{};
    std::optional<std::uint8_t> min_el{};
    std::optional<std::uint8_t> max_el{};
    std::optional<std::uint64_t> min_pc{};
    std::optional<std::uint64_t> max_pc{};
    std::optional<std::uint64_t> min_address{};
    std::optional<std::uint64_t> max_address{};
    AddressFilterMode address_mode{AddressFilterMode::All};
    std::optional<std::uint64_t> min_timestamp{};
    std::optional<std::uint64_t> max_timestamp{};
    std::optional<std::uint64_t> min_sequence{};
    std::optional<std::uint64_t> max_sequence{};

    [[nodiscard]] bool is_valid(std::string* out_error = nullptr) const {
        if (min_el && max_el && *min_el > *max_el) {
            if (out_error) *out_error = "min_el cannot exceed max_el";
            return false;
        }
        if (min_pc && max_pc && *min_pc > *max_pc) {
            if (out_error) *out_error = "min_pc cannot exceed max_pc";
            return false;
        }
        if (min_address && max_address && *min_address > *max_address) {
            if (out_error) *out_error = "min_address cannot exceed max_address";
            return false;
        }
        if (min_timestamp && max_timestamp && *min_timestamp > *max_timestamp) {
            if (out_error) *out_error = "min_timestamp cannot exceed max_timestamp";
            return false;
        }
        if (min_sequence && max_sequence && *min_sequence > *max_sequence) {
            if (out_error) *out_error = "min_sequence cannot exceed max_sequence";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool matches(const TraceEvent& ev) const noexcept {
        if (event_types.has_value()) {
            bool found = false;
            for (const auto& t : *event_types) {
                if (ev.get_event_type() == t) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        if (core_ids.has_value()) {
            bool found = false;
            for (const auto& c : *core_ids) {
                if (ev.core_id == c) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        if (min_el.has_value() && ev.exception_level < *min_el) return false;
        if (max_el.has_value() && ev.exception_level > *max_el) return false;
        if (min_pc.has_value() && ev.pc < *min_pc) return false;
        if (max_pc.has_value() && ev.pc > *max_pc) return false;

        // Address filtering with mode awareness
        if (min_address.has_value() || max_address.has_value()) {
            bool should_check_address = false;
            switch (address_mode) {
                case AddressFilterMode::All:
                    should_check_address = true;
                    break;
                case AddressFilterMode::MemoryOnly:
                    should_check_address = (ev.get_event_type() == TraceEventType::MemoryRead ||
                                            ev.get_event_type() == TraceEventType::MemoryWrite);
                    break;
                case AddressFilterMode::BranchTargetOnly:
                    should_check_address = (ev.get_event_type() == TraceEventType::BranchTaken);
                    break;
                case AddressFilterMode::TimerCompareOnly:
                    should_check_address = (ev.get_event_type() == TraceEventType::TimerEvent);
                    break;
                case AddressFilterMode::VectorTargetOnly:
                    should_check_address = (ev.get_event_type() == TraceEventType::ExceptionEntered ||
                                            ev.get_event_type() == TraceEventType::InterruptDelivered);
                    break;
            }
            if (!should_check_address) return false;
            if (min_address.has_value() && ev.address < *min_address) return false;
            if (max_address.has_value() && ev.address > *max_address) return false;
        }

        if (min_timestamp.has_value() && ev.virtual_time < *min_timestamp) return false;
        if (max_timestamp.has_value() && ev.virtual_time > *max_timestamp) return false;
        if (min_sequence.has_value() && ev.sequence_number < *min_sequence) return false;
        if (max_sequence.has_value() && ev.sequence_number > *max_sequence) return false;

        return true;
    }
};

// -----------------------------------------------------------------------------
// Timeline Entry
// -----------------------------------------------------------------------------
struct TimelineEntry {
    std::uint64_t file_index{0};
    std::uint64_t sequence_number{0};
    std::uint16_t core_id{0};
    std::uint64_t timestamp{0};
    TraceEventType event_type{TraceEventType::Invalid};
    std::uint8_t exception_level{0};
    std::uint64_t pc{0};
    std::uint64_t address{0};
    std::uint64_t data{0};
    std::uint32_t opcode_or_syndrome{0};
    std::uint32_t flags{0};
    std::string decoded_info{};
};

// -----------------------------------------------------------------------------
// Subsystem Summaries
// -----------------------------------------------------------------------------
struct InstructionAnalysis {
    std::uint64_t total_retired{0};
    std::map<std::uint16_t, std::uint64_t> per_core_count{};
    std::uint64_t min_pc{0};
    std::uint64_t max_pc{0};
    std::uint64_t first_pc{0};
    std::uint64_t last_pc{0};
    std::uint64_t unique_pc_count{0};
    std::uint64_t sequence_gaps{0};
    std::uint64_t sequence_regressions{0};
};

struct BranchAnalysis {
    std::uint64_t total_branches{0};
    std::map<std::uint16_t, std::uint64_t> per_core_count{};
    std::uint64_t forward_branches{0};
    std::uint64_t backward_branches{0};
    std::uint64_t branches_without_target{0};
};

struct ExceptionAnalysis {
    std::uint64_t total_entries{0};
    std::uint64_t total_returns{0};
    std::uint64_t entries_per_el[4]{0, 0, 0, 0};
    std::uint64_t returns_per_el[4]{0, 0, 0, 0};
    std::uint64_t synchronous_entries{0};
    std::uint64_t asynchronous_entries{0};
    std::uint64_t unmatched_entries{0};
    std::uint64_t unmatched_returns{0};
};

struct MemoryAnalysis {
    std::uint64_t total_reads{0};
    std::uint64_t total_writes{0};
    std::uint64_t total_atomics{0};
    std::map<std::uint16_t, std::uint64_t> per_core_reads{};
    std::map<std::uint16_t, std::uint64_t> per_core_writes{};
    std::uint64_t size_1b{0};
    std::uint64_t size_2b{0};
    std::uint64_t size_4b{0};
    std::uint64_t size_8b{0};
    std::uint64_t size_16b{0};
    std::uint64_t size_other{0};
    std::uint64_t min_address{0};
    std::uint64_t max_address{0};
};

struct SystemRegisterAnalysis {
    std::uint64_t total_accesses{0};
    std::uint64_t total_reads{0};
    std::uint64_t total_writes{0};
    std::map<std::uint16_t, std::uint64_t> per_core_count{};
    std::map<std::uint64_t, std::uint64_t> register_counts{};
};

struct TimerAnalysis {
    std::uint64_t compare_updates{0};
    std::uint64_t control_updates{0};
    std::uint64_t expirations{0};
    std::uint64_t interrupt_assertions{0};
    std::uint64_t interrupt_clears{0};
    std::uint64_t resets{0};
    std::uint64_t per_type_counts[3]{0, 0, 0}; // Physical, Virtual, HypervisorPhysical
};

struct PerCoreSummary {
    std::uint16_t core_id{0};
    std::uint64_t event_count{0};
    std::uint64_t first_sequence{0};
    std::uint64_t last_sequence{0};
    std::uint64_t first_timestamp{0};
    std::uint64_t last_timestamp{0};
    std::map<TraceEventType, std::uint64_t> type_counts{};
};

// -----------------------------------------------------------------------------
// Overall Inspection & Analysis Results
// -----------------------------------------------------------------------------
struct TraceFileInspectionResult {
    std::string file_path{};
    std::uint64_t file_size{0};
    std::uint16_t format_version{0};
    std::uint32_t schema_version{0};
    bool header_valid{false};
    bool footer_valid{false};
    bool crc_valid{false};
    TraceFileIntegrityStatus integrity_status{TraceFileIntegrityStatus::ValidComplete};
    std::uint64_t block_count{0};
    std::uint64_t event_count{0};
    std::uint64_t footer_expected_events{0};
    std::uint64_t footer_expected_blocks{0};
    std::uint64_t dropped_markers_count{0};
    std::uint64_t unknown_event_types_count{0};
    std::uint64_t trace_start_time{0};
    std::uint64_t trace_end_time{0};
    std::map<std::uint16_t, std::uint64_t> per_core_event_counts{};
    std::vector<TraceDiagnostic> diagnostics{};
};

struct TraceAnalysisReport {
    TraceFileInspectionResult inspection{};
    TraceFilter active_filter{};
    std::uint64_t filtered_events_count{0};
    InstructionAnalysis instructions{};
    BranchAnalysis branches{};
    ExceptionAnalysis exceptions{};
    MemoryAnalysis memory{};
    SystemRegisterAnalysis sysregs{};
    TimerAnalysis timers{};
    std::vector<PerCoreSummary> core_summaries{};
    std::vector<TimerInterruptChain> causal_chains{};
    std::vector<TraceDiagnostic> sequence_diagnostics{};
    std::vector<TimelineEntry> sample_timeline{};
};

} // namespace winios::trace

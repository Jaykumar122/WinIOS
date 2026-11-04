#pragma once

#include "trace_analysis_types.h"
#include "trace_event.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace winios::trace {

// -----------------------------------------------------------------------------
// TraceAnalyzer: High-performance, read-only trace file inspector and analyzer
// -----------------------------------------------------------------------------
class TraceAnalyzer {
public:
    TraceAnalyzer() = default;
    ~TraceAnalyzer() = default;

    // Non-copyable, movable
    TraceAnalyzer(const TraceAnalyzer&) = delete;
    TraceAnalyzer& operator=(const TraceAnalyzer&) = delete;
    TraceAnalyzer(TraceAnalyzer&&) noexcept = default;
    TraceAnalyzer& operator=(TraceAnalyzer&&) noexcept = default;

    // Inspects file structure, headers, block integrity, footer, and basic metadata.
    // Distinguishes all 10 corruption/validity states.
    [[nodiscard]] TraceFileInspectionResult inspect_file(const std::filesystem::path& path) const;

    // Performs full execution analysis, subsystem summaries, sequence validation,
    // and timer-to-interrupt causal analysis using streaming block-by-block processing.
    [[nodiscard]] TraceAnalysisReport analyze_file(
        const std::filesystem::path& path,
        const TraceFilter& filter = {},
        std::size_t max_timeline_events = 1000) const;

    // Reconstructs timeline entries matching filter.
    // If core_id filter is specified, presents per-core timeline.
    [[nodiscard]] std::vector<TimelineEntry> extract_timeline(
        const std::filesystem::path& path,
        const TraceFilter& filter = {},
        std::size_t max_events = 0) const;

    // Streams timeline entries directly to a callback to avoid large in-memory collections.
    bool stream_timeline(
        const std::filesystem::path& path,
        const TraceFilter& filter,
        const std::function<bool(const TimelineEntry&)>& consumer) const;

    // Decodes event-specific metadata into a concise human-readable description string.
    [[nodiscard]] static std::string decode_event_details(const TraceEvent& ev);

private:
    // Helper to process an event during full analysis
    static void process_event(
        const TraceEvent& ev,
        std::uint64_t file_index,
        TraceAnalysisReport& report,
        std::map<std::uint16_t, std::uint64_t>& last_seq_per_core,
        std::map<std::uint16_t, bool>& cpu_started_per_core,
        std::map<std::uint16_t, bool>& cpu_stopped_per_core,
        std::map<std::uint16_t, std::uint32_t>& in_exception_depth_per_core,
        std::map<std::uint16_t, bool>& pending_timer_irq_per_core,
        std::map<std::pair<std::uint16_t, std::uint8_t>, TimerInterruptChain>& active_timer_chains);
};

} // namespace winios::trace

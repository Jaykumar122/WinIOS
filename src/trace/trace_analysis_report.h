#pragma once

#include "trace_analysis_types.h"

#include <string>
#include <vector>

namespace winios::trace {

// -----------------------------------------------------------------------------
// Human-Readable Text Formatters
// -----------------------------------------------------------------------------
[[nodiscard]] std::string format_inspection_report(const TraceFileInspectionResult& res);
[[nodiscard]] std::string format_summary_report(const TraceAnalysisReport& rep);
[[nodiscard]] std::string format_validation_report(const TraceAnalysisReport& rep);
[[nodiscard]] std::string format_timeline_report(const std::vector<TimelineEntry>& timeline);

// -----------------------------------------------------------------------------
// Machine-Readable JSON Exporters
// -----------------------------------------------------------------------------
[[nodiscard]] std::string export_report_to_json(const TraceAnalysisReport& rep);
[[nodiscard]] std::string export_timeline_to_json(const std::vector<TimelineEntry>& timeline);

// -----------------------------------------------------------------------------
// JSON String Escape Utility
// -----------------------------------------------------------------------------
[[nodiscard]] std::string escape_json_string(const std::string& input);

} // namespace winios::trace

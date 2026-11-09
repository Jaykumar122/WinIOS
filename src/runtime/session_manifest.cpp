#include "runtime/session_manifest.h"
#include "runtime/artifact_publication.h"
#include "snapshot/snapshot_manager.h"
#include "sndl/sndl_validation.h"
#include "sndl/sndl_source_hooks.h"
#include "trace/trace_serializer.h"
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
using Hashes = winios::replay::ReplayComponentHashes;
constexpr std::uint64_t kFileLimit = ElfSession::kMaxMemory + 1024u * 1024u;
[[noreturn]] void invalid(const char* message) { throw std::invalid_argument(message); }
std::string hex(std::uint64_t value, std::size_t width) {
    std::array<char, 16> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, 16);
    const std::string digits(buffer.data(), result.ptr);
    if (digits.size() > width) invalid("Manifest hash width overflow");
    return std::string(width - digits.size(), '0') + digits;
}
void validate(const SessionManifest& m) {
    if (m.memory < 4096 || m.memory > ElfSession::kMaxMemory || m.budget == 0
        || m.budget > 100000000 || m.steps == 0 || m.steps > m.budget
        || m.steps > std::numeric_limits<std::uint64_t>::max() - m.initial_retired
        || m.final_retired != m.initial_retired + m.steps || !m.replay_verified
        || m.snapshot_association == 0 || m.elf.size == 0 || m.elf.size > ElfSession::kMaxMemory
        || m.snapshot.size == 0 || m.snapshot.size > m.memory + 1024u * 1024u
        || m.log.size == 0 || m.log.size > ElfSession::kMaxMemory
        || (m.completion != ExecutionStatus::completed
            && m.completion != ExecutionStatus::instruction_limit_reached)
        || (m.completion == ExecutionStatus::instruction_limit_reached && m.steps != m.budget)) {
        invalid("Inconsistent or out-of-range manifest metadata");
    }
}
std::string body(const SessionManifest& m) {
    validate(m);
    std::string text = "winios_elf_manifest=1\n";
    const auto field = [&](const std::string& key, const std::string& value) { text += key + "=" + value + "\n"; };
    const auto identity = [&](const char* name, const SessionFileIdentity& id) {
        field(std::string(name) + "_size", std::to_string(id.size));
        field(std::string(name) + "_crc32c", hex(id.crc32c, 8));
    };
    identity("elf", m.elf); identity("snapshot", m.snapshot); identity("log", m.log);
    field("snapshot_association", hex(m.snapshot_association, 8));
    const auto hashes = [&](const char* name, const Hashes& h) {
        const std::array values{h.cpu_registers_hash, h.simd_registers_hash, h.system_registers_hash,
            h.memory_hash, h.timer_hash, h.gic_hash, h.combined_hash};
        const std::array names{"cpu", "simd", "sysreg", "memory", "timer", "gic", "combined"};
        for (std::size_t i = 0; i < names.size(); ++i) field(std::string(name) + "_" + names[i], hex(values[i], 16));
    };
    hashes("initial", m.initial); hashes("final", m.final);
    field("memory", std::to_string(m.memory)); field("budget", std::to_string(m.budget));
    field("initial_retired", std::to_string(m.initial_retired));
    field("final_retired", std::to_string(m.final_retired));
    field("steps", std::to_string(m.steps)); field("events", std::to_string(m.events));
    field("completion", execution_status_name(m.completion)); field("replay_verified", "true");
    return text;
}
std::string read_bounded(const std::filesystem::path& path, std::uint64_t limit) {
    if (!std::filesystem::is_regular_file(path)) invalid("Input is not a regular file");
    const auto size = std::filesystem::file_size(path);
    if (size == 0 || size > limit) invalid("Input exceeds size bounds");
    std::ifstream input(path, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))
        || input.peek() != std::char_traits<char>::eof()) invalid("Input changed or could not be read");
    return bytes;
}
}
SessionFileIdentity session_file_identity(const std::filesystem::path& path, std::uint64_t limit) {
    const auto bytes = read_bounded(path, limit);
    return {bytes.size(), winios::trace::calculate_crc32c(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())};
}
std::string serialize_session_manifest(const SessionManifest& m) {
    auto text = body(m);
    const auto crc = winios::trace::calculate_crc32c(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    return text + "manifest_crc32c=" + hex(crc, 8) + "\n";
}
SessionManifest parse_session_manifest(std::string_view text) {
    if (text.empty() || text.size() > 16384) invalid("Manifest size invalid");
    std::size_t offset = 0;
    const auto take = [&](const std::string& key) {
        const auto end = text.find('\n', offset);
        if (end == std::string_view::npos) invalid("Truncated manifest");
        const auto line = text.substr(offset, end - offset);
        const std::string prefix = key + "=";
        if (!line.starts_with(prefix)) invalid("Missing, duplicate, unknown or reordered manifest field");
        offset = end + 1;
        return line.substr(prefix.size());
    };
    const auto number = [&](const std::string& key, int base, std::size_t width = 0) {
        const auto value = take(key);
        std::uint64_t result{};
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, base);
        if (value.empty() || (width && value.size() != width) || parsed.ec != std::errc{}
            || parsed.ptr != value.data() + value.size()) invalid("Malformed or overflowing manifest number/hash");
        return result;
    };
    if (take("winios_elf_manifest") != "1") invalid("Unsupported manifest schema");
    SessionManifest m;
    const auto identity = [&](const char* name, SessionFileIdentity& id) {
        id.size = number(std::string(name) + "_size", 10);
        id.crc32c = static_cast<std::uint32_t>(number(std::string(name) + "_crc32c", 16, 8));
    };
    identity("elf", m.elf); identity("snapshot", m.snapshot); identity("log", m.log);
    m.snapshot_association = static_cast<std::uint32_t>(number("snapshot_association", 16, 8));
    const auto hashes = [&](const char* name, Hashes& h) {
        const std::array names{"cpu", "simd", "sysreg", "memory", "timer", "gic", "combined"};
        const std::array<std::uint64_t*, 7> values{&h.cpu_registers_hash, &h.simd_registers_hash,
            &h.system_registers_hash, &h.memory_hash, &h.timer_hash, &h.gic_hash, &h.combined_hash};
        for (std::size_t i = 0; i < names.size(); ++i) *values[i] = number(std::string(name) + "_" + names[i], 16, 16);
    };
    hashes("initial", m.initial); hashes("final", m.final);
    m.memory = number("memory", 10); m.budget = number("budget", 10);
    m.initial_retired = number("initial_retired", 10); m.final_retired = number("final_retired", 10);
    m.steps = number("steps", 10); m.events = number("events", 10);
    const auto status = take("completion");
    if (status == "completed") m.completion = ExecutionStatus::completed;
    else if (status == "instruction_limit_reached") m.completion = ExecutionStatus::instruction_limit_reached;
    else invalid("Invalid manifest completion");
    if (take("replay_verified") != "true") invalid("Manifest is not verified");
    (void)number("manifest_crc32c", 16, 8);
    if (offset != text.size() || serialize_session_manifest(m) != text) invalid("Noncanonical or corrupted manifest");
    return m;
}
SessionManifest load_session_manifest(const std::filesystem::path& path) {
    return parse_session_manifest(read_bounded(path, 16384));
}
void save_session_manifest(const std::filesystem::path& path, const SessionManifest& m) {
    const auto text = serialize_session_manifest(m);
    if (std::filesystem::exists(path)) invalid("Manifest output exists");
    ArtifactPublication publication(path);
    publication.write(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    (void)load_session_manifest(publication.temporary_path());
    publication.commit();
}
void verify_session_manifest(const SessionManifest& m, const std::filesystem::path& elf,
    const std::filesystem::path& snapshot, const std::filesystem::path& log) {
    validate(m);
    if (session_file_identity(elf, ElfSession::kMaxMemory) != m.elf
        || session_file_identity(snapshot, kFileLimit) != m.snapshot
        || session_file_identity(log, ElfSession::kMaxMemory) != m.log) invalid("Manifest artifact identity mismatch");
    winios::snapshot::SnapshotInspection info;
    if (!winios::snapshot::SnapshotManager::inspect_file(snapshot, info).success
        || info.header.header_crc32c != m.snapshot_association) invalid("Manifest snapshot association mismatch");
    winios::sndl::SndlInspectionInfo log_info;
    if (winios::sndl::SndlValidation::inspect_file(log, log_info).is_failure()
        || log_info.header.base_snapshot_hash != m.snapshot_association
        || log_info.footer.total_instructions != m.steps
        || log_info.footer.total_records != m.events) invalid("Manifest log metadata mismatch");
    RuntimeConfig limits{}; limits.memory_size = static_cast<std::size_t>(m.memory);
    limits.max_instructions = static_cast<std::size_t>(m.budget);
    ElfSession baseline(limits);
    if (!baseline.restore(snapshot).success || baseline.hashes() != m.initial
        || baseline.retired() != m.initial_retired) invalid("Manifest initial state/retirement mismatch");
    winios::replay::ReplayExecutionStats stats;
    const auto result = ElfSession::verify(snapshot, log, m.final, static_cast<std::size_t>(m.steps), stats);
    if (result.is_failure() || stats.instructions_executed != m.steps || stats.events_consumed != m.events)
        invalid("Manifest replay state or event mismatch");
    // Check actual CPU metadata/lifecycle, not merely the arithmetic prediction.
    winios::replay::ReplayConfig config;
    config.max_instructions = m.steps; config.max_replay_duration_ms = 30000;
    config.require_exact_event_consumption = true; config.divergence_report_path.clear();
    winios::sndl::ScopedThreadRecorder mute(nullptr);
    winios::replay::ReplaySession replay;
    if (replay.prepare(snapshot, log, config).is_failure() || replay.run().is_failure()
        || replay.cpu()->get_instruction_retirement_count() != m.final_retired
        || replay.cpu()->is_halted() != (m.completion == ExecutionStatus::completed)
        || replay.cpu()->is_waiting() || replay.stats().final_state_hashes != m.final)
        invalid("Manifest replay retirement/completion mismatch");
}

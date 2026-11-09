#pragma once

#include "runtime/guest_runtime.h"
#include "replay/replay_engine.h"
#include "replay/replay_state_hash.h"
#include "snapshot/snapshot_types.h"

#include <memory>

struct ElfRecordingResult {
    bool success{false};
    ExecutionResult execution{};
    winios::replay::ReplayComponentHashes final_hashes{};
    std::uint64_t records{};
    std::string error;
};

// A session is single-owner/single-threaded. Distinct sessions may run on
// different threads; their recording hooks never replace the global binding.
class ElfSession {
public:
    static constexpr std::size_t kMaxMemory = 64u * 1024u * 1024u;
    explicit ElfSession(const RuntimeConfig& config);
    ~ElfSession();
    ElfSession(const ElfSession&) = delete;
    ElfSession& operator=(const ElfSession&) = delete;

    ExecutionResult load(const std::filesystem::path& elf);
    ExecutionResult run(std::size_t instruction_budget);
    winios::snapshot::SnapshotResult capture(const std::filesystem::path& snapshot) const;
    winios::snapshot::SnapshotResult restore(const std::filesystem::path& snapshot);
    ElfRecordingResult record(const std::filesystem::path& snapshot,
                              const std::filesystem::path& log,
                              std::size_t instruction_budget);
    std::uint64_t retired() const noexcept;
    std::size_t memory_size() const noexcept { return config_.memory_size; }
    winios::replay::ReplayComponentHashes hashes() const;
    static winios::replay::ReplayResult verify(
        const std::filesystem::path& snapshot, const std::filesystem::path& log,
        const winios::replay::ReplayComponentHashes& expected,
        std::size_t instruction_budget, winios::replay::ReplayExecutionStats& stats);
private:
    struct Machine;
    RuntimeConfig config_;
    std::unique_ptr<Machine> machine_;
    std::size_t loaded_bytes_{};
    bool ready_{false};
    ExecutionResult run_impl(std::size_t budget);
};

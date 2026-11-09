#pragma once

#include "runtime/elf_session.h"
#include "replay/replay_state_hash.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct CheckpointMetadata {
    static constexpr std::uint32_t kMagic = 0x5743484bu; // "WCHK" (WinIOS Checkpoint)
    static constexpr std::uint32_t kVersion = 1;

    std::uint32_t magic{kMagic};
    std::uint32_t version{kVersion};
    std::uint64_t timestamp_ms{0};
    std::uint64_t instruction_budget{0};
    std::uint64_t retired_instructions{0};
    std::uint64_t memory_capacity{0};

    // Architectural registers
    std::uint64_t pc{0};
    std::uint64_t sp{0};
    std::uint32_t pstate{0};
    bool halted{false};
    bool waiting{false};
    std::array<std::uint64_t, 31> gpr{};

    // Subsystem state hashes
    winios::replay::ReplayComponentHashes component_hashes{};

    // Snapshot association
    std::uint32_t snapshot_crc32c{0};
    std::uint64_t snapshot_size{0};

    std::uint32_t header_crc32c{0};

    bool operator==(const CheckpointMetadata&) const = default;
};

struct ExecutionCheckpoint {
    CheckpointMetadata header{};
    std::vector<std::uint8_t> snapshot_payload{};
    std::uint32_t payload_crc32c{0};

    bool operator==(const ExecutionCheckpoint&) const = default;
};

struct DivergenceReport {
    bool has_divergence{false};
    std::string component; // "PC", "SP", "GPR[i]", "MEMORY", "TIMER", "GIC", "RETIREMENT"
    std::uint64_t expected_value{0};
    std::uint64_t actual_value{0};
    std::string details;
};

class CheckpointManager {
public:
    static constexpr std::size_t kMaxCheckpointBytes = 70u * 1024u * 1024u;

    // Capture execution checkpoint from an active ElfSession
    static ExecutionCheckpoint capture(const ElfSession& session, std::uint64_t budget);

    // Serialization & Validation
    static std::vector<std::uint8_t> serialize(const ExecutionCheckpoint& cp);
    static ExecutionCheckpoint deserialize(std::span<const std::uint8_t> bytes);
    static bool save(const std::filesystem::path& path, const ExecutionCheckpoint& cp);
    static ExecutionCheckpoint load(const std::filesystem::path& path);
    static bool validate(const ExecutionCheckpoint& cp);

    // Restores checkpoint into an ElfSession
    static bool restore(ElfSession& session, const ExecutionCheckpoint& cp);

    // Precise divergence detection
    static DivergenceReport compare_state(const ElfSession& a, const ElfSession& b);
    static DivergenceReport compare_state(const ElfSession& session, const ExecutionCheckpoint& cp);
};

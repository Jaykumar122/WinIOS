#pragma once
#include "runtime/elf_session.h"
#include <string_view>

struct SessionFileIdentity {
    std::uint64_t size{};
    std::uint32_t crc32c{};
    bool operator==(const SessionFileIdentity&) const = default;
};
struct SessionManifest {
    SessionFileIdentity elf, snapshot, log;
    winios::replay::ReplayComponentHashes initial, final;
    std::uint64_t memory{}, budget{}, initial_retired{}, final_retired{}, steps{}, events{};
    std::uint32_t snapshot_association{};
    ExecutionStatus completion{ExecutionStatus::completed};
    bool replay_verified{true};
};
SessionFileIdentity session_file_identity(const std::filesystem::path&, std::uint64_t limit);
std::string serialize_session_manifest(const SessionManifest&);
SessionManifest parse_session_manifest(std::string_view);
SessionManifest load_session_manifest(const std::filesystem::path&);
void save_session_manifest(const std::filesystem::path&, const SessionManifest&);
// Validates artifacts, restores baseline, and verifies actual replay state/metadata.
void verify_session_manifest(const SessionManifest&, const std::filesystem::path& elf,
    const std::filesystem::path& snapshot, const std::filesystem::path& log);

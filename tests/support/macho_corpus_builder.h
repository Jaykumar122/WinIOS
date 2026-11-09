#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "loader/real_artifact_harness.h"

namespace winios::corpus {

// -----------------------------------------------------------------------------
// Mach-O Corpus Artifact Record
// -----------------------------------------------------------------------------
struct MachOCorpusEntry {
    std::string                name;
    std::string                description;
    std::string                provenance;
    std::string                license;
    std::uint32_t              cputype{0x0100000Cu};    // CPU_TYPE_ARM64
    std::uint32_t              cpusubtype{0x00000000u}; // CPU_SUBTYPE_ARM64_ALL
    std::uint32_t              filetype{2};             // MH_EXECUTE
    std::uint64_t              entry_point{0x100001000ULL};
    std::vector<std::string>   dependencies;
    std::vector<std::string>   required_syscalls;
    std::vector<std::string>   required_extensions;
    std::vector<std::uint8_t>  binary_data;
    std::string                sha256;
    std::size_t                file_size{0};
    loader::AdmissionDecision  expected_decision{loader::AdmissionDecision::Admitted};
};

// -----------------------------------------------------------------------------
// MachOCorpusRegistry: Controlled Corpus of Darwin ARM64 Mach-O Binaries
// -----------------------------------------------------------------------------
class MachOCorpusRegistry {
public:
    static MachOCorpusEntry create_hello_world();
    static MachOCorpusEntry create_math_compute();
    static MachOCorpusEntry create_file_io();
    static MachOCorpusEntry create_memory_mmap();
    static MachOCorpusEntry create_mach_ipc_client();
    static MachOCorpusEntry create_dynamic_chained_fixup();
    static MachOCorpusEntry create_fairplay_encrypted_app();

    // Retrieve full catalog of test corpus binaries
    static std::vector<MachOCorpusEntry> get_all_corpus_entries();
};

} // namespace winios::corpus

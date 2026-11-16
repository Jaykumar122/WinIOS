#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::loader {

// -----------------------------------------------------------------------------
// Controlled Clean-Room Mach-O Fixture Descriptor
// -----------------------------------------------------------------------------
struct ControlledMachOFixture {
    std::string                name;
    std::string                description;
    std::string                provenance;
    std::string                license{"MIT / Clean-Room Synthetic"};
    std::uint32_t              cputype{0x0100000Cu};    // CPU_TYPE_ARM64
    std::uint32_t              cpusubtype{0x00000000u}; // CPU_SUBTYPE_ARM64_ALL
    std::uint32_t              filetype{2};             // MH_EXECUTE (or 6 for MH_DYLIB)
    std::vector<std::string>   load_commands;
    std::vector<std::string>   dependencies;
    std::vector<std::uint8_t>  binary_data;
    std::string                sha256;
    std::size_t                file_size{0};
    bool                       is_malformed{false};
    std::string                expected_behavior;
};

// -----------------------------------------------------------------------------
// PublicMachOCorpus: Clean-Room Public Mach-O Test Corpus Generator
// -----------------------------------------------------------------------------
class PublicMachOCorpus {
public:
    // Standard Valid Clean-Room Workloads
    static ControlledMachOFixture create_minimal_executable();
    static ControlledMachOFixture create_multisegment_executable();
    static ControlledMachOFixture create_multi_image_app();
    static ControlledMachOFixture create_multi_image_dylib_math();
    static ControlledMachOFixture create_multi_image_dylib_base();
    static ControlledMachOFixture create_symbol_export_dylib();
    static ControlledMachOFixture create_weak_symbol_executable();
    static ControlledMachOFixture create_chained_fixup_executable();
    static ControlledMachOFixture create_tls_executable();

    // Controlled Malformed Security Test Fixtures
    static ControlledMachOFixture create_malformed_truncated_header();
    static ControlledMachOFixture create_malformed_overlapping_segments();
    static ControlledMachOFixture create_malformed_corrupt_load_commands();

    // Complete Catalog Retrieval
    static std::vector<ControlledMachOFixture> get_full_corpus();
};

} // namespace winios::loader

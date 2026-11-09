#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "loader/userspace_macho_loader.h"

using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Valid ARM64 and ARM64e Fixtures Admission
bool test_valid_arm64_and_arm64e_admission() {
    std::cout << "[Test 1] Valid ARM64 and ARM64e Mach-O admission...\n";

    // 1. Valid ARM64 executable
    auto arm64_data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();
    UserspaceLoadResult res_arm64;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(arm64_data.data(), arm64_data.size(), res_arm64),
                "Valid ARM64 Mach-O must parse successfully");
    TEST_ASSERT(res_arm64.status == UserspaceMachOLoadStatus::Success, "Status must be Success");
    TEST_ASSERT(!res_arm64.is_arm64e, "ARM64 binary must not be marked arm64e");
    TEST_ASSERT(res_arm64.entry_point != 0, "Valid entry point must be resolved");

    // 2. Valid ARM64e executable
    auto arm64e_data = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    UserspaceLoadResult res_arm64e;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(arm64e_data.data(), arm64e_data.size(), res_arm64e),
                "Valid ARM64e Mach-O must parse successfully");
    TEST_ASSERT(res_arm64e.status == UserspaceMachOLoadStatus::Success, "Status must be Success");
    TEST_ASSERT(res_arm64e.is_arm64e, "ARM64e binary must be marked is_arm64e == true");
    TEST_ASSERT(!res_arm64e.dylinker_path.empty(), "Dylinker path must be /usr/lib/dyld");

    return true;
}

// Test 2: Truncated Header and Null Input Rejection
bool test_truncated_header_rejection() {
    std::cout << "[Test 2] Truncated header and null input rejection...\n";

    UserspaceLoadResult res;
    // Null data
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(nullptr, 100, res), "Null data must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::TruncatedHeader, "Status must be TruncatedHeader");

    // Data smaller than 32 bytes (sizeof(MachOHeader64Raw))
    std::uint8_t tiny[16] = {0};
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(tiny, sizeof(tiny), res), "Tiny data must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::TruncatedHeader, "Status must be TruncatedHeader");

    return true;
}

// Test 3: Invalid Magic Rejection (ELF, PE, Corrupted)
bool test_invalid_magic_rejection() {
    std::cout << "[Test 3] Invalid magic rejection (ELF, PE, Corrupted)...\n";

    auto data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();

    // 1. Corrupted magic 0xDEADBEEF
    data[0] = 0xEF; data[1] = 0xBE; data[2] = 0xAD; data[3] = 0xDE;
    UserspaceLoadResult res;
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "Bad magic must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::InvalidMagic, "Status must be InvalidMagic");

    // 2. Linux ELF magic (\x7fELF)
    data[0] = 0x7F; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "ELF magic must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::InvalidMagic, "Status must be InvalidMagic");

    return true;
}

// Test 4: Unsupported CPU Type and Subtype Rejection
bool test_unsupported_cpu_rejection() {
    std::cout << "[Test 4] Unsupported CPU type and subtype rejection...\n";

    auto data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();

    // Change cputype to x86_64 (0x01000007)
    // cputype is at offset 4
    *reinterpret_cast<std::uint32_t*>(data.data() + 4) = 0x01000007u;
    UserspaceLoadResult res;
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "x86_64 CPU must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::UnsupportedCpuType, "Status must be UnsupportedCpuType");

    // Restore cputype to ARM64, modify subtype to unsupported (0x999)
    *reinterpret_cast<std::uint32_t*>(data.data() + 4) = kMachOCpuTypeARM64;
    *reinterpret_cast<std::uint32_t*>(data.data() + 8) = 0x999u;
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "Unsupported subtype must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::UnsupportedCpuSubtype, "Status must be UnsupportedCpuSubtype");

    return true;
}

// Test 5: Unsupported File Type Rejection
bool test_unsupported_file_type_rejection() {
    std::cout << "[Test 5] Unsupported Mach-O file type rejection...\n";

    auto data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();

    // Change filetype (offset 12) to MH_OBJECT (1) or MH_CORE (4)
    *reinterpret_cast<std::uint32_t*>(data.data() + 12) = 0x00000001u; // MH_OBJECT
    UserspaceLoadResult res;
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "MH_OBJECT must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::UnsupportedFileType, "Status must be UnsupportedFileType");

    *reinterpret_cast<std::uint32_t*>(data.data() + 12) = 0x00000004u; // MH_CORE
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "MH_CORE must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::UnsupportedFileType, "Status must be UnsupportedFileType");

    return true;
}

// Test 6: Corrupted Load Commands Bounds and Structure
bool test_corrupted_load_commands() {
    std::cout << "[Test 6] Corrupted load commands bounds and structure rejection...\n";

    auto data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();

    // 1. sizeofcmds exceeds file size: set sizeofcmds (offset 20) to 0x100000
    *reinterpret_cast<std::uint32_t*>(data.data() + 20) = 0x100000u;
    UserspaceLoadResult res;
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "Excessive sizeofcmds must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::CorruptedLoadCommands, "Status must be CorruptedLoadCommands");

    // 2. Corrupt first load command's cmdsize to 2 (smaller than 8 bytes minimum)
    data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();
    // first command cmdsize is at offset 32 + 4 = 36
    *reinterpret_cast<std::uint32_t*>(data.data() + 36) = 2u;
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "cmdsize < 8 must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::CorruptedLoadCommands, "Status must be CorruptedLoadCommands");

    // 3. Corrupt first command cmdsize to unaligned value (13)
    *reinterpret_cast<std::uint32_t*>(data.data() + 36) = 13u;
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "Unaligned cmdsize must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::CorruptedLoadCommands, "Status must be CorruptedLoadCommands");

    return true;
}

// Test 7: Adversarial Segment Bounds, Overflow, and Overlap Validation
bool test_segment_bounds_overflow_overlap() {
    std::cout << "[Test 7] Adversarial segment bounds, overflow, and overlap validation...\n";

    std::string err;

    // 1. Virtual memory range integer overflow
    std::vector<UserspaceSegment> segs;
    UserspaceSegment s1;
    s1.name = "__TEXT";
    s1.vmaddr = 0xFFFFFFFFFFFF0000ULL;
    s1.vmsize = 0x20000ULL; // vmaddr + vmsize overflows 64-bit space
    s1.fileoff = 0;
    s1.filesize = 0x1000;
    segs.push_back(s1);

    auto status = UserspaceMachOLoader::validate_segments(segs, 0x10000, 0x4000, err);
    TEST_ASSERT(status == UserspaceMachOLoadStatus::IntegerOverflow, "Virtual address overflow must be rejected");

    // 2. File offset integer overflow
    segs.clear();
    s1.vmaddr = 0x100000000ULL;
    s1.vmsize = 0x4000;
    s1.fileoff = 0xFFFFFFFFFFFF0000ULL;
    s1.filesize = 0x20000; // fileoff + filesize overflows
    segs.push_back(s1);

    status = UserspaceMachOLoader::validate_segments(segs, 0x10000, 0x4000, err);
    TEST_ASSERT(status == UserspaceMachOLoadStatus::IntegerOverflow, "File offset overflow must be rejected");

    // 3. Segment payload exceeds file size
    segs.clear();
    s1.fileoff = 0x1000;
    s1.filesize = 0x5000; // 0x6000 > file_size 0x4000
    segs.push_back(s1);

    status = UserspaceMachOLoader::validate_segments(segs, 0x4000, 0x4000, err);
    TEST_ASSERT(status == UserspaceMachOLoadStatus::SegmentOutOfBounds, "Segment beyond file bounds must be rejected");

    // 4. Misaligned segment on 16 KB page boundary
    segs.clear();
    s1.fileoff = 0;
    s1.filesize = 0x1000;
    s1.vmaddr = 0x100001000ULL; // Not aligned to 16 KB (0x4000)
    s1.vmsize = 0x4000;
    segs.push_back(s1);

    status = UserspaceMachOLoader::validate_segments(segs, 0x10000, 0x4000, err);
    TEST_ASSERT(status == UserspaceMachOLoadStatus::MisalignedSegment, "Misaligned segment must be rejected");

    // 5. Overlapping segments
    segs.clear();
    s1.vmaddr = 0x100000000ULL;
    s1.vmsize = 0x8000;
    segs.push_back(s1);

    UserspaceSegment s2;
    s2.name = "__DATA";
    s2.vmaddr = 0x100004000ULL; // Overlaps s1 [0x100000000, 0x100008000)
    s2.vmsize = 0x4000;
    segs.push_back(s2);

    status = UserspaceMachOLoader::validate_segments(segs, 0x10000, 0x4000, err);
    TEST_ASSERT(status == UserspaceMachOLoadStatus::OverlappingSegments, "Overlapping segments must be rejected");

    return true;
}

// Test 8: Missing Entry Point Rejection
bool test_missing_entry_point_rejection() {
    std::cout << "[Test 8] Missing entry point rejection in MH_EXECUTE...\n";

    // Create binary without LC_MAIN and with non-executable segments
    auto data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();

    // Overwrite LC_MAIN command (cmd = kCmdMain = 0x80000028) with LC_SEGMENT_64 of size 0
    UserspaceLoadResult res;
    // Corrupt entryoff in LC_MAIN
    // In our fixture builder, find LC_MAIN and set cmd to 0
    for (std::size_t i = 32; i + 8 <= data.size(); i += 4) {
        std::uint32_t cmd = *reinterpret_cast<const std::uint32_t*>(data.data() + i);
        if (cmd == kCmdMain) {
            *reinterpret_cast<std::uint32_t*>(data.data() + i) = 0x77777777u; // Unknown command
            break;
        }
    }

    // Also strip execute permissions on segments
    for (std::size_t i = 32; i + sizeof(MachOSegment64Raw) <= data.size(); i += 4) {
        auto* seg = reinterpret_cast<MachOSegment64Raw*>(data.data() + i);
        if (seg->cmd == kCmdSegment64) {
            seg->initprot &= ~kProtExecute;
            seg->maxprot &= ~kProtExecute;
        }
    }

    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "Missing entry point must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::MissingEntryPoint, "Status must be MissingEntryPoint");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 147: MACH-O ADMISSION & HARDENING TEST SUITE       \n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*fn)(), const char* name) {
        if (fn()) {
            std::cout << "  -> PASS: " << name << "\n\n";
            passed++;
        } else {
            std::cout << "  -> FAIL: " << name << "\n\n";
            failed++;
        }
    };

    run_test(test_valid_arm64_and_arm64e_admission, "Valid ARM64 and ARM64e Fixture Admission");
    run_test(test_truncated_header_rejection, "Truncated Header & Null Input Rejection");
    run_test(test_invalid_magic_rejection, "Invalid Magic Rejection (ELF, PE, Corrupted)");
    run_test(test_unsupported_cpu_rejection, "Unsupported CPU Type and Subtype Rejection");
    run_test(test_unsupported_file_type_rejection, "Unsupported Mach-O File Type Rejection");
    run_test(test_corrupted_load_commands, "Corrupted Load Commands Bounds and Structure");
    run_test(test_segment_bounds_overflow_overlap, "Adversarial Segment Bounds, Overflow, Overlap");
    run_test(test_missing_entry_point_rejection, "Missing Entry Point Rejection in MH_EXECUTE");

    std::cout << "=================================================================\n";
    std::cout << "STAGE 147 RESULTS: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

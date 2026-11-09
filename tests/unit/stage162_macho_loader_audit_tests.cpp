#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"

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

// Test 1: Mach-O 64-Bit Header Field Validation
bool test_macho_header_field_validation() {
    std::cout << "[Test 1] Mach-O 64-Bit Header Field Validation...\n";

    // 1. Valid ARM64 executable
    auto arm64_data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();
    UserspaceLoadResult res1;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(arm64_data.data(), arm64_data.size(), res1),
                "ARM64 metadata parsing must succeed");
    TEST_ASSERT(res1.status == UserspaceMachOLoadStatus::Success, "Status must be Success");
    TEST_ASSERT(!res1.is_arm64e, "ARM64 binary must have is_arm64e == false");
    TEST_ASSERT(res1.header.filetype == kMachOFileTypeExecute, "filetype must be MH_EXECUTE");

    // 2. Valid ARM64e executable
    auto arm64e_data = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    UserspaceLoadResult res2;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(arm64e_data.data(), arm64e_data.size(), res2),
                "ARM64e metadata parsing must succeed");
    TEST_ASSERT(res2.is_arm64e, "ARM64e binary must have is_arm64e == true");
    TEST_ASSERT(res2.header.filetype == kMachOFileTypeExecute, "filetype must be MH_EXECUTE");

    return true;
}

// Test 2: Mach-O Segment Commands Audit (__PAGEZERO, __TEXT, __DATA)
bool test_macho_segment_commands_audit() {
    std::cout << "[Test 2] Mach-O Segment Commands Audit (__PAGEZERO, __TEXT, __DATA)...\n";

    auto data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();
    UserspaceLoadResult res;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "Parsing must succeed");

    bool has_pagezero = false;
    bool has_text = false;
    bool has_data = false;

    for (const auto& seg : res.segments) {
        if (seg.name == "__PAGEZERO") {
            has_pagezero = true;
            TEST_ASSERT(seg.is_pagezero, "__PAGEZERO must be flagged is_pagezero == true");
            TEST_ASSERT(seg.initprot == kProtNone, "__PAGEZERO initprot must be PROT_NONE");
            TEST_ASSERT(seg.maxprot == kProtNone, "__PAGEZERO maxprot must be PROT_NONE");
        } else if (seg.name == "__TEXT") {
            has_text = true;
            TEST_ASSERT(!seg.is_pagezero, "__TEXT must not be pagezero");
            TEST_ASSERT((seg.initprot & kProtExecute) != 0, "__TEXT must have execute permission");
            TEST_ASSERT((seg.initprot & kProtWrite) == 0, "__TEXT must not have write permission under W^X");
        } else if (seg.name == "__DATA") {
            has_data = true;
            TEST_ASSERT((seg.initprot & kProtWrite) != 0, "__DATA must have write permission");
            TEST_ASSERT((seg.initprot & kProtExecute) == 0, "__DATA must not have execute permission");
        }
    }

    TEST_ASSERT(has_pagezero, "Must contain __PAGEZERO segment");
    TEST_ASSERT(has_text, "Must contain __TEXT segment");
    TEST_ASSERT(has_data, "Must contain __DATA segment");

    return true;
}

// Test 3: Adversarial Load Command Bounds Rejection
bool test_macho_adversarial_load_command_bounds() {
    std::cout << "[Test 3] Adversarial Load Command Bounds Rejection...\n";

    auto valid_data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();

    // 1. Corrupted cmdsize < 8 (minimum load command size)
    auto bad_cmdsize = valid_data;
    // Offset of first load command = sizeof(MachOHeader64Raw) = 32
    bad_cmdsize[36] = 4; bad_cmdsize[37] = 0; bad_cmdsize[38] = 0; bad_cmdsize[39] = 0; // cmdsize = 4
    UserspaceLoadResult res1;
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(bad_cmdsize.data(), bad_cmdsize.size(), res1),
                "cmdsize < 8 must be rejected");
    TEST_ASSERT(res1.status == UserspaceMachOLoadStatus::CorruptedLoadCommands,
                "Status must be CorruptedLoadCommands");

    // 2. cmdsize extending past sizeofcmds
    auto giant_cmd = valid_data;
    giant_cmd[36] = 0x00; giant_cmd[37] = 0x80; giant_cmd[38] = 0x00; giant_cmd[39] = 0x00; // cmdsize = 32KB
    UserspaceLoadResult res2;
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(giant_cmd.data(), giant_cmd.size(), res2),
                "cmdsize exceeding sizeofcmds must be rejected");

    return true;
}

// Test 4: Adversarial Segment File Offsets Exceeding File Size
bool test_macho_adversarial_segment_offsets() {
    std::cout << "[Test 4] Adversarial Segment File Offsets Exceeding File Size...\n";

    auto data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();

    // In first segment command (__PAGEZERO is first, __TEXT is second at offset 32 + 72 = 104)
    // Offset 104: cmd=0x19, cmdsize=72, segname[16], vmaddr(8), vmsize(8), fileoff(8), filesize(8)
    // fileoff is at 104 + 8 + 16 + 8 + 8 = 144
    // filesize is at 144 + 8 = 152
    // Set filesize to 0x7FFFFFFF (exceeds data.size())
    data[152] = 0xFF; data[153] = 0xFF; data[154] = 0x7F; data[155] = 0x00;

    UserspaceLoadResult res;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res),
                "parse_metadata should read segment headers");

    std::string val_err;
    auto val_status = UserspaceMachOLoader::validate_segments(res.segments, data.size(), kApplePageSize16K, val_err);
    TEST_ASSERT(val_status == UserspaceMachOLoadStatus::SegmentOutOfBounds,
                "Segment exceeding file bounds must return SegmentOutOfBounds");

    // Also verify load_into_memory rejects the binary
    UserspaceLoadConfig cfg{};
    Memory mem(1024 * 1024);
    auto load_res = UserspaceMachOLoader::load_into_memory(data.data(), data.size(), mem, cfg);
    TEST_ASSERT(!load_res.is_success(), "load_into_memory must fail on out-of-bounds segment");
    TEST_ASSERT(load_res.status == UserspaceMachOLoadStatus::SegmentOutOfBounds,
                "load_into_memory status must be SegmentOutOfBounds");

    return true;
}

// Test 5: Entry Point Discovery (LC_MAIN vs LC_UNIXTHREAD)
bool test_macho_entry_point_discovery() {
    std::cout << "[Test 5] Entry Point Discovery (LC_MAIN & LC_UNIXTHREAD)...\n";

    auto data = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();
    UserspaceLoadResult res;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "Parsing must succeed");

    TEST_ASSERT(res.entry_point != 0, "Valid non-zero entry point must be resolved");
    // Standard fixture base: 0x100000000
    TEST_ASSERT(res.entry_point >= 0x100000000ULL, "Entry point must be within __TEXT address range");

    return true;
}

// Test 6: Dylinker and Dependent Library Discovery
bool test_macho_dylinker_and_dylib_dependencies() {
    std::cout << "[Test 6] Dylinker and Dependent Library Discovery...\n";

    auto data = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib", "/System/Library/Frameworks/Foundation.framework/Foundation"});

    UserspaceLoadResult res;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(data.data(), data.size(), res), "Parsing must succeed");

    TEST_ASSERT(res.dylinker_path == "/usr/lib/dyld", "Dylinker path must be /usr/lib/dyld");
    TEST_ASSERT(res.dylibs.size() >= 2, "Must identify at least 2 dependent dylibs");
    TEST_ASSERT(res.dylibs[0].path == "/usr/lib/libSystem.B.dylib",
                "First dependent dylib must match libSystem");

    return true;
}

// Test 7: Corrupted Magic and Input Rejection
bool test_macho_corrupted_magic_rejection() {
    std::cout << "[Test 7] Corrupted Magic and Null Input Rejection...\n";

    UserspaceLoadResult res;

    // 1. Null data
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(nullptr, 1024, res), "Null buffer must fail");

    // 2. Truncated buffer (< 32 bytes)
    std::uint8_t mini[20] = {0};
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(mini, sizeof(mini), res), "Short buffer must fail");

    // 3. Invalid magic (e.g. Windows PE 'MZ')
    std::uint8_t pe[64] = {'M', 'Z', 0};
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(pe, sizeof(pe), res), "PE magic must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::InvalidMagic, "Status must be InvalidMagic");

    // 4. Linux ELF
    std::uint8_t elf[64] = {0x7F, 'E', 'L', 'F'};
    TEST_ASSERT(!UserspaceMachOLoader::parse_metadata(elf, sizeof(elf), res), "ELF magic must fail");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::InvalidMagic, "Status must be InvalidMagic");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 162: Mach-O Loader Independent Audit\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_macho_header_field_validation, "Mach-O 64-Bit Header Field Validation");
    run_test(test_macho_segment_commands_audit, "Mach-O Segment Commands Audit (__PAGEZERO, __TEXT, __DATA)");
    run_test(test_macho_adversarial_load_command_bounds, "Adversarial Load Command Bounds Rejection");
    run_test(test_macho_adversarial_segment_offsets, "Adversarial Segment File Offsets Rejection");
    run_test(test_macho_entry_point_discovery, "Entry Point Discovery (LC_MAIN & LC_UNIXTHREAD)");
    run_test(test_macho_dylinker_and_dylib_dependencies, "Dylinker & Dependent Library Discovery");
    run_test(test_macho_corrupted_magic_rejection, "Corrupted Magic & Null Input Rejection");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

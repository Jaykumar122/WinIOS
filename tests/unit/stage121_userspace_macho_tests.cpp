#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
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

bool test_valid_arm64e_metadata() {
    std::cout << "[Test 1] Valid ARM64e Metadata Parsing & Verification...\n";

    auto bytes = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"});
    TEST_ASSERT(!bytes.empty(), "Fixture generator must produce non-empty buffer");

    UserspaceLoadResult res;
    bool ok = UserspaceMachOLoader::parse_metadata(bytes.data(), bytes.size(), res);
    TEST_ASSERT(ok, "parse_metadata must succeed for valid ARM64e binary");
    TEST_ASSERT(res.is_success(), "Result status must be Success");
    TEST_ASSERT(res.is_arm64e, "Binary must be detected as ARM64e");
    TEST_ASSERT(res.is_pie, "Binary must have MH_PIE flag");
    TEST_ASSERT(res.header.magic == kMachO64Magic, "Header magic must match MH_MAGIC_64");
    TEST_ASSERT(res.header.cputype == kMachOCpuTypeARM64, "CPU type must be ARM64");

    // Segments inspection
    const auto* pzero = res.find_segment("__PAGEZERO");
    TEST_ASSERT(pzero != nullptr, "__PAGEZERO must be present");
    TEST_ASSERT(pzero->is_pagezero, "__PAGEZERO flag must be set");
    TEST_ASSERT(pzero->vmaddr == 0, "__PAGEZERO vmaddr must be 0");
    TEST_ASSERT(pzero->vmsize == 0x100000000ULL, "__PAGEZERO vmsize must be 4GB");

    const auto* text = res.find_segment("__TEXT");
    TEST_ASSERT(text != nullptr, "__TEXT must be present");
    TEST_ASSERT(text->is_readable(), "__TEXT must be readable");
    TEST_ASSERT(text->is_executable(), "__TEXT must be executable");
    TEST_ASSERT(!text->is_writable(), "__TEXT must not be writable");

    const auto* data = res.find_segment("__DATA");
    TEST_ASSERT(data != nullptr, "__DATA must be present");
    TEST_ASSERT(data->is_readable(), "__DATA must be readable");
    TEST_ASSERT(data->is_writable(), "__DATA must be writable");

    const auto* linkedit = res.find_segment("__LINKEDIT");
    TEST_ASSERT(linkedit != nullptr, "__LINKEDIT must be present");

    // Dynamic Linking & Versioning
    TEST_ASSERT(res.dylinker_path == "/usr/lib/dyld", "Dylinker path must match /usr/lib/dyld");
    TEST_ASSERT(!res.dylibs.empty(), "Dylib list must not be empty");
    TEST_ASSERT(res.dylibs[0].path == "/usr/lib/libSystem.B.dylib", "Dylib dependency must match");
    TEST_ASSERT(res.min_os_version == "17.0.0", "Target OS must be 17.0.0");
    TEST_ASSERT(res.has_code_signature, "Code signature command must be present");
    TEST_ASSERT(!res.sha256_hex.empty(), "SHA256 must be computed");

    std::cout << "  Passed (Entry Point: 0x" << std::hex << res.entry_point << std::dec << ")\n";
    return true;
}

bool test_valid_arm64_metadata() {
    std::cout << "[Test 2] Valid ARM64 (Non-PAC) Metadata Verification...\n";

    auto bytes = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();
    UserspaceLoadResult res;
    bool ok = UserspaceMachOLoader::parse_metadata(bytes.data(), bytes.size(), res);
    TEST_ASSERT(ok, "parse_metadata must succeed for valid ARM64 binary");
    TEST_ASSERT(res.is_success(), "Result status must be Success");
    TEST_ASSERT(!res.is_arm64e, "Binary must not be flagged as ARM64e");
    TEST_ASSERT(res.header.cputype == kMachOCpuTypeARM64, "CPU type must be ARM64");

    std::cout << "  Passed (ARM64 All Subtype Accepted)\n";
    return true;
}

bool test_invalid_magic_rejection() {
    std::cout << "[Test 3] Invalid Magic and Cigam Rejection...\n";

    auto bytes = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(bytes.data());

    // Corrupt magic to arbitrary value
    hdr->magic = 0xDEADBEEFu;
    UserspaceLoadResult res1;
    bool ok1 = UserspaceMachOLoader::parse_metadata(bytes.data(), bytes.size(), res1);
    TEST_ASSERT(!ok1, "parse_metadata must fail for corrupted magic");
    TEST_ASSERT(res1.status == UserspaceMachOLoadStatus::InvalidMagic, "Status must be InvalidMagic");

    // Corrupt magic to opposite endianness (MH_CIGAM_64)
    hdr->magic = kMachO64Cigam;
    UserspaceLoadResult res2;
    bool ok2 = UserspaceMachOLoader::parse_metadata(bytes.data(), bytes.size(), res2);
    TEST_ASSERT(!ok2, "parse_metadata must fail for big-endian MH_CIGAM_64");
    TEST_ASSERT(res2.status == UserspaceMachOLoadStatus::InvalidMagic, "Status must be InvalidMagic");

    std::cout << "  Passed (Invalid magics strictly rejected)\n";
    return true;
}

bool test_unsupported_cpu_and_subtype() {
    std::cout << "[Test 4] Unsupported CPU Architecture and Subtype Rejection...\n";

    auto bytes = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(bytes.data());

    // Set CPU to x86_64 (0x01000007)
    hdr->cputype = 0x01000007u;
    UserspaceLoadResult res1;
    bool ok1 = UserspaceMachOLoader::parse_metadata(bytes.data(), bytes.size(), res1);
    TEST_ASSERT(!ok1, "parse_metadata must fail for non-ARM64");
    TEST_ASSERT(res1.status == UserspaceMachOLoadStatus::UnsupportedCpuType, "Status must be UnsupportedCpuType");

    // Restore CPU, corrupt subtype to invalid
    hdr->cputype = kMachOCpuTypeARM64;
    hdr->cpusubtype = 0x00000099u;
    UserspaceLoadResult res2;
    bool ok2 = UserspaceMachOLoader::parse_metadata(bytes.data(), bytes.size(), res2);
    TEST_ASSERT(!ok2, "parse_metadata must fail for unsupported subtype");
    TEST_ASSERT(res2.status == UserspaceMachOLoadStatus::UnsupportedCpuSubtype, "Status must be UnsupportedCpuSubtype");

    std::cout << "  Passed (Non-ARM64 architectures blocked)\n";
    return true;
}

bool test_truncated_load_commands() {
    std::cout << "[Test 5] Truncated Load Commands Rejection...\n";

    auto bytes = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(bytes.data());

    // Claim sizeofcmds extends beyond file size
    hdr->sizeofcmds = static_cast<std::uint32_t>(bytes.size() * 2);
    UserspaceLoadResult res;
    bool ok = UserspaceMachOLoader::parse_metadata(bytes.data(), bytes.size(), res);
    TEST_ASSERT(!ok, "parse_metadata must fail when sizeofcmds exceeds file bounds");
    TEST_ASSERT(res.status == UserspaceMachOLoadStatus::CorruptedLoadCommands, "Status must be CorruptedLoadCommands");

    std::cout << "  Passed (Corrupted command sizes detected)\n";
    return true;
}

bool test_segment_integer_overflow_rejection() {
    std::cout << "[Test 6] Segment Address Integer Overflow Rejection...\n";

    std::vector<UserspaceSegment> segs;
    UserspaceSegment seg1;
    seg1.name = "__BAD_OVERFLOW";
    seg1.vmaddr = 0xFFFFFFFFFFFFF000ULL;
    seg1.vmsize = 0x4000ULL; // Overflow
    seg1.fileoff = 0;
    seg1.filesize = 0x1000;
    seg1.initprot = kProtRead;
    segs.push_back(seg1);

    std::string err;
    auto status = UserspaceMachOLoader::validate_segments(segs, 0x10000, kApplePageSize16K, err);
    TEST_ASSERT(status == UserspaceMachOLoadStatus::IntegerOverflow, "Must return IntegerOverflow");

    std::cout << "  Passed (Address space wrap-around rejected)\n";
    return true;
}

bool test_segment_out_of_bounds_rejection() {
    std::cout << "[Test 7] Segment File Payload Out-Of-Bounds Rejection...\n";

    std::vector<UserspaceSegment> segs;
    UserspaceSegment seg1;
    seg1.name = "__OVERRUN";
    seg1.vmaddr = 0x100000000ULL;
    seg1.vmsize = 0x4000ULL;
    seg1.fileoff = 0x4000ULL;
    seg1.filesize = 0x10000ULL; // Exceeds file_size
    seg1.initprot = kProtRead | kProtExecute;
    segs.push_back(seg1);

    std::string err;
    auto status = UserspaceMachOLoader::validate_segments(segs, 0x8000, kApplePageSize16K, err);
    TEST_ASSERT(status == UserspaceMachOLoadStatus::SegmentOutOfBounds, "Must return SegmentOutOfBounds");

    std::cout << "  Passed (File payload overruns rejected)\n";
    return true;
}

bool test_segment_misalignment_rejection() {
    std::cout << "[Test 8] Segment Granule Misalignment Rejection...\n";

    std::vector<UserspaceSegment> segs;
    UserspaceSegment seg1;
    seg1.name = "__MISALIGNED";
    seg1.vmaddr = 0x100001000ULL; // Not aligned to 16 KB (0x4000)
    seg1.vmsize = 0x4000ULL;
    seg1.fileoff = 0;
    seg1.filesize = 0x4000ULL;
    seg1.initprot = kProtRead | kProtExecute;
    segs.push_back(seg1);

    std::string err;
    auto status = UserspaceMachOLoader::validate_segments(segs, 0x10000, kApplePageSize16K, err);
    TEST_ASSERT(status == UserspaceMachOLoadStatus::MisalignedSegment, "Must return MisalignedSegment");

    std::cout << "  Passed (16 KB page misalignment rejected)\n";
    return true;
}

bool test_overlapping_segments_rejection() {
    std::cout << "[Test 9] Overlapping Segments Rejection...\n";

    std::vector<UserspaceSegment> segs;
    UserspaceSegment seg1;
    seg1.name = "__TEXT";
    seg1.vmaddr = 0x100000000ULL;
    seg1.vmsize = 0x8000ULL; // [0x100000000, 0x100008000)
    seg1.fileoff = 0;
    seg1.filesize = 0x4000ULL;
    seg1.initprot = kProtRead | kProtExecute;
    segs.push_back(seg1);

    UserspaceSegment seg2;
    seg2.name = "__DATA_COLLIDE";
    seg2.vmaddr = 0x100004000ULL; // Overlaps with seg1!
    seg2.vmsize = 0x8000ULL;
    seg2.fileoff = 0x4000ULL;
    seg2.filesize = 0x4000ULL;
    seg2.initprot = kProtRead | kProtWrite;
    segs.push_back(seg2);

    std::string err;
    auto status = UserspaceMachOLoader::validate_segments(segs, 0x20000, kApplePageSize16K, err);
    TEST_ASSERT(status == UserspaceMachOLoadStatus::OverlappingSegments, "Must return OverlappingSegments");

    std::cout << "  Passed (Virtual segment overlaps rejected)\n";
    return true;
}

bool test_load_into_memory_and_stack_abi() {
    std::cout << "[Test 10] Memory Loading & ARM64 Darwin Initial Stack ABI...\n";

    auto bytes = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    Memory mem(0x200000); // 2 MB guest RAM

    UserspaceLoadConfig cfg;
    cfg.aslr_enabled = true;
    cfg.aslr_slide = 0x10000ULL; // 64 KB slide
    cfg.stack_top = 0x1E0000ULL; // Within memory buffer for direct inspection
    cfg.page_size = kApplePageSize16K;
    cfg.argv = {"/sbin/launchd", "--verbose", "-s"};
    cfg.envp = {"PATH=/usr/bin:/bin:/usr/sbin:/sbin", "HOME=/", "SHELL=/bin/sh"};
    cfg.executable_path = "/sbin/launchd";

    UserspaceLoadResult res = UserspaceMachOLoader::load_into_memory(
        bytes.data(), bytes.size(), mem, cfg);

    TEST_ASSERT(res.is_success(), "load_into_memory must succeed");
    TEST_ASSERT(res.aslr_slide == 0x10000ULL, "ASLR slide must be recorded");
    TEST_ASSERT(res.entry_point == 0x100001000ULL + 0x10000ULL, "Entry point must be slid");

    // Verify ARM64 code was written to memory at entry offset
    // Entry offset in TEXT is 0x1000.
    std::uint32_t instr1 = mem.read_u32(0x1000);
    std::uint32_t instr2 = mem.read_u32(0x1004);
    TEST_ASSERT(instr1 == 0xD2800000u, "First instruction must be MOV X0, #0");
    TEST_ASSERT(instr2 == 0xD65F03C0u, "Second instruction must be RET");

    // Verify Stack ABI
    std::uint64_t sp = res.initial_sp;
    TEST_ASSERT(sp != 0, "Initial SP must be non-zero");
    TEST_ASSERT(sp < cfg.stack_top, "SP must be below stack top");
    TEST_ASSERT((sp & 0xFULL) == 0, "SP must be 16-byte aligned for ARM64");

    // Read argc at SP
    std::uint64_t argc = mem.read_u64(static_cast<std::size_t>(sp));
    TEST_ASSERT(argc == 3, "argc must be 3 (/sbin/launchd, --verbose, -s)");

    // Read argv pointers
    std::uint64_t argv0_ptr = mem.read_u64(static_cast<std::size_t>(sp + 8));
    std::uint64_t argv1_ptr = mem.read_u64(static_cast<std::size_t>(sp + 16));
    std::uint64_t argv2_ptr = mem.read_u64(static_cast<std::size_t>(sp + 24));
    std::uint64_t argv_null = mem.read_u64(static_cast<std::size_t>(sp + 32));

    TEST_ASSERT(argv0_ptr > sp, "argv[0] must point above pointer table");
    TEST_ASSERT(argv1_ptr > sp, "argv[1] must point above pointer table");
    TEST_ASSERT(argv2_ptr > sp, "argv[2] must point above pointer table");
    TEST_ASSERT(argv_null == 0, "argv must be NULL terminated");

    // Read string values
    auto read_str = [&](std::uint64_t ptr) -> std::string {
        std::string s;
        std::size_t off = static_cast<std::size_t>(ptr);
        while (off < mem.size()) {
            char c = static_cast<char>(mem.read_u8(off++));
            if (c == '\0') break;
            s.push_back(c);
        }
        return s;
    };

    TEST_ASSERT(read_str(argv0_ptr) == "/sbin/launchd", "argv[0] string match");
    TEST_ASSERT(read_str(argv1_ptr) == "--verbose", "argv[1] string match");
    TEST_ASSERT(read_str(argv2_ptr) == "-s", "argv[2] string match");

    // Read envp pointers
    std::uint64_t envp0_ptr = mem.read_u64(static_cast<std::size_t>(sp + 40));
    std::uint64_t envp1_ptr = mem.read_u64(static_cast<std::size_t>(sp + 48));
    std::uint64_t envp2_ptr = mem.read_u64(static_cast<std::size_t>(sp + 56));
    std::uint64_t envp_null = mem.read_u64(static_cast<std::size_t>(sp + 64));

    TEST_ASSERT(read_str(envp0_ptr) == "PATH=/usr/bin:/bin:/usr/sbin:/sbin", "envp[0] string match");
    TEST_ASSERT(read_str(envp1_ptr) == "HOME=/", "envp[1] string match");
    TEST_ASSERT(read_str(envp2_ptr) == "SHELL=/bin/sh", "envp[2] string match");
    TEST_ASSERT(envp_null == 0, "envp must be NULL terminated");

    // Read apple vector (executable path)
    std::uint64_t apple0_ptr = mem.read_u64(static_cast<std::size_t>(sp + 72));
    std::uint64_t apple_null = mem.read_u64(static_cast<std::size_t>(sp + 80));

    TEST_ASSERT(read_str(apple0_ptr) == "/sbin/launchd", "apple[0] must be executable path");
    TEST_ASSERT(apple_null == 0, "apple vector must be NULL terminated");

    std::cout << "  Passed (16-byte aligned SP: 0x" << std::hex << sp << std::dec 
              << ", verified argv, envp, and apple vectors)\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 121: USERSPACE MACH-O LOADER & STACK ABI TESTS    \n";
    std::cout << "  Platform: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e        \n";
    std::cout << "  Target: Darwin Userspace /sbin/launchd Binary Handoff          \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_valid_arm64e_metadata()) passed++;
    if (test_valid_arm64_metadata()) passed++;
    if (test_invalid_magic_rejection()) passed++;
    if (test_unsupported_cpu_and_subtype()) passed++;
    if (test_truncated_load_commands()) passed++;
    if (test_segment_integer_overflow_rejection()) passed++;
    if (test_segment_out_of_bounds_rejection()) passed++;
    if (test_segment_misalignment_rejection()) passed++;
    if (test_overlapping_segments_rejection()) passed++;
    if (test_load_into_memory_and_stack_abi()) passed++;

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "Stage 121 Tests Complete: " << passed << " / " << total << " passed\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}

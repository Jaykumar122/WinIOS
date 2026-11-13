// =============================================================================
// stage_192_2_macho_hardening_tests.cpp
//
// Campaign 192 Stage 192.2:
//   Mach-O Parser Security & Robustness Verification
//
//   Audits and validates MachOLoader hardening against:
//     1. Truncated headers, tiny buffers, and malformed fat binaries
//     2. Oversized load command counts (ncmds=0, ncmds > 4096)
//     3. Misaligned load command sizes (cmdsize % 4 != 0)
//     4. Load command bounds violations and cumulative size mismatches
//     5. 64-bit integer overflow in segment vmaddr + vmsize
//     6. 64-bit integer overflow in segment fileoff + filesize
//     7. Segment filesize exceeding virtual size (filesize > vmsize)
//     8. Virtual memory segment overlap detection
//     9. Invalid entry point pointing outside executable segments
//    10. Invalid entry point pointing inside non-executable segment (__DATA)
//    11. Executable file without any valid entry point or executable segment
//    12. Destination memory bounds and address wrap protection
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "loader/guest_memory_map.h"
#include "loader/macho_loader.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;

namespace {

int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ \
                      << ": " << #cond << "\n"; \
            ++g_failed; \
        } \
    } while (0)

#define CHECK_MSG(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ \
                      << ": " << (msg) << " (" #cond ")\n"; \
            ++g_failed; \
        } \
    } while (0)

// Helper to construct a base valid synthetic ARM64 Mach-O buffer
std::vector<std::uint8_t> make_valid_macho(
    std::uint64_t text_vmaddr = 0x100000000ULL,
    std::uint64_t text_vmsize = 0x4000ULL,
    std::uint64_t text_fileoff = 0x1000ULL,
    std::uint64_t text_filesize = 0x1000ULL,
    std::uint64_t entry_pc = 0x100000100ULL)
{
    std::vector<std::uint8_t> buf(0x8000, 0);
    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->magic = kMachOMagic64;
    hdr->cputype = kCpuTypeARM64;
    hdr->cpusubtype = kCpuSubtypeARM64All;
    hdr->filetype = kMachOFileTypeExecute;
    hdr->ncmds = 2;
    hdr->sizeofcmds = sizeof(MachOSegmentCommand64) + sizeof(MachOLoadCommand) + 8 + sizeof(MachOArmThreadState64);
    hdr->flags = 0;
    hdr->reserved = 0;

    std::size_t off = sizeof(MachOHeader64);

    // Segment 1: __TEXT (RX)
    auto* seg = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + off);
    seg->cmd = kLoadCmdSegment64;
    seg->cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(seg->segname, "__TEXT", 6);
    seg->vmaddr = text_vmaddr;
    seg->vmsize = text_vmsize;
    seg->fileoff = text_fileoff;
    seg->filesize = text_filesize;
    seg->maxprot = kVmProtRead | kVmProtExecute;
    seg->initprot = kVmProtRead | kVmProtExecute;
    seg->nsects = 0;
    seg->flags = 0;
    off += sizeof(MachOSegmentCommand64);

    // Command 2: LC_UNIXTHREAD
    auto* th = reinterpret_cast<MachOLoadCommand*>(buf.data() + off);
    th->cmd = kLoadCmdUnixThread;
    th->cmdsize = sizeof(MachOLoadCommand) + 8 + sizeof(MachOArmThreadState64);
    off += sizeof(MachOLoadCommand);

    *reinterpret_cast<std::uint32_t*>(buf.data() + off) = kArmThreadState64;
    *reinterpret_cast<std::uint32_t*>(buf.data() + off + 4) = sizeof(MachOArmThreadState64) / 4;
    off += 8;

    auto* ts = reinterpret_cast<MachOArmThreadState64*>(buf.data() + off);
    ts->pc = entry_pc;
    ts->sp = 0x100008000ULL;
    ts->lr = 0;
    ts->cpsr = 0x60000004u;

    return buf;
}

// -----------------------------------------------------------------------------
// Test 1: Truncated headers and boundary buffer checks
// -----------------------------------------------------------------------------
void test_truncated_headers() {
    std::cout << "[Test 1] Truncated headers and small buffer rejection...\n";
    MachOLoadResult res;

    // Buffer smaller than MachOHeader64
    std::uint8_t tiny[sizeof(MachOHeader64) - 1] = {0};
    *reinterpret_cast<std::uint32_t*>(tiny) = kMachOMagic64;
    bool ok = MachOLoader::parse_metadata(tiny, sizeof(tiny), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::TruncatedHeader);

    // Fat binary with truncated MachOFatHeader
    std::uint8_t fat_tiny[sizeof(MachOFatHeader) - 1] = {0};
    *reinterpret_cast<std::uint32_t*>(fat_tiny) = kMachOFatMagic;
    ok = MachOLoader::parse_metadata(fat_tiny, sizeof(fat_tiny), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::InvalidMagic);

    // Fat binary with huge nfat_arch overflow
    std::vector<std::uint8_t> fat_overflow(sizeof(MachOFatHeader) + 16, 0);
    auto* fhdr = reinterpret_cast<MachOFatHeader*>(fat_overflow.data());
    fhdr->magic = kMachOFatMagic;
    fhdr->nfat_arch = 0xFFFFFFFFu; // massive count
    ok = MachOLoader::parse_metadata(fat_overflow.data(), fat_overflow.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::InvalidMagic);
}

// -----------------------------------------------------------------------------
// Test 2: Oversized and zero load command counts
// -----------------------------------------------------------------------------
void test_command_count_validation() {
    std::cout << "[Test 2] Command count bounds (ncmds=0, ncmds > 4096, sizeofcmds mismatch)...\n";
    MachOLoadResult res;

    // ncmds = 0
    auto buf = make_valid_macho();
    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->ncmds = 0;
    bool ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands);

    // ncmds = 50000 (oversized)
    buf = make_valid_macho();
    hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->ncmds = 50000;
    ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands);

    // sizeofcmds smaller than minimum required for ncmds
    buf = make_valid_macho();
    hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->ncmds = 10;
    hdr->sizeofcmds = 16; // 10 commands require at least 80 bytes
    ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands);
}

// -----------------------------------------------------------------------------
// Test 3: Misaligned and corrupted load command sizes
// -----------------------------------------------------------------------------
void test_misaligned_load_command_sizes() {
    std::cout << "[Test 3] Misaligned command sizes and slice bounds violations...\n";
    MachOLoadResult res;

    // cmdsize not aligned to 4 bytes (e.g. cmdsize = 75)
    auto buf = make_valid_macho();
    auto* cmd = reinterpret_cast<MachOLoadCommand*>(buf.data() + sizeof(MachOHeader64));
    cmd->cmdsize = 75;
    bool ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands);

    // cmdsize smaller than sizeof(MachOLoadCommand)
    buf = make_valid_macho();
    cmd = reinterpret_cast<MachOLoadCommand*>(buf.data() + sizeof(MachOHeader64));
    cmd->cmdsize = 4;
    ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands);

    // cmdsize extends beyond slice size
    buf = make_valid_macho();
    cmd = reinterpret_cast<MachOLoadCommand*>(buf.data() + sizeof(MachOHeader64));
    cmd->cmdsize = 0x10000;
    ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands);
}

// -----------------------------------------------------------------------------
// Test 4: Integer overflow in segment vmaddr and fileoff
// -----------------------------------------------------------------------------
void test_segment_integer_overflow() {
    std::cout << "[Test 4] 64-bit integer overflow in segment ranges...\n";
    MachOLoadResult res;

    // vmaddr wraps 64-bit space: vmaddr = 0xFFFFFFFFFFFFF000, vmsize = 0x2000
    auto buf = make_valid_macho();
    auto* seg = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + sizeof(MachOHeader64));
    seg->vmaddr = 0xFFFFFFFFFFFFF000ULL;
    seg->vmsize = 0x2000ULL;
    bool ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::IntegerOverflow);

    // fileoff + filesize overflow / beyond slice
    buf = make_valid_macho();
    seg = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + sizeof(MachOHeader64));
    seg->fileoff = 0xFFFFFFFFFFFFFFF0ULL;
    seg->filesize = 0x100ULL;
    ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands);

    // filesize > vmsize
    buf = make_valid_macho();
    seg = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + sizeof(MachOHeader64));
    seg->vmsize = 0x1000ULL;
    seg->filesize = 0x2000ULL;
    ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::CorruptedLoadCommands);
}

// -----------------------------------------------------------------------------
// Test 5: Segment overlap detection
// -----------------------------------------------------------------------------
void test_segment_overlap_rejection() {
    std::cout << "[Test 5] Segment overlap rejection...\n";
    MachOLoadResult res;

    // Create binary with 2 overlapping segments:
    // Seg 1: __TEXT at 0x100000000, vmsize 0x4000 (ends 0x100004000)
    // Seg 2: __DATA at 0x100002000, vmsize 0x4000 (starts 0x100002000, overlaps __TEXT!)
    std::vector<std::uint8_t> buf(0x8000, 0);
    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->magic = kMachOMagic64;
    hdr->cputype = kCpuTypeARM64;
    hdr->cpusubtype = kCpuSubtypeARM64All;
    hdr->filetype = kMachOFileTypeExecute;
    hdr->ncmds = 2;
    hdr->sizeofcmds = sizeof(MachOSegmentCommand64) * 2;

    std::size_t off = sizeof(MachOHeader64);

    // Seg 1: __TEXT
    auto* s1 = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + off);
    s1->cmd = kLoadCmdSegment64;
    s1->cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(s1->segname, "__TEXT", 6);
    s1->vmaddr = 0x100000000ULL;
    s1->vmsize = 0x4000ULL;
    s1->fileoff = 0x1000ULL;
    s1->filesize = 0x1000ULL;
    s1->initprot = kVmProtRead | kVmProtExecute;
    s1->maxprot = kVmProtRead | kVmProtExecute;
    off += sizeof(MachOSegmentCommand64);

    // Seg 2: __DATA overlapping
    auto* s2 = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + off);
    s2->cmd = kLoadCmdSegment64;
    s2->cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(s2->segname, "__DATA", 6);
    s2->vmaddr = 0x100002000ULL; // Overlaps __TEXT!
    s2->vmsize = 0x4000ULL;
    s2->fileoff = 0x2000ULL;
    s2->filesize = 0x1000ULL;
    s2->initprot = kVmProtRead | kVmProtWrite;
    s2->maxprot = kVmProtRead | kVmProtWrite;

    bool ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::SegmentOverlap);
    CHECK(res.error_message.find("overlap") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Test 6: Invalid Entry Point Validation
// -----------------------------------------------------------------------------
void test_invalid_entry_point_rejection() {
    std::cout << "[Test 6] Invalid entry point validation (outside segments or non-executable)...\n";
    MachOLoadResult res;

    // Entry point outside all segments (0x200000000 when segment is at 0x100000000..0x100004000)
    auto buf = make_valid_macho(0x100000000ULL, 0x4000ULL, 0x1000ULL, 0x1000ULL, 0x200000000ULL);
    bool ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::InvalidEntryPoint);

    // Entry point pointing into non-executable segment
    // Change __TEXT protections to Read/Write only (no Execute)
    buf = make_valid_macho(0x100000000ULL, 0x4000ULL, 0x1000ULL, 0x1000ULL, 0x100000100ULL);
    auto* seg = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + sizeof(MachOHeader64));
    seg->initprot = kVmProtRead | kVmProtWrite;
    seg->maxprot = kVmProtRead | kVmProtWrite;
    ok = MachOLoader::parse_metadata(buf.data(), buf.size(), res);
    CHECK(!ok);
    CHECK(res.status == MachOLoadStatus::InvalidEntryPoint);
}

// -----------------------------------------------------------------------------
// Test 7: Load into Memory Bounds and RAM Zero Check
// -----------------------------------------------------------------------------
void test_memory_load_hardening() {
    std::cout << "[Test 7] Memory load boundary checks (zero RAM, overflow)...\n";
    auto buf = make_valid_macho();
    Memory mem(0x10000);

    // Zero RAM size
    auto res = MachOLoader::load_from_buffer(buf.data(), buf.size(), mem, 0x1000, 0);
    CHECK(!res.is_success());
    CHECK(res.status == MachOLoadStatus::MemoryBoundsExceeded);

    // RAM base + size wraps 64-bit space
    res = MachOLoader::load_from_buffer(buf.data(), buf.size(), mem, 0xFFFFFFFFFFFFF000ULL, 0x2000ULL);
    CHECK(!res.is_success());
    CHECK(res.status == MachOLoadStatus::IntegerOverflow);
}

// -----------------------------------------------------------------------------
// Test 8: Status String Coverage
// -----------------------------------------------------------------------------
void test_status_strings() {
    std::cout << "[Test 8] Status string mappings...\n";
    CHECK(std::string(macho_load_status_string(MachOLoadStatus::Success)) == "Success");
    CHECK(std::string(macho_load_status_string(MachOLoadStatus::SegmentOverlap)) == "SegmentOverlap");
    CHECK(std::string(macho_load_status_string(MachOLoadStatus::InvalidEntryPoint)) == "InvalidEntryPoint");
    CHECK(std::string(macho_load_status_string(MachOLoadStatus::IntegerOverflow)) == "IntegerOverflow");
}

} // namespace

int main() {
    try {
        test_truncated_headers();
        test_command_count_validation();
        test_misaligned_load_command_sizes();
        test_segment_integer_overflow();
        test_segment_overlap_rejection();
        test_invalid_entry_point_rejection();
        test_memory_load_hardening();
        test_status_strings();
    } catch (const std::exception& ex) {
        std::cerr << "[EXCEPTION] " << ex.what() << "\n";
        return 1;
    }

    if (g_failed != 0) {
        std::cerr << "stage_192_2_macho_hardening_tests: " << g_failed << " check(s) failed\n";
        return 1;
    }
    std::cout << "ALL STAGE 192.2 MACH-O HARDENING TESTS PASSED\n";
    return 0;
}

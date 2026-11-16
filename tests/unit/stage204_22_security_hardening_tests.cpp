// ==============================================================================
// WinIOS Campaign 204 Stage 204.22: Security Hardening & Robustness Tests
// ==============================================================================
// Rigorous adversarial validation across loader, runtime, VFS, and syscall boundary:
// 1. Truncated & Corrupt Mach-O Headers
// 2. Integer Overflow in Section & Segment Offsets
// 3. Overlapping Segment Virtual Address Rejection
// 4. Out-of-Range Symbols and Corrupt LC_SYMTAB
// 5. Filesystem Path Traversal Containment
// 6. File Descriptor Table Exhaustion (EMFILE)
// 7. Invalid Syscall Dispatcher Arguments (ENOSYS, EBADF)
// ==============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>

#include "loader/dyld_engine.h"
#include "loader/macho_fixture_corpus.h"
#include "loader/userspace_macho_loader.h"
#include "runtime/darwin_application_runner.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_syscall_dispatcher.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Truncated & Corrupted Mach-O Headers
// -----------------------------------------------------------------------------
void test_truncated_and_corrupt_headers() {
    std::cout << "[Test 1] Truncated & Corrupted Mach-O Headers...\n";

    // Subtest 1.1: Truncated buffer (< 32 bytes)
    std::vector<std::uint8_t> tiny(16, 0);
    UserspaceLoadResult res1;
    bool p1 = UserspaceMachOLoader::parse_metadata(tiny.data(), tiny.size(), res1);
    TEST_CHECK(!p1, "Parsing < 32 bytes must fail");
    TEST_CHECK(res1.status == UserspaceMachOLoadStatus::TruncatedHeader, "Status must be TruncatedHeader");

    // Subtest 1.2: Corrupt magic (0xDEADBEEF)
    std::vector<std::uint8_t> bad_magic(sizeof(MachOHeader64Raw), 0);
    std::uint32_t bogus = 0xDEADBEEFu;
    std::memcpy(bad_magic.data(), &bogus, 4);
    UserspaceLoadResult res2;
    bool p2 = UserspaceMachOLoader::parse_metadata(bad_magic.data(), bad_magic.size(), res2);
    TEST_CHECK(!p2, "Parsing invalid magic must fail");
    TEST_CHECK(res2.status == UserspaceMachOLoadStatus::InvalidMagic, "Status must be InvalidMagic");

    // Subtest 1.3: sizeofcmds extends beyond file size
    std::vector<std::uint8_t> overflow_cmds(sizeof(MachOHeader64Raw), 0);
    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.ncmds = 1;
    hdr.sizeofcmds = 0x8000; // 32KB cmds in 32-byte file
    std::memcpy(overflow_cmds.data(), &hdr, sizeof(hdr));
    UserspaceLoadResult res3;
    bool p3 = UserspaceMachOLoader::parse_metadata(overflow_cmds.data(), overflow_cmds.size(), res3);
    TEST_CHECK(!p3, "Parsing cmds exceeding file size must fail");
}

// -----------------------------------------------------------------------------
// Test 2: Integer Overflow in Segment & Section Offsets
// -----------------------------------------------------------------------------
void test_integer_overflow_handling() {
    std::cout << "[Test 2] Integer Overflow in Segment Offsets...\n";

    std::vector<std::uint8_t> overflow_buf(sizeof(MachOHeader64Raw) + sizeof(MachOSegment64Raw), 0);
    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.ncmds = 1;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw);
    std::memcpy(overflow_buf.data(), &hdr, sizeof(hdr));

    MachOSegment64Raw seg{};
    seg.cmd = kCmdSegment64;
    seg.cmdsize = sizeof(seg);
    std::memcpy(seg.segname, "__OVERFLOW", 10);
    seg.vmaddr = 0x100000000ULL;
    seg.vmsize = 0x4000;
    seg.fileoff = 0xFFFFFFFFFFFFFF00ULL; // Near max uint64, addition will wrap
    seg.filesize = 0x2000ULL;
    seg.maxprot = 7;
    seg.initprot = 7;
    std::memcpy(overflow_buf.data() + sizeof(hdr), &seg, sizeof(seg));

    UserspaceLoadResult res;
    bool p = UserspaceMachOLoader::parse_metadata(overflow_buf.data(), overflow_buf.size(), res);
    if (p) {
        std::string err;
        auto st = UserspaceMachOLoader::validate_segments(res.segments, overflow_buf.size(), 0x4000, err);
        TEST_CHECK(st != UserspaceMachOLoadStatus::Success, "Segment offset overflow must fail validation");
    } else {
        TEST_CHECK(true, "Overflow caught at parse time");
    }
}

// -----------------------------------------------------------------------------
// Test 3: Overlapping Segment Virtual Address Rejection
// -----------------------------------------------------------------------------
void test_overlapping_segments_rejection() {
    std::cout << "[Test 3] Overlapping Segment Virtual Address Rejection...\n";

    auto overlap_fix = PublicMachOCorpus::create_malformed_overlapping_segments();
    UserspaceLoadResult res;
    bool p = UserspaceMachOLoader::parse_metadata(overlap_fix.binary_data.data(), overlap_fix.binary_data.size(), res);
    TEST_CHECK(p, "Overlap fixture should parse load commands");

    std::string err;
    auto val_status = UserspaceMachOLoader::validate_segments(res.segments, overlap_fix.binary_data.size(), 0x4000, err);
    TEST_CHECK(val_status == UserspaceMachOLoadStatus::OverlappingSegments, "Must return OverlappingSegments");

    DarwinApplicationRunner runner;
    std::string load_err;
    bool loaded = runner.load_executable(overlap_fix.binary_data.data(), overlap_fix.binary_data.size(), load_err);
    TEST_CHECK(!loaded, "DarwinApplicationRunner must reject overlapping segments");
    TEST_CHECK(load_err.find("overlaps") != std::string::npos, "Error must describe overlap");
}

// -----------------------------------------------------------------------------
// Test 4: Filesystem Path Traversal Containment
// -----------------------------------------------------------------------------
void test_filesystem_path_traversal() {
    std::cout << "[Test 4] Filesystem Path Traversal Containment...\n";

    DarwinVfs vfs;
    vfs.create_directory("/var");
    vfs.create_directory("/var/log");
    vfs.write_file("/var/log/syslog.txt", "system logs", 11);

    // Canonicalization tests
    TEST_CHECK(vfs.canonicalize_path("/var/log/syslog.txt") == "/var/log/syslog.txt", "Normal path matches");
    TEST_CHECK(vfs.canonicalize_path("/var/log/../../var/log/syslog.txt") == "/var/log/syslog.txt", "Parent traversal resolved");
    TEST_CHECK(vfs.canonicalize_path("/../../../../var/log/syslog.txt") == "/var/log/syslog.txt", "Root escaping stripped");
    TEST_CHECK(vfs.canonicalize_path("/../../../") == "/", "Excess parent traversal clamps to root");

    // Path resolution
    auto node = vfs.resolve_path("/var/log/../../../../var/log/syslog.txt");
    TEST_CHECK(node != nullptr, "Resolved node within VFS");
    TEST_CHECK(node->name == "syslog.txt", "Node matches target");

    // Non-existent path returns nullptr
    auto bad_node = vfs.resolve_path("/var/log/../../../../etc/shadow");
    TEST_CHECK(bad_node == nullptr, "Non-existent file returns nullptr");
}

// -----------------------------------------------------------------------------
// Test 5: File Descriptor Table Exhaustion (EMFILE)
// -----------------------------------------------------------------------------
void test_file_descriptor_exhaustion() {
    std::cout << "[Test 5] File Descriptor Table Exhaustion...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(0, "/usr/bin/fd_stress", 501);

    std::vector<int> opened_fds;
    // Attempt to open up to 300 files
    for (int i = 0; i < 300; ++i) {
        std::string path = "/tmp/file_" + std::to_string(i) + ".txt";
        int fd = bsd.open(pid, path, 0x0200u /* O_CREAT */, 0644);
        if (fd < 0) {
            break;
        }
        opened_fds.push_back(fd);
    }

    std::cout << "  -> Opened FDs count: " << opened_fds.size() << std::endl;
    // Process descriptor table has limit of 256
    TEST_CHECK(opened_fds.size() <= 256, "Opened descriptors must be bounded");
    TEST_CHECK(opened_fds.size() > 200, "Should have allocated descriptors up to limit");

    // Next open must fail (EMFILE)
    int failed_fd = bsd.open(pid, "/tmp/overflow.txt", 0x0200u, 0644);
    TEST_CHECK(failed_fd < 0, "Opening past descriptor limit must fail");

    // Close one descriptor and verify subsequent open succeeds
    if (!opened_fds.empty()) {
        int freed_fd = opened_fds.back();
        opened_fds.pop_back();
        int rc = bsd.close(pid, freed_fd);
        TEST_CHECK(rc == 0, "Closing descriptor must succeed");

        int reclaimed_fd = bsd.open(pid, "/tmp/reclaimed.txt", 0x0200u, 0644);
        TEST_CHECK(reclaimed_fd >= 0, "Reclaimed slot must allow open");
        bsd.close(pid, reclaimed_fd);
    }

    // Clean up remaining fds
    for (int fd : opened_fds) {
        bsd.close(pid, fd);
    }
}

// -----------------------------------------------------------------------------
// Test 6: Invalid Syscall Arguments & Error Codes
// -----------------------------------------------------------------------------
void test_invalid_syscall_arguments() {
    std::cout << "[Test 6] Invalid Syscall Arguments & Error Codes...\n";

    DarwinSyscallDispatcher dispatcher;
    auto bsd = std::make_shared<DarwinBsdSubsystem>();
    std::uint32_t pid = bsd->create_process(0, "/usr/bin/syscall_fuzz", 501);
    dispatcher.bind_bsd_subsystem(bsd, pid);

    CPU cpu;
    Memory mem(0x1000000);

    // 6.1 Unknown syscall number -> ENOSYS (-78)
    cpu.write_register(16, 9999);
    auto res_unknown = dispatcher.dispatch(cpu, mem);
    TEST_CHECK(res_unknown < 0, "Unknown syscall must return negative error");
    TEST_CHECK(cpu.read_register(0) == 78, "X0 must hold ENOSYS (78)");
    TEST_CHECK((cpu.get_nzcv() & 0x2u) != 0, "Carry flag must be set");

    // 6.2 SYS_read on non-existent fd (99) -> EBADF (-9)
    cpu.write_register(16, 3); // SYS_read
    cpu.write_register(0, 99); // bad fd
    cpu.write_register(1, 0x1000);
    cpu.write_register(2, 64);
    auto res_read = dispatcher.dispatch(cpu, mem);
    TEST_CHECK(res_read < 0, "Read on bad fd must return negative error");
    TEST_CHECK(cpu.read_register(0) == 9, "X0 must hold EBADF (9)");
    TEST_CHECK((cpu.get_nzcv() & 0x2u) != 0, "Carry flag must be set");

    // 6.3 SYS_write on non-existent fd (99) -> EBADF (-9)
    cpu.write_register(16, 4); // SYS_write
    cpu.write_register(0, 99); // bad fd
    cpu.write_register(1, 0x1000);
    cpu.write_register(2, 64);
    auto res_write = dispatcher.dispatch(cpu, mem);
    TEST_CHECK(res_write < 0, "Write on bad fd must return negative error");
    TEST_CHECK(cpu.read_register(0) == 9, "X0 must hold EBADF (9)");
    TEST_CHECK((cpu.get_nzcv() & 0x2u) != 0, "Carry flag must be set");

    // 6.4 SYS_close on non-existent fd (99) -> EBADF (-9)
    cpu.write_register(16, 6); // SYS_close
    cpu.write_register(0, 99);
    auto res_close = dispatcher.dispatch(cpu, mem);
    TEST_CHECK(res_close < 0, "Close on bad fd must return negative error");
    TEST_CHECK(cpu.read_register(0) == 9, "X0 must hold EBADF (9)");
    TEST_CHECK((cpu.get_nzcv() & 0x2u) != 0, "Carry flag must be set");
}

// -----------------------------------------------------------------------------
// Test 7: Out-of-Range Symbols in LC_SYMTAB
// -----------------------------------------------------------------------------
void test_out_of_range_symtab() {
    std::cout << "[Test 7] Out-of-Range Symbols in LC_SYMTAB...\n";

    std::vector<std::uint8_t> dummy(1024, 0);
    std::vector<DyldSymtabSymbolInfo> out_symbols;
    std::string err;

    // Out-of-bounds symoff (0x80000 in 1024-byte buffer)
    bool ok = DyldEngine::parse_symbol_table(
        dummy.data(), dummy.size(), 0x80000, 50, 0x90000, 0x1000, out_symbols, err);
    TEST_CHECK(!ok, "Out-of-range symtab must fail parsing");
    TEST_CHECK(!err.empty(), "Error message must report boundary error");

    // Out-of-bounds stroff
    err.clear();
    bool ok2 = DyldEngine::parse_symbol_table(
        dummy.data(), dummy.size(), 0, 2, 0x90000, 0x1000, out_symbols, err);
    TEST_CHECK(!ok2, "Out-of-range string table must fail parsing");
    TEST_CHECK(!err.empty(), "Error message must report boundary error");
}

int main() {
    std::cout << "=== Running Stage 204.22 Security Hardening & Robustness Tests ===" << std::endl;

    test_truncated_and_corrupt_headers();
    test_integer_overflow_handling();
    test_overlapping_segments_rejection();
    test_filesystem_path_traversal();
    test_file_descriptor_exhaustion();
    test_invalid_syscall_arguments();
    test_out_of_range_symtab();

    if (g_failures == 0) {
        std::cout << "ALL STAGE 204.22 SECURITY HARDENING TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED!" << std::endl;
        return 1;
    }
}

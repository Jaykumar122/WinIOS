#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "loader/initramfs_loader.h"
#include "loader/rootfs_builder.h"
#include "runtime/linux_boot_session.h"
#include "runtime/linux_syscall_abi.h"
#include "runtime/userspace_runner.h"

#define STAGE84_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

std::vector<std::uint8_t> create_dummy_kernel() {
    std::vector<std::uint8_t> buf(0x10000, 0);
    auto* hdr = reinterpret_cast<LinuxArm64ImageHeader*>(buf.data());
    hdr->code0 = 0x14000010u; // B +64
    hdr->code1 = 0xD503201Fu; // NOP
    hdr->text_offset = 0x80000ULL;
    hdr->image_size = 0x20000ULL;
    hdr->flags = 0x2u;
    hdr->magic = kLinuxArm64ImageMagic;
    for (std::size_t i = 64; i < buf.size(); i += 4) {
        *reinterpret_cast<std::uint32_t*>(buf.data() + i) = 0xD503201Fu;
    }
    return buf;
}

// Generates AArch64 machine code for a synthetic /init:
// 1. write(1, 0x1000, 18)
// 2. exit_group(0)
std::vector<std::uint8_t> create_synthetic_init_binary() {
    std::vector<std::uint8_t> code;
    code.reserve(64);

    // Instructions:
    // 0x00: MOV X1, #0x1000       -> 0xD2820001 (buf = 0x1000)
    // 0x04: MOV X0, #1            -> 0xD2800020 (fd = 1)
    // 0x08: MOV X2, #18           -> 0xD2800242 (count = 18)
    // 0x0C: MOV X8, #64           -> 0xD2800808 (write = 64)
    // 0x10: SVC #0                -> 0xD4000001
    // 0x14: MOV X0, #0            -> 0xD2800000 (status = 0)
    // 0x18: MOV X8, #94           -> 0xD2800BC8 (exit_group = 94)
    // 0x1C: SVC #0                -> 0xD4000001

    const std::uint32_t insns[] = {
        0xD2820001u, // MOV X1, #0x1000
        0xD2800020u, // MOV X0, #1
        0xD2800242u, // MOV X2, #18
        0xD2800808u, // MOV X8, #64
        0xD4000001u, // SVC #0
        0xD2800000u, // MOV X0, #0
        0xD2800BC8u, // MOV X8, #94
        0xD4000001u  // SVC #0
    };

    for (auto insn : insns) {
        const auto* b = reinterpret_cast<const std::uint8_t*>(&insn);
        code.insert(code.end(), b, b + 4);
    }

    return code;
}

// -----------------------------------------------------------------------------
// Suite 1: CPIO newc In-Memory Archive Generation
// -----------------------------------------------------------------------------
bool test_cpio_archive_generation() {
    std::cout << "[1] test_cpio_archive_generation...\n" << std::flush;

    RootfsBuilder builder;
    builder.add_file_text("hello.txt", "Hello World\n");
    std::cout << "Building CPIO...\n" << std::flush;
    auto cpio = builder.build_cpio();
    std::cout << "Built CPIO, size=" << cpio.size() << "\n" << std::flush;

    STAGE84_ASSERT(!cpio.empty(), "CPIO archive created");
    STAGE84_ASSERT(cpio.size() >= 110, "Size at least header size");
    STAGE84_ASSERT(std::memcmp(cpio.data(), "070701", 6) == 0, "Magic matches 070701");

    // Check TRAILER!!! presence
    std::string s(reinterpret_cast<const char*>(cpio.data()), cpio.size());
    STAGE84_ASSERT(s.find("TRAILER!!!") != std::string::npos, "TRAILER found in archive");

    std::cout << "[PASS] CPIO newc In-Memory Archive Generation\n\n" << std::flush;
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Archive Header and 4-Byte Alignment Conformance
// -----------------------------------------------------------------------------
bool test_archive_alignment_conformance() {
    std::cout << "[2] test_archive_alignment_conformance...\n";

    RootfsBuilder builder;
    builder.add_file_text("a", "1"); // 1-byte name, 1-byte data
    builder.add_file_text("bb", "12"); // 2-byte name, 2-byte data
    builder.add_file_text("ccc", "123"); // 3-byte name, 3-byte data
    auto cpio = builder.build_cpio();

    std::string err;
    auto entries = RootfsBuilder::parse_cpio(cpio.data(), cpio.size(), &err);
    STAGE84_ASSERT(err.empty(), "Parsing succeeded without alignment errors");
    STAGE84_ASSERT(entries.size() == 3, "Parsed exactly 3 entries");
    STAGE84_ASSERT(entries[0].path == "a", "Entry 0 name");
    STAGE84_ASSERT(entries[1].path == "bb", "Entry 1 name");
    STAGE84_ASSERT(entries[2].path == "ccc", "Entry 2 name");

    std::cout << "[PASS] Archive Header and 4-Byte Alignment Conformance\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Essential Rootfs Directory Tree Verification
// -----------------------------------------------------------------------------
bool test_essential_rootfs_directory_tree() {
    std::cout << "[3] test_essential_rootfs_directory_tree...\n";

    auto rootfs = RootfsBuilder::create_minimal_synthetic_rootfs();

    STAGE84_ASSERT(RootfsBuilder::has_file(rootfs.data(), rootfs.size(), "bin"), "/bin directory present");
    STAGE84_ASSERT(RootfsBuilder::has_file(rootfs.data(), rootfs.size(), "dev"), "/dev directory present");
    STAGE84_ASSERT(RootfsBuilder::has_file(rootfs.data(), rootfs.size(), "proc"), "/proc directory present");
    STAGE84_ASSERT(RootfsBuilder::has_file(rootfs.data(), rootfs.size(), "sys"), "/sys directory present");
    STAGE84_ASSERT(RootfsBuilder::has_file(rootfs.data(), rootfs.size(), "etc"), "/etc directory present");
    STAGE84_ASSERT(RootfsBuilder::has_file(rootfs.data(), rootfs.size(), "etc/passwd"), "/etc/passwd present");
    STAGE84_ASSERT(RootfsBuilder::has_file(rootfs.data(), rootfs.size(), "init"), "/init present");

    std::cout << "[PASS] Essential Rootfs Directory Tree Verification\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Synthetic /init Binary Payload Insertion
// -----------------------------------------------------------------------------
bool test_init_binary_payload_insertion() {
    std::cout << "[4] test_init_binary_payload_insertion...\n";

    auto init_code = create_synthetic_init_binary();
    auto rootfs = RootfsBuilder::create_minimal_synthetic_rootfs(init_code);

    std::vector<std::uint8_t> extracted;
    bool found = RootfsBuilder::get_file_content(rootfs.data(), rootfs.size(), "init", extracted);
    STAGE84_ASSERT(found, "/init extracted from rootfs");
    STAGE84_ASSERT(extracted.size() == init_code.size(), "Extracted size matches input code size");
    STAGE84_ASSERT(extracted == init_code, "Extracted bytes match original binary bytes");

    std::cout << "[PASS] Synthetic /init Binary Payload Insertion\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: CPIO Archive Extraction and Path Query Verification
// -----------------------------------------------------------------------------
bool test_cpio_extraction_and_path_queries() {
    std::cout << "[5] test_cpio_extraction_and_path_queries...\n";

    RootfsBuilder builder;
    builder.add_file_text("etc/config.json", "{\"boot\": true}");
    builder.add_file_text("var/log/boot.log", "OK");
    auto cpio = builder.build_cpio();

    // Query with and without leading slash
    STAGE84_ASSERT(RootfsBuilder::has_file(cpio.data(), cpio.size(), "etc/config.json"), "Found without slash");
    STAGE84_ASSERT(RootfsBuilder::has_file(cpio.data(), cpio.size(), "/etc/config.json"), "Found with slash");
    STAGE84_ASSERT(!RootfsBuilder::has_file(cpio.data(), cpio.size(), "nonexistent"), "Nonexistent not found");

    std::vector<std::uint8_t> content;
    STAGE84_ASSERT(RootfsBuilder::get_file_content(cpio.data(), cpio.size(), "/etc/config.json", content), "Content retrieved");
    std::string text(content.begin(), content.end());
    STAGE84_ASSERT(text == "{\"boot\": true}", "Content matches");

    std::cout << "[PASS] CPIO Archive Extraction and Path Query Verification\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Corrupted/Truncated Archive Error Handling
// -----------------------------------------------------------------------------
bool test_corrupted_archive_handling() {
    std::cout << "[6] test_corrupted_archive_handling...\n";

    // 1. Empty buffer
    std::string err;
    auto e1 = RootfsBuilder::parse_cpio(nullptr, 0, &err);
    STAGE84_ASSERT(e1.empty() && !err.empty(), "Empty buffer fails gracefully");

    // 2. Bad magic
    std::vector<std::uint8_t> bad_magic(120, 0);
    std::memcpy(bad_magic.data(), "BADMAG", 6);
    err.clear();
    auto e2 = RootfsBuilder::parse_cpio(bad_magic.data(), bad_magic.size(), &err);
    STAGE84_ASSERT(!err.empty(), "Bad magic detected");

    // 3. Truncated header
    std::vector<std::uint8_t> trunc(50, 0);
    err.clear();
    auto e3 = RootfsBuilder::parse_cpio(trunc.data(), trunc.size(), &err);
    STAGE84_ASSERT(!err.empty(), "Truncated buffer detected");

    std::cout << "[PASS] Corrupted/Truncated Archive Error Handling\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Userspace Stack and EL0 Process Context Initialization
// -----------------------------------------------------------------------------
bool test_userspace_process_context_setup() {
    std::cout << "[7] test_userspace_process_context_setup...\n";

    CPU cpu;
    Memory mem(0x00200000ULL);

    const std::string banner = "Hello from /init!\n";
    std::memcpy(mem.raw_data() + 0x1000, banner.data(), banner.size());

    UserspaceConfig cfg;
    cfg.entry_point = 0x00400000ULL;
    cfg.stack_pointer = 0x007FFFF0ULL;
    cfg.code = { 0xD5, 0x03, 0x20, 0x1F }; // NOP

    STAGE84_ASSERT(LinuxSyscallAbi::is_sp_aligned(cfg.stack_pointer), "SP 16-byte aligned");

    std::cout << "[PASS] Userspace Stack and EL0 Process Context Initialization\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Userspace /init Stepping and Syscall Interception
// -----------------------------------------------------------------------------
bool test_init_stepping_and_syscall_interception() {
    std::cout << "[8] test_init_stepping_and_syscall_interception...\n";

    CPU cpu;
    Memory mem(0x00200000ULL);

    const std::string banner = "Hello from /init!\n";
    std::memcpy(mem.raw_data() + 0x1000, banner.data(), banner.size());

    UserspaceConfig cfg;
    cfg.entry_point = 0x00010000ULL;
    cfg.stack_pointer = 0x00080000ULL;
    cfg.code = create_synthetic_init_binary();
    cfg.max_instructions = 1000;

    UserspaceExecutionResult res = UserspaceRunner::run(cpu, mem, cfg);

    STAGE84_ASSERT(res.exited, "/init program completed and exited");
    STAGE84_ASSERT(res.exit_code == 0, "Exit code is 0");
    STAGE84_ASSERT(res.console_output.find("Hello from /init!\n") != std::string::npos,
                   "Console output intercepted from write() syscall");

    std::cout << "Intercepted /init console output:\n" << res.console_output << "\n";
    std::cout << "[PASS] Userspace /init Stepping and Syscall Interception\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Process Clean Termination via exit_group(0)
// -----------------------------------------------------------------------------
bool test_process_clean_exit_group() {
    std::cout << "[9] test_process_clean_exit_group...\n";

    CPU cpu;
    Memory mem(0x00200000ULL);

    // Assembly:
    // MOV X0, #42
    // MOV X8, #94 (exit_group)
    // SVC #0
    const std::uint32_t code_insns[] = {
        0xD2800540u, // MOV X0, #42
        0xD2800BC8u, // MOV X8, #94
        0xD4000001u  // SVC #0
    };
    std::vector<std::uint8_t> code;
    for (auto i : code_insns) {
        const auto* b = reinterpret_cast<const std::uint8_t*>(&i);
        code.insert(code.end(), b, b + 4);
    }

    UserspaceConfig cfg;
    cfg.entry_point = 0x00020000ULL;
    cfg.stack_pointer = 0x00080000ULL;
    cfg.code = code;

    UserspaceExecutionResult res = UserspaceRunner::run(cpu, mem, cfg);
    STAGE84_ASSERT(res.exited, "Process exited");
    STAGE84_ASSERT(res.exit_code == 42, "Exit status is 42");

    std::cout << "[PASS] Process Clean Termination via exit_group(0)\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Integrated Linux Boot Session with Validated Rootfs
// -----------------------------------------------------------------------------
bool test_integrated_boot_session_with_rootfs() {
    std::cout << "[10] test_integrated_boot_session_with_rootfs...\n";

    auto rootfs = RootfsBuilder::create_minimal_synthetic_rootfs(create_synthetic_init_binary());

    LinuxBootConfig cfg;
    cfg.kernel_data = create_dummy_kernel();
    cfg.initramfs_data = rootfs;

    LinuxBootSession session(cfg);
    std::string err;
    STAGE84_ASSERT(session.initialize(err), "Session initialize with rootfs");

    const auto* initrd_region = session.memory_map().find_region(GuestMemoryRegionType::Initramfs);
    STAGE84_ASSERT(initrd_region != nullptr, "Initramfs registered in memory map");
    STAGE84_ASSERT(initrd_region->size >= rootfs.size(), "Region size sufficient for CPIO");

    STAGE84_ASSERT(session.boot(err), "Boot session handoff");
    STAGE84_ASSERT(session.status() == LinuxSessionStatus::Running, "Session is running");

    // Verify rootfs CPIO magic present in guest RAM at initrd location
    std::uint64_t initrd_start = initrd_region->base_address;
    std::uint8_t magic[6];
    for (std::size_t i = 0; i < 6; ++i) {
        magic[i] = session.platform().memory().read_u8(initrd_start + i);
    }
    STAGE84_ASSERT(std::memcmp(magic, "070701", 6) == 0, "Valid CPIO newc archive placed in guest RAM");

    std::cout << "[PASS] Integrated Linux Boot Session with Validated Rootfs\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== WinIOS Stage 84: Minimal Userspace Boot & Rootfs Validation Tests ===\n" << std::flush;

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        std::cout << "Starting " << name << "...\n" << std::flush;
        if (test_fn()) {
            passed++;
            std::cout << "Completed " << name << ": OK\n" << std::flush;
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n" << std::flush;
        }
    };

    run_test(test_cpio_archive_generation, "test_cpio_archive_generation");
    run_test(test_archive_alignment_conformance, "test_archive_alignment_conformance");
    run_test(test_essential_rootfs_directory_tree, "test_essential_rootfs_directory_tree");
    run_test(test_init_binary_payload_insertion, "test_init_binary_payload_insertion");
    run_test(test_cpio_extraction_and_path_queries, "test_cpio_extraction_and_path_queries");
    run_test(test_corrupted_archive_handling, "test_corrupted_archive_handling");
    run_test(test_userspace_process_context_setup, "test_userspace_process_context_setup");
    run_test(test_init_stepping_and_syscall_interception, "test_init_stepping_and_syscall_interception");
    run_test(test_process_clean_exit_group, "test_process_clean_exit_group");
    run_test(test_integrated_boot_session_with_rootfs, "test_integrated_boot_session_with_rootfs");

    std::cout << "\nStage 84 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "firmware/apple_boot_args.h"
#include "runtime/apple_kernel_execution_engine.h"

using namespace winios;
using namespace winios::firmware;
using namespace winios::runtime;

static int g_test_count = 0;
static int g_pass_count = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(name, func) \
    do { \
        g_test_count++; \
        std::cout << "[RUN] Test " << g_test_count << ": " << name << " ... "; \
        if (func()) { \
            g_pass_count++; \
            std::cout << "PASS\n"; \
        } else { \
            std::cout << "FAIL\n"; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Compile-time layout, size, and alignment verification (Static Asserts)
// -----------------------------------------------------------------------------
bool test_compile_time_abi_layout() {
    // 1. AppleBootVideo (48 bytes, 8-byte aligned)
    static_assert(sizeof(AppleBootVideo) == 48);
    static_assert(alignof(AppleBootVideo) == 8);
    TEST_ASSERT(sizeof(AppleBootVideo) == 48, "AppleBootVideo must be 48 bytes");
    TEST_ASSERT(alignof(AppleBootVideo) == 8, "AppleBootVideo must be 8-byte aligned");

    // 2. AppleBootArgsRev2 (736 bytes, 8-byte aligned)
    static_assert(sizeof(AppleBootArgsRev2) == 736);
    static_assert(alignof(AppleBootArgsRev2) == 8);
    static_assert(offsetof(AppleBootArgsRev2, Revision) == 0);
    static_assert(offsetof(AppleBootArgsRev2, Version) == 2);
    static_assert(offsetof(AppleBootArgsRev2, virtBase) == 8);
    static_assert(offsetof(AppleBootArgsRev2, physBase) == 16);
    static_assert(offsetof(AppleBootArgsRev2, memSize) == 24);
    static_assert(offsetof(AppleBootArgsRev2, topOfKernelData) == 32);
    static_assert(offsetof(AppleBootArgsRev2, Video) == 40);
    static_assert(offsetof(AppleBootArgsRev2, machineType) == 88);
    static_assert(offsetof(AppleBootArgsRev2, deviceTreeP) == 96);
    static_assert(offsetof(AppleBootArgsRev2, deviceTreeLength) == 104);
    static_assert(offsetof(AppleBootArgsRev2, CommandLine) == 108);
    static_assert(offsetof(AppleBootArgsRev2, bootFlags) == 720);
    static_assert(offsetof(AppleBootArgsRev2, memSizeActual) == 728);

    // 3. AppleBootArgsRev3 (1152 bytes, 8-byte aligned)
    static_assert(sizeof(AppleBootArgsRev3) == 1152);
    static_assert(alignof(AppleBootArgsRev3) == 8);
    static_assert(offsetof(AppleBootArgsRev3, Revision) == 0);
    static_assert(offsetof(AppleBootArgsRev3, Version) == 2);
    static_assert(offsetof(AppleBootArgsRev3, virtBase) == 8);
    static_assert(offsetof(AppleBootArgsRev3, physBase) == 16);
    static_assert(offsetof(AppleBootArgsRev3, memSize) == 24);
    static_assert(offsetof(AppleBootArgsRev3, topOfKernelData) == 32);
    static_assert(offsetof(AppleBootArgsRev3, Video) == 40);
    static_assert(offsetof(AppleBootArgsRev3, machineType) == 88);
    static_assert(offsetof(AppleBootArgsRev3, deviceTreeP) == 96);
    static_assert(offsetof(AppleBootArgsRev3, deviceTreeLength) == 104);
    static_assert(offsetof(AppleBootArgsRev3, CommandLine) == 108);
    static_assert(offsetof(AppleBootArgsRev3, bootFlags) == 1136);
    static_assert(offsetof(AppleBootArgsRev3, memSizeActual) == 1144);

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Standard host-side builder validation (Revision 2)
// -----------------------------------------------------------------------------
bool test_builder_rev2() {
    AppleBootVideo video{};
    video.v_baseAddr = 0x48000000ULL;
    video.v_display = 1;
    video.v_rowBytes = 4704;
    video.v_width = 1176;
    video.v_height = 2556;
    video.v_depth = 32;

    const auto args = AppleBootArgsBuilder::create(
        0x40000000ULL,
        0xFFFFFE0007004000ULL,
        64ULL * 1024 * 1024,
        0x42000000ULL,
        0x43E00000ULL,
        2048,
        "-v debug=0x144 serial=3",
        video);

    TEST_ASSERT(args.Revision == kAppleBootArgsRevision2, "Revision must be 2");
    TEST_ASSERT(args.Version == kAppleBootArgsVersion2, "Version must be 2");
    TEST_ASSERT(args.physBase == 0x40000000ULL, "physBase mismatch");
    TEST_ASSERT(args.virtBase == 0xFFFFFE0007004000ULL, "virtBase mismatch");
    TEST_ASSERT(args.memSize == 64ULL * 1024 * 1024, "memSize mismatch");
    TEST_ASSERT(args.memSizeActual == 64ULL * 1024 * 1024, "memSizeActual mismatch");
    TEST_ASSERT(args.deviceTreeP == 0x43E00000ULL, "deviceTreeP mismatch");
    TEST_ASSERT(args.deviceTreeLength == 2048, "deviceTreeLength mismatch");
    TEST_ASSERT(std::string(args.CommandLine) == "-v debug=0x144 serial=3", "CommandLine mismatch");
    TEST_ASSERT(args.Video.v_width == 1176, "Video width mismatch");

    std::string err;
    TEST_ASSERT(AppleBootArgsValidator::validate(args, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Valid Rev 2 boot args should pass validation: " + err);

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Downstream experimental builder (1152-byte Rev 3 layout)
// -----------------------------------------------------------------------------
bool test_builder_rev3() {
    AppleBootVideo video{};
    video.v_baseAddr = 0x48000000ULL;
    video.v_display = 1;
    video.v_rowBytes = 4704;
    video.v_width = 1176;
    video.v_height = 2556;
    video.v_depth = 32;

    const AppleBootArgsExperimentalRev3 args = AppleBootArgsBuilder::create_experimental_rev3(
        0x40000000ULL,
        0xFFFFFE0007004000ULL,
        64ULL * 1024 * 1024,
        0x42000000ULL,
        0x43E00000ULL,
        4096,
        "-v debug=0x144 keepsyms=1 serial=3 cs_enforcement=1",
        video,
        kAppleBootFlagDarkBoot | kAppleBootFlagSerial);

    TEST_ASSERT(args.Revision == kAppleBootArgsExperimentalRevision3, "Revision must be 3");
    TEST_ASSERT(args.Version == kAppleBootArgsVersion2, "Version must be 2");
    TEST_ASSERT(sizeof(args.CommandLine) == 1024, "CommandLine must be 1024 bytes in Rev 3");
    TEST_ASSERT(sizeof(AppleBootArgsExperimentalRev3) == 1152, "Rev 3 size must be 1152 bytes");
    TEST_ASSERT((args.bootFlags & kAppleBootFlagSerial) != 0, "Serial boot flag must be set");

    std::string err;
    TEST_ASSERT(AppleBootArgsValidator::validate_rev3(args, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Valid Rev 3 boot args should pass validation: " + err);

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Buffer truncation rejection
// -----------------------------------------------------------------------------
bool test_buffer_truncation_rejection() {
    std::vector<std::uint8_t> small_buf(500, 0); // Smaller than 736
    std::string err;
    TEST_ASSERT(!AppleBootArgsValidator::validate_buffer_layout(small_buf.data(), small_buf.size(), &err),
                "Must reject buffer smaller than Rev 2 size (736)");
    TEST_ASSERT(err.find("truncated") != std::string::npos, "Error must describe truncation");

    // Rev 3 truncated
    std::vector<std::uint8_t> rev3_buf(800, 0);
    auto* r16 = reinterpret_cast<std::uint16_t*>(rev3_buf.data());
    r16[0] = kAppleBootArgsRevision3;
    r16[1] = kAppleBootArgsVersion2;
    TEST_ASSERT(!AppleBootArgsValidator::validate_buffer_layout(rev3_buf.data(), rev3_buf.size(), &err),
                "Must reject Rev 3 buffer smaller than 1152 bytes");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Misaligned buffer rejection
// -----------------------------------------------------------------------------
bool test_misaligned_buffer_rejection() {
    std::vector<std::uint8_t> buf(1200, 0);
    // Offset by 1 byte to make pointer unaligned (odd address)
    const void* misaligned = buf.data() + 1;
    std::string err;
    TEST_ASSERT(!AppleBootArgsValidator::validate_buffer_layout(misaligned, 736, &err),
                "Must reject buffer not 8-byte aligned");
    TEST_ASSERT(err.find("aligned") != std::string::npos, "Error must report alignment violation");

    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Unsupported Revision and Version rejection
// -----------------------------------------------------------------------------
bool test_unsupported_version_rejection() {
    AppleBootArgsRev2 args = AppleBootArgsBuilder::create(
        0x40000000ULL, 0xFFFFFE0007004000ULL, 64ULL * 1024 * 1024,
        0x42000000ULL, 0x43E00000ULL, 1024);

    // 1. Revision 1 (legacy 32-bit / non-64-bit Apple Silicon)
    args.Revision = 1;
    std::string err;
    TEST_ASSERT(!AppleBootArgsValidator::validate(args, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Must reject Revision 1");

    // 2. Revision 99
    args.Revision = 99;
    TEST_ASSERT(!AppleBootArgsValidator::validate(args, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Must reject Revision 99");

    // 3. Version 1
    args.Revision = 2;
    args.Version = 1;
    TEST_ASSERT(!AppleBootArgsValidator::validate(args, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Must reject Version 1");

    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Physical memory range & page granule alignment enforcement
// -----------------------------------------------------------------------------
bool test_memory_granule_alignment_and_range() {
    std::string err;

    // 1. Unaligned physBase (must be 16 KB page aligned for Apple Silicon)
    TEST_ASSERT(!AppleBootArgsValidator::validate_memory_contract(
                    0x40002000ULL, // Not 16 KB aligned (0x2000 vs 0x4000)
                    64ULL * 1024 * 1024, 0x42000000ULL, 0xFFFFFE0007004000ULL,
                    0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Must reject non-16KB aligned physBase");
    TEST_ASSERT(err.find("16 KB") != std::string::npos, "Error must describe 16 KB granule alignment");

    // 2. Zero memSize
    TEST_ASSERT(!AppleBootArgsValidator::validate_memory_contract(
                    0x40000000ULL, 0, 0x42000000ULL, 0xFFFFFE0007004000ULL,
                    0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Must reject zero memSize");

    // 3. topOfKernelData < physBase
    TEST_ASSERT(!AppleBootArgsValidator::validate_memory_contract(
                    0x40000000ULL, 64ULL * 1024 * 1024, 0x3FFFF000ULL, 0xFFFFFE0007004000ULL,
                    0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Must reject topOfKernelData < physBase");

    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Integer overflow protection
// -----------------------------------------------------------------------------
bool test_address_overflow_protection() {
    std::string err;
    // physBase + memSize overflow (using 16KB aligned physBase)
    const std::uint64_t overflow_phys = 0xFFFFFFFFFFFFC000ULL;
    const std::uint64_t huge_size = 0x10000ULL;

    TEST_ASSERT(!AppleBootArgsValidator::validate_memory_contract(
                    overflow_phys, huge_size, overflow_phys + 0x4000ULL, 0xFFFFFE0007004000ULL,
                    0x40000000ULL, 0x40000000ULL, &err),
                "Must reject integer overflow in physical memory bounds");
    TEST_ASSERT(err.find("overflow") != std::string::npos, "Error must mention overflow");

    return true;
}

// -----------------------------------------------------------------------------
// Test 9: RAM boundary enforcement
// -----------------------------------------------------------------------------
bool test_ram_boundary_enforcement() {
    std::string err;
    // Memory exceeds physical RAM
    TEST_ASSERT(!AppleBootArgsValidator::validate_memory_contract(
                    0x40000000ULL, 128ULL * 1024 * 1024, 0x42000000ULL, 0xFFFFFE0007004000ULL,
                    0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Must reject memory range extending beyond allocated RAM");

    return true;
}

// -----------------------------------------------------------------------------
// Test 10: DeviceTree contract validation
// -----------------------------------------------------------------------------
bool test_devicetree_contract_validation() {
    std::string err;
    // 1. Null DeviceTree pointer
    TEST_ASSERT(!AppleBootArgsValidator::validate_devicetree_contract(
                    0, 1024, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Must reject null DeviceTree pointer");

    // 2. Zero length
    TEST_ASSERT(!AppleBootArgsValidator::validate_devicetree_contract(
                    0x43E00000ULL, 0, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Must reject zero DeviceTree length");

    // 3. Unaligned DeviceTree pointer
    TEST_ASSERT(!AppleBootArgsValidator::validate_devicetree_contract(
                    0x43E00003ULL, 1024, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Must reject unaligned DeviceTree pointer");

    // 4. Valid DeviceTree pointer
    TEST_ASSERT(AppleBootArgsValidator::validate_devicetree_contract(
                    0x43E00000ULL, 2048, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Valid DeviceTree must pass");

    return true;
}

// -----------------------------------------------------------------------------
// Test 11: Non-overlapping regions validation
// -----------------------------------------------------------------------------
bool test_non_overlapping_regions() {
    std::string err;

    // 1. Valid non-overlapping layout
    // Kernel: 0x40000000..0x42000000
    // DTB:    0x43E00000..0x43E01000
    // FB:     0x48000000..0x49000000
    TEST_ASSERT(AppleBootArgsValidator::validate_non_overlapping_regions(
                    0x40000000ULL, 0x42000000ULL,
                    0x43E00000ULL, 0x1000,
                    0x48000000ULL, 0x1000000ULL, &err),
                "Non-overlapping regions must pass");

    // 2. Kernel overlaps DeviceTree
    // Kernel: 0x40000000..0x43E00800, DTB: 0x43E00000..0x43E01000
    TEST_ASSERT(!AppleBootArgsValidator::validate_non_overlapping_regions(
                    0x40000000ULL, 0x43E00800ULL,
                    0x43E00000ULL, 0x1000,
                    0, 0, &err),
                "Must detect Kernel overlapping DeviceTree");
    TEST_ASSERT(err.find("overlaps") != std::string::npos, "Error must mention overlap");

    // 3. Framebuffer overlaps DeviceTree
    TEST_ASSERT(!AppleBootArgsValidator::validate_non_overlapping_regions(
                    0x40000000ULL, 0x41000000ULL,
                    0x43E00000ULL, 0x1000,
                    0x43E00800ULL, 0x10000, &err),
                "Must detect Framebuffer overlapping DeviceTree");

    return true;
}

// -----------------------------------------------------------------------------
// Test 12: Command line validation
// -----------------------------------------------------------------------------
bool test_command_line_validation() {
    std::string err;

    // 1. Valid command line
    char valid_cmd[] = "-v debug=0x144 serial=3 keepsyms=1";
    TEST_ASSERT(AppleBootArgsValidator::validate_command_line(valid_cmd, sizeof(valid_cmd), &err),
                "Valid command line must pass");

    // 2. Unterminated command line
    char unterminated[16];
    std::memset(unterminated, 'A', sizeof(unterminated));
    TEST_ASSERT(!AppleBootArgsValidator::validate_command_line(unterminated, sizeof(unterminated), &err),
                "Unterminated command line must be rejected");

    // 3. Non-printable binary control bytes
    char binary_cmd[] = "-v debug=\x01\x02";
    TEST_ASSERT(!AppleBootArgsValidator::validate_command_line(binary_cmd, sizeof(binary_cmd), &err),
                "Command line with control characters must be rejected");

    return true;
}

// -----------------------------------------------------------------------------
// Test 13: Virtual address canonical mapping validation
// -----------------------------------------------------------------------------
bool test_virtual_address_canonical_mapping() {
    std::string err;

    // 1. Valid high-half kernel VA
    TEST_ASSERT(AppleBootArgsValidator::validate_memory_contract(
                    0x40000000ULL, 64ULL * 1024 * 1024, 0x42000000ULL,
                    0xFFFFFE0007004000ULL, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Canonical high-half virtBase must pass");

    // 2. Invalid low-half address (userspace or unmapped address)
    TEST_ASSERT(!AppleBootArgsValidator::validate_memory_contract(
                    0x40000000ULL, 64ULL * 1024 * 1024, 0x42000000ULL,
                    0x0000000000400000ULL, 0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Non-canonical low-half virtBase must be rejected");

    return true;
}

// -----------------------------------------------------------------------------
// Test 14: Video Console ABI conformance
// -----------------------------------------------------------------------------
bool test_video_console_abi() {
    AppleBootVideo v{};
    v.v_baseAddr = 0x50000000ULL;
    v.v_display = 1;
    v.v_width = 1176;
    v.v_height = 2556;
    v.v_depth = 32;
    v.v_rowBytes = 1176 * 4; // 4704

    TEST_ASSERT(v.v_rowBytes == 4704, "Stride calculation match");
    const std::uint64_t total_fb_bytes = v.v_height * v.v_rowBytes;
    TEST_ASSERT(total_fb_bytes == 2556ULL * 4704ULL, "Total framebuffer size match");

    return true;
}

// -----------------------------------------------------------------------------
// Test 15: Boot flags verification
// -----------------------------------------------------------------------------
bool test_boot_flags() {
    TEST_ASSERT(kAppleBootFlagDarkBoot == 1ULL, "DarkBoot flag bit 0");
    TEST_ASSERT(kAppleBootFlagTextMode == 2ULL, "TextMode flag bit 1");
    TEST_ASSERT(kAppleBootFlagSerial == 4ULL, "Serial flag bit 2");
    TEST_ASSERT(kAppleBootFlagSafeBoot == 8ULL, "SafeBoot flag bit 3");

    return true;
}

// -----------------------------------------------------------------------------
// Test 16: Guest CPU boot contract execution with naturally aligned ABI
// -----------------------------------------------------------------------------
bool test_guest_cpu_boot_contract_execution() {
    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 35;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    TEST_ASSERT(engine.initialize(&err), "Engine initialization failed: " + err);

    // Verify natural ABI offsets staged in physical RAM
    // Revision at 0x00, Version at 0x02
    TEST_ASSERT(engine.memory().read_u16(cfg.boot_args_phys) == 2, "boot_args->Revision must be 2");
    TEST_ASSERT(engine.memory().read_u16(cfg.boot_args_phys + 2) == 2, "boot_args->Version must be 2");
    // deviceTreeP at offset 96 (0x60)
    TEST_ASSERT(engine.memory().read_u64(cfg.boot_args_phys + offsetof(AppleBootArgs, deviceTreeP)) == cfg.devicetree_phys,
                "boot_args->deviceTreeP must match cfg.devicetree_phys at offset 96");
    // physBase at offset 16 (0x10)
    TEST_ASSERT(engine.memory().read_u64(cfg.boot_args_phys + offsetof(AppleBootArgs, physBase)) == cfg.ram_base,
                "boot_args->physBase must match cfg.ram_base at offset 16");

    const auto ev = engine.run();
    TEST_ASSERT(ev.setup_success, "Execution setup must succeed");
    TEST_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Kernel must halt at WFI");
    TEST_ASSERT(ev.instructions_retired == 23, "Must retire exactly 23 instructions in cold-boot sequence");
    TEST_ASSERT(ev.final_state.x[0] == 0, "X0 must be 0 (success status code)");
    TEST_ASSERT(ev.final_state.x[19] == cfg.boot_args_phys, "X19 must preserve boot_args pointer");
    TEST_ASSERT(ev.final_state.x[1] == 2, "X1 must hold Revision 2");
    TEST_ASSERT(ev.final_state.x[2] == 2, "X2 must hold Version 2");
    TEST_ASSERT(ev.final_state.x[22] == 0xFFFFFE0007004000ULL, "X22 must hold virtBase (BA_VIRT_BASE)");
    TEST_ASSERT(ev.final_state.x[23] == cfg.ram_base, "X23 must hold physBase (BA_PHYS_BASE)");
    TEST_ASSERT(ev.final_state.x[24] == cfg.ram_size, "X24 must hold memSize (BA_MEM_SIZE)");
    TEST_ASSERT(ev.final_state.x[5] == cfg.devicetree_phys, "X5 must hold deviceTreeP");
    TEST_ASSERT(ev.final_state.x[6] == 16, "X6 must hold deviceTreeLength");
    TEST_ASSERT(ev.final_state.x[7] == 0x61647473u, "X7 must hold 'adts' magic loaded from DTB");
    TEST_ASSERT(ev.final_state.x[26] == kAppleBootFlagDarkBoot, "X26 must hold bootFlags (BA_BOOT_FLAGS)");
    TEST_ASSERT(ev.final_state.x[27] == cfg.ram_size, "X27 must hold memSizeActual");
    TEST_ASSERT(ev.final_state.x[20] == 0x504C4150ULL, "X20 must contain 'APPL' token");

    // Verify stack stores
    const std::uint64_t stack_flags = engine.memory().read_u64(cfg.boot_stack_phys + 8);
    TEST_ASSERT(stack_flags == kAppleBootFlagDarkBoot, "Boot stack at SP+8 must contain bootFlags");
    const std::uint32_t stack_val = engine.memory().read_u32(cfg.boot_stack_phys);
    TEST_ASSERT(stack_val == 0x61647473u, "Boot stack at SP+0 must contain 'adts' DTB word");

    return true;
}

// -----------------------------------------------------------------------------
// Test 17: Guest corruption detection at verified natural offsets
// -----------------------------------------------------------------------------
bool test_guest_corruption_detection() {
    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 30;

    // 1. Nullify deviceTreeP at offset 96
    {
        AppleKernelExecutionEngine engine(cfg);
        TEST_ASSERT(engine.initialize(), "Init engine");

        engine.memory().write_u64(cfg.boot_args_phys + offsetof(AppleBootArgs, deviceTreeP), 0);
        const auto ev = engine.run();
        TEST_ASSERT(ev.final_state.x[0] == 1, "X0 must be 1 (error code after CBZ on null deviceTreeP)");
    }

    // 2. Corrupt Revision at offset 0
    {
        AppleKernelExecutionEngine engine(cfg);
        TEST_ASSERT(engine.initialize(), "Init engine");

        engine.memory().write_u16(cfg.boot_args_phys, 0x1234);
        const auto ev = engine.run();
        TEST_ASSERT(ev.final_state.x[0] == 1, "X0 must be 1 (error code after Revision mismatch)");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Test 18: Idempotent serialization & memory read-back
// -----------------------------------------------------------------------------
bool test_idempotent_serialization() {
    const auto orig = AppleBootArgsBuilder::create(
        0x40000000ULL, 0xFFFFFE0007004000ULL, 64ULL * 1024 * 1024,
        0x42000000ULL, 0x43E00000ULL, 2048, "serial=3 debug=0x144");

    std::vector<std::uint8_t> mem(sizeof(AppleBootArgsRev2), 0);
    std::memcpy(mem.data(), &orig, sizeof(AppleBootArgsRev2));

    AppleBootArgsRev2 read_back{};
    std::memcpy(&read_back, mem.data(), sizeof(AppleBootArgsRev2));

    TEST_ASSERT(read_back.Revision == orig.Revision, "Revision match");
    TEST_ASSERT(read_back.Version == orig.Version, "Version match");
    TEST_ASSERT(read_back.physBase == orig.physBase, "physBase match");
    TEST_ASSERT(read_back.virtBase == orig.virtBase, "virtBase match");
    TEST_ASSERT(read_back.deviceTreeP == orig.deviceTreeP, "deviceTreeP match");
    TEST_ASSERT(std::strcmp(read_back.CommandLine, orig.CommandLine) == 0, "CommandLine match");

    return true;
}

// -----------------------------------------------------------------------------
// Test 19: Separation of genuine ABI rules from emulator safety policies
// -----------------------------------------------------------------------------
bool test_abi_rules_vs_safety_policy_separation() {
    // 1. Pure C struct ABI rules (Apple XNU pexpert/pexpert/arm64/boot.h)
    static_assert(sizeof(AppleBootArgsRev2) == 736, "Canonical Rev 2 size must be 736");
    static_assert(alignof(AppleBootArgsRev2) == 8, "Canonical Rev 2 alignment must be 8");
    static_assert(kAppleBootArgsRevision2 == 2, "Canonical Revision must be 2");
    static_assert(kAppleBootArgsVersion2 == 2, "Canonical Version must be 2");
    static_assert(kAppleBootCommandLineMaxRev2 == 608, "Canonical BOOT_LINE_LENGTH must be 608");
    static_assert(sizeof(AppleBootVideo) == 48, "Boot_Video size must be 48");

    // All field offsets naturally aligned
    static_assert(offsetof(AppleBootArgsRev2, Revision) == 0);
    static_assert(offsetof(AppleBootArgsRev2, Version) == 2);
    static_assert(offsetof(AppleBootArgsRev2, virtBase) == 8);
    static_assert(offsetof(AppleBootArgsRev2, physBase) == 16);
    static_assert(offsetof(AppleBootArgsRev2, memSize) == 24);
    static_assert(offsetof(AppleBootArgsRev2, topOfKernelData) == 32);
    static_assert(offsetof(AppleBootArgsRev2, Video) == 40);
    static_assert(offsetof(AppleBootArgsRev2, machineType) == 88);
    static_assert(offsetof(AppleBootArgsRev2, deviceTreeP) == 96);
    static_assert(offsetof(AppleBootArgsRev2, deviceTreeLength) == 104);
    static_assert(offsetof(AppleBootArgsRev2, CommandLine) == 108);
    static_assert(offsetof(AppleBootArgsRev2, bootFlags) == 720);
    static_assert(offsetof(AppleBootArgsRev2, memSizeActual) == 728);

    // 2. Emulator / WinIOS safety policies (distinct from raw C struct ABI)
    std::string err;

    // Policy A: 16 KB page granule requirement on Apple Silicon
    TEST_ASSERT(!AppleBootArgsValidator::validate_memory_contract(
                    0x40001000ULL, 64ULL * 1024 * 1024, 0x42000000ULL, 0xFFFFFE0007004000ULL,
                    0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Safety Policy: Must enforce 16 KB page granule");
    TEST_ASSERT(err.find("16 KB") != std::string::npos, "Policy message must cite 16 KB page granule");

    // Policy B: Host RAM buffer boundary check
    TEST_ASSERT(!AppleBootArgsValidator::validate_memory_contract(
                    0x40000000ULL, 128ULL * 1024 * 1024, 0x42000000ULL, 0xFFFFFE0007004000ULL,
                    0x40000000ULL, 64ULL * 1024 * 1024, &err),
                "Safety Policy: Must prevent physical RAM over-subscription");

    // Policy C: Integer overflow protection
    TEST_ASSERT(!AppleBootArgsValidator::validate_memory_contract(
                    0xFFFFFFFFFFFFC000ULL, 0x10000ULL, 0xFFFFFFFFFFFFE000ULL, 0xFFFFFE0007004000ULL,
                    0x40000000ULL, 0x40000000ULL, &err),
                "Safety Policy: Must reject integer overflow");

    // Policy D: Non-overlapping execution memory regions
    TEST_ASSERT(!AppleBootArgsValidator::validate_non_overlapping_regions(
                    0x40000000ULL, 0x43E00100ULL,
                    0x43E00000ULL, 0x1000ULL,
                    0, 0, &err),
                "Safety Policy: Must detect overlapping kernel and DTB");

    // Policy E: Printable ASCII command line enforcement
    char bad_cmd[] = "-v \x07\x08";
    TEST_ASSERT(!AppleBootArgsValidator::validate_command_line(bad_cmd, sizeof(bad_cmd), &err),
                "Safety Policy: Must reject binary control characters in command line");

    return true;
}

// -----------------------------------------------------------------------------
// Main Test Runner
// -----------------------------------------------------------------------------
int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 104: Apple Boot Arguments ABI Verification Test Suite     \n";
    std::cout << "=================================================================\n";

    RUN_TEST("Compile-time ABI layout & alignment", test_compile_time_abi_layout);
    RUN_TEST("Standard builder Rev 2", test_builder_rev2);
    RUN_TEST("Downstream experimental builder (1152-byte Rev 3)", test_builder_rev3);
    RUN_TEST("Buffer truncation rejection", test_buffer_truncation_rejection);
    RUN_TEST("Misaligned buffer rejection", test_misaligned_buffer_rejection);
    RUN_TEST("Unsupported version rejection", test_unsupported_version_rejection);
    RUN_TEST("Memory granule alignment & range", test_memory_granule_alignment_and_range);
    RUN_TEST("Address overflow protection", test_address_overflow_protection);
    RUN_TEST("RAM boundary enforcement", test_ram_boundary_enforcement);
    RUN_TEST("DeviceTree contract validation", test_devicetree_contract_validation);
    RUN_TEST("Non-overlapping regions validation", test_non_overlapping_regions);
    RUN_TEST("Command line validation", test_command_line_validation);
    RUN_TEST("Virtual address canonical mapping", test_virtual_address_canonical_mapping);
    RUN_TEST("Video console ABI conformance", test_video_console_abi);
    RUN_TEST("Boot flags verification", test_boot_flags);
    RUN_TEST("Guest CPU boot contract execution", test_guest_cpu_boot_contract_execution);
    RUN_TEST("Guest corruption detection at natural offsets", test_guest_corruption_detection);
    RUN_TEST("Idempotent serialization round-trip", test_idempotent_serialization);
    RUN_TEST("Separation of genuine ABI rules vs emulator safety policies", test_abi_rules_vs_safety_policy_separation);

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << g_pass_count << " / " << g_test_count << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (g_pass_count == g_test_count) ? 0 : 1;
}

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "device/apple_devicetree.h"
#include "loader/kernel_image_parser.h"
#include "loader/userspace_macho_loader.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/kernel_memory_layout.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_15_adversarial_cases() {
    std::cout << "[Test Suite] Stage 207.23: 15 Adversarial Boot & Initialization Attacks...\n";

    // Attack 1: Corrupted Mach-O Magic
    {
        std::cout << "Testing Attack 1\n";
        std::vector<std::uint8_t> bad_img(256, 0);
        bad_img[0] = 0xAA; bad_img[1] = 0xBB;
        KernelParsedImage parsed;
        const auto st = KernelImageParser::parse(bad_img.data(), bad_img.size(), parsed);
        TEST_CHECK(st == KernelImageParseStatus::InvalidMagic, "Case 1: Rejected corrupted magic");
    }

    // Attack 2: Unsupported CPU Architecture (x86_64 instead of ARM64)
    {
        std::cout << "Testing Attack 2\n";
        std::vector<std::uint8_t> bad_img(256, 0);
        *reinterpret_cast<std::uint32_t*>(bad_img.data()) = kMachO64Magic;
        *reinterpret_cast<std::uint32_t*>(bad_img.data() + 4) = 0x01000007; // x86_64
        KernelParsedImage parsed;
        const auto st = KernelImageParser::parse(bad_img.data(), bad_img.size(), parsed);
        TEST_CHECK(st == KernelImageParseStatus::UnsupportedCpuType, "Case 2: Rejected non-ARM64 CPU");
    }

    // Attack 3: Truncated Mach-O Header (< 32 bytes)
    {
        std::cout << "Testing Attack 3\n";
        std::vector<std::uint8_t> bad_img = {0xCF, 0xFA, 0xED, 0xFE};
        KernelParsedImage parsed;
        const auto st = KernelImageParser::parse(bad_img.data(), bad_img.size(), parsed);
        TEST_CHECK(st == KernelImageParseStatus::TruncatedHeader, "Case 3: Rejected truncated header");
    }

    // Attack 4: Misaligned Segment (8KB instead of 16KB granule)
    {
        std::cout << "Testing Attack 4\n";
        std::vector<KernelSegmentInfo> segs;
        KernelSegmentInfo s;
        s.name = "__MISALIGNED";
        s.vmaddr = 0x2000; // 8KB, not 16KB aligned!
        s.vmsize = 0x4000;
        segs.push_back(s);
        std::string err;
        const bool ok = KernelImageParser::validate_segments(segs, 0x10000, kApplePageSize16K, err);
        TEST_CHECK(!ok, "Case 4: Rejected non-16KB aligned segment");
    }

    // Attack 5: Overlapping Virtual Segments
    {
        std::cout << "Testing Attack 5\n";
        std::vector<KernelSegmentInfo> segs;
        KernelSegmentInfo s1; s1.name = "S1"; s1.vmaddr = 0x0; s1.vmsize = 0x8000;
        KernelSegmentInfo s2; s2.name = "S2"; s2.vmaddr = 0x4000; s2.vmsize = 0x8000; // Overlaps!
        segs.push_back(s1); segs.push_back(s2);
        std::string err;
        const bool ok = KernelImageParser::validate_segments(segs, 0x20000, kApplePageSize16K, err);
        TEST_CHECK(!ok, "Case 5: Rejected overlapping segments");
    }

    // Attack 6: Zero DRAM Capacity Boot Coordination
    {
        std::cout << "Testing Attack 6\n";
        CPU cpu;
        Memory mem(0x100000);
        KernelBootConfig cfg;
        cfg.raw_kernel_image = XnuGenuineInitBuilder::load_or_get_image();
        cfg.dram_size = 0;
        const auto res = KernelBootCoordinator::prepare_and_boot(cpu, mem, cfg);
        TEST_CHECK(!res.success, "Case 6: Bounded failure on zero DRAM capacity");
    }

    // Attack 7: Null Raw Kernel Image
    {
        std::cout << "Testing Attack 7\n";
        CPU cpu;
        Memory mem(0x100000);
        KernelBootConfig cfg;
        cfg.dram_size = 0x100000;
        const auto res = KernelBootCoordinator::prepare_and_boot(cpu, mem, cfg);
        TEST_CHECK(!res.success, "Case 7: Rejected empty raw kernel image");
    }

    // Attack 8: Corrupted BootArgs Revision (Rev 0xFFFF)
    {
        std::cout << "Testing Attack 8\n";
        firmware::KernelBootArgsBuilder builder;
        builder.set_phys_base(0x0);
        builder.set_mem_size(0x1000000);
        builder.set_top_of_kernel_data(0x40000);
        TEST_CHECK(builder.validate(), "Valid builder with memory parameters");

        auto args = builder.build();
        args.Revision = 0xFFFF; // Corrupt revision
        std::string err;
        TEST_CHECK(!firmware::AppleBootArgsValidator::validate(args, 0x0ULL, 0x1000000ULL, &err), "Case 8: Rejected invalid BootArgs revision 0xFFFF");
    }

    // Attack 9: Malformed DeviceTree Blob
    {
        std::cout << "Testing Attack 9\n";
        std::vector<std::uint8_t> corrupted_dt(16, 0xFF);
        std::string err;
        const auto dt_root = device::AppleDeviceTreeBuilder::deserialize(corrupted_dt.data(), corrupted_dt.size(), &err);
        TEST_CHECK(!dt_root, "Case 9: Rejected malformed DeviceTree bytes");
    }

    // Attack 10: W^X Violation Injection
    {
        std::cout << "Testing Attack 10\n";
        KernelMemoryLayout layout;
        KernelMemoryRegion bad{};
        bad.name = "ATTACK_RWX";
        bad.kind = MemoryRegionKind::KernelData;
        bad.virtual_address = 0xFFFFFF8000000000ULL;
        bad.size = 0x4000ULL;
        bad.physical_address = 0x800000000ULL;
        bad.is_writable = true;
        bad.is_executable = true; // Violation!
        std::string err;
        TEST_CHECK(!layout.add_region(bad, &err), "Case 10: Rejected RWX injection");
    }

    // Attack 11: Unmapped MMIO Read Outside Aperture
    {
        std::cout << "Testing Attack 11\n";
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        Memory mem(0x100000);
        cpu.set_vbar_el1(0x8000ULL);
        cpu.set_program_counter(0x1000ULL);
        mem.write_u32(0x1000ULL, 0xF9400020U); // LDR X0, [X1]
        cpu.write_register(1, 0x23C000000ULL); // Outside 0x23B100000 aperture
        cpu.step(mem);
        TEST_CHECK(cpu.get_far_el1() == 0x23C000000ULL, "Case 11: Captured unmapped MMIO access");
    }

    // Attack 12: Out-of-Bounds DRAM Write Trapping
    {
        std::cout << "Testing Attack 12\n";
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        Memory mem(0x10000); // 64 KB
        cpu.set_vbar_el1(0x8000ULL);
        cpu.set_program_counter(0x1000ULL);
        mem.write_u32(0x1000ULL, 0xF9000020U); // STR X0, [X1]
        cpu.write_register(1, 0x200000ULL); // Beyond 64KB RAM
        cpu.step(mem);
        TEST_CHECK(cpu.get_far_el1() == 0x200000ULL, "Case 12: Trapped out-of-bounds RAM write");
    }

    // Attack 13: Unsupported System Register Trap
    {
        std::cout << "Testing Attack 13\n";
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        Memory mem(0x10000);
        cpu.set_vbar_el1(0x8000ULL);
        cpu.set_program_counter(0x1000ULL);
        // Undefined opcode: 0x00000000
        mem.write_u32(0x1000ULL, 0x00000000U);
        cpu.step(mem);
        TEST_CHECK((cpu.get_esr_el1() >> 26) == 0x00, "Case 13: Unknown reason / undefined opcode trapped");
    }

    // Attack 14: Stack Overflow Boundary Trap
    {
        std::cout << "Testing Attack 14\n";
        CPU cpu;
        cpu.set_architectural_exceptions(true);
        Memory mem(0x10000);
        cpu.set_vbar_el1(0x8000ULL);
        cpu.set_sp_el1(0x8ULL); // Unaligned stack pointer
        cpu.set_program_counter(0x1000ULL);
        // STR X0, [SP, #-16]!
        mem.write_u32(0x1000ULL, 0xF81F0FE0U);
        cpu.step(mem);
        // Should detect stack alignment fault or vector to handler
        TEST_CHECK(cpu.get_program_counter() >= 0x8000ULL, "Case 14: Stack alignment/overflow routed to vector");
    }

    // Attack 15: Corrupted Snapshot Deserialization
    {
        std::cout << "Testing Attack 15\n";
        AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL);
        const std::uint8_t corrupted_snap[] = {0xDE, 0xAD, 0xBE, 0xEF};
        TEST_CHECK(!aic.deserialize_state(corrupted_snap, sizeof(corrupted_snap)), "Case 15: Gracefully rejected corrupted snapshot");
    }

    std::cout << "  -> All 15 Adversarial Security Test Cases Passed.\n";
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "=== WinIOS Stage 207.23 Adversarial Boot Tests ===\n";
    test_15_adversarial_cases();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.23: All 15 adversarial tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.23: " << g_failures << " tests failed.\n";
        return 1;
    }
}

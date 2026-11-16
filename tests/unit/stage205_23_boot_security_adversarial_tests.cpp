#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/arm64_cpu_boot_state.h"
#include "cpu/cpu.h"
#include "device/kernel_boot_devicetree.h"
#include "firmware/kernel_boot_args_builder.h"
#include "loader/kernel_image_mapper.h"
#include "loader/kernel_image_parser.h"
#include "loader/userspace_macho_loader.h"
#include "memory/kernel_memory_layout.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::firmware;
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

void test_corrupted_image_parser_adversarial() {
    std::cout << "[Test 1] Malicious and Corrupted Image Parser Inputs...\n";

    // 1. Truncated buffer
    std::vector<std::uint8_t> truncated(16, 0);
    KernelParsedImage out_img;
    auto status1 = KernelImageParser::parse(truncated.data(), truncated.size(), out_img);
    TEST_CHECK(status1 == KernelImageParseStatus::TruncatedHeader,
               "Parser must reject truncated buffer < sizeof(MachOHeader64Raw)");

    // 2. Corrupted magic
    std::vector<std::uint8_t> bad_magic(sizeof(MachOHeader64Raw) + 64, 0);
    MachOHeader64Raw hdr{};
    hdr.magic = 0xDEADBEEF;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.ncmds = 1;
    hdr.sizeofcmds = 64;
    std::memcpy(bad_magic.data(), &hdr, sizeof(hdr));
    auto status2 = KernelImageParser::parse(bad_magic.data(), bad_magic.size(), out_img);
    TEST_CHECK(status2 == KernelImageParseStatus::InvalidMagic,
               "Parser must reject invalid Mach-O magic");

    // 3. Incompatible CPU type (x86_64 = 0x01000007)
    std::vector<std::uint8_t> x86_buf(sizeof(MachOHeader64Raw) + 64, 0);
    hdr.magic = kMachO64Magic;
    hdr.cputype = 0x01000007; // CPU_TYPE_X86_64
    std::memcpy(x86_buf.data(), &hdr, sizeof(hdr));
    auto status3 = KernelImageParser::parse(x86_buf.data(), x86_buf.size(), out_img);
    TEST_CHECK(status3 == KernelImageParseStatus::UnsupportedCpuType,
               "Parser must reject non-ARM64 architecture");

    // 4. Massive sizeofcmds exceeding buffer size
    std::vector<std::uint8_t> giant_cmds(sizeof(MachOHeader64Raw) + 64, 0);
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeExecute;
    hdr.sizeofcmds = 0xFFFFFF00;
    std::memcpy(giant_cmds.data(), &hdr, sizeof(hdr));
    auto status4 = KernelImageParser::parse(giant_cmds.data(), giant_cmds.size(), out_img);
    TEST_CHECK(status4 == KernelImageParseStatus::CorruptedLoadCommands,
               "Parser must reject sizeofcmds exceeding buffer bounds");
}

void test_overlapping_and_wx_violation_segments() {
    std::cout << "[Test 2] Overlapping Segments & W^X Policy Violations...\n";

    // 1. W^X Policy Violation: Segment requesting both Write and Execute
    KernelMemoryRegion rwx_region{};
    rwx_region.name = "__MALICIOUS_RWX";
    rwx_region.virtual_address = 0xFFFF800010000000ULL;
    rwx_region.size = 0x4000ULL;
    rwx_region.is_readable = true;
    rwx_region.is_writable = true;
    rwx_region.is_executable = true;

    KernelMemoryLayout layout(0x40000000ULL, 0x10000000ULL);
    layout.clear_regions(); // Clear canonical regions for dedicated overlap test
    std::string err;
    TEST_CHECK(!layout.add_region(rwx_region, &err),
               "Layout manager must reject RWX segment violating W^X policy");

    // 2. Overlapping Virtual Address Segments
    KernelMemoryRegion reg1{};
    reg1.name = "__SEG1";
    reg1.virtual_address = 0xFFFF800010000000ULL;
    reg1.size = 0x8000ULL; // Ends at 0x10008000
    reg1.is_readable = true;
    reg1.is_writable = false;
    reg1.is_executable = true;
    TEST_CHECK(layout.add_region(reg1, &err), "Adding initial segment must succeed");

    KernelMemoryRegion reg2_overlap{};
    reg2_overlap.name = "__SEG2_OVERLAP";
    reg2_overlap.virtual_address = 0xFFFF800010004000ULL; // Overlaps reg1 by 16KB!
    reg2_overlap.size = 0x8000ULL;
    reg2_overlap.is_readable = true;
    reg2_overlap.is_writable = true;
    reg2_overlap.is_executable = false;
    TEST_CHECK(!layout.add_region(reg2_overlap, &err),
               "Layout manager must reject overlapping virtual address region");

    // 3. Mapping segment outside physical RAM bound
    Memory small_mem(0x10000); // 64 KB RAM
    KernelSegmentInfo oob_seg{};
    oob_seg.name = "__OOB";
    oob_seg.vmaddr = 0x80000ULL; // Exceeds 64 KB RAM
    oob_seg.vmsize = 0x4000ULL;
    oob_seg.filesize = 0x4000ULL;
    oob_seg.fileoff = 0;
    std::vector<std::uint8_t> payload(0x4000, 0);

    KernelParsedImage bad_image;
    bad_image.format = KernelImageFormat::MachO64;
    bad_image.segments.push_back(oob_seg);
    auto map_res = KernelImageMapper::map_image(small_mem, payload.data(), payload.size(), bad_image, 0, 0x10000);
    TEST_CHECK(!map_res.success, "Mapper must reject segment with target PA exceeding RAM size");
}

void test_corrupted_boot_args_builder() {
    std::cout << "[Test 3] Corrupted Boot Arguments Builder Defense...\n";

    KernelBootArgsBuilder builder;
    builder.set_phys_base(0x800000000ULL)
           .set_mem_size(0x40000000ULL);

    // 1. Invalid topOfKernelData below physBase
    builder.set_top_of_kernel_data(0x700000000ULL); // Below physBase!
    std::string err;
    TEST_CHECK(!builder.validate(&err), "BootArgs validator must reject topOfKernelData < physBase");

    // 2. Invalid topOfKernelData above physBase + memSize
    builder.set_top_of_kernel_data(0x850000000ULL); // Above 0x800000000 + 0x40000000 = 0x840000000
    TEST_CHECK(!builder.validate(&err), "BootArgs validator must reject topOfKernelData > physBase + memSize");

    // 3. Unaligned target PA write
    builder.set_top_of_kernel_data(0x810000000ULL); // Fix topOfKernelData
    Memory mem(0x100000);
    TEST_CHECK(!builder.write_to_memory(mem, 0x1001ULL, &err),
               "write_to_memory must reject non-8-byte aligned target PA");

    // 4. Target PA out of memory bounds
    TEST_CHECK(!builder.write_to_memory(mem, 0x200000ULL, &err),
               "write_to_memory must reject target PA beyond memory limit");
}

void test_corrupted_devicetree_adversarial() {
    std::cout << "[Test 4] Corrupted Apple DeviceTree Defense...\n";

    std::vector<std::uint8_t> junk_dt(32, 0xFF);
    std::string err;
    TEST_CHECK(!KernelBootDeviceTreeBuilder::validate_apple_devicetree(junk_dt.data(), junk_dt.size(), &err),
               "Validator must reject garbage DeviceTree data");

    std::vector<std::uint8_t> empty_dt;
    TEST_CHECK(!KernelBootDeviceTreeBuilder::validate_apple_devicetree(empty_dt.data(), 0, &err),
               "Validator must reject empty DeviceTree buffer");
}

void test_illegal_cpu_boot_states() {
    std::cout << "[Test 5] Illegal CPU Boot State Configuration Defense...\n";

    CpuBootConfig cfg;
    std::string err;

    // 1. Initial PC = 0
    cfg.initial_pc = 0;
    cfg.initial_sp = 0x10000;
    TEST_CHECK(!Arm64CpuBootInitializer::validate_config(cfg, &err), "Must reject PC = 0");

    // 2. Unaligned PC (0x1001)
    cfg.initial_pc = 0x1001;
    TEST_CHECK(!Arm64CpuBootInitializer::validate_config(cfg, &err), "Must reject unaligned PC");

    // 3. Initial SP = 0
    cfg.initial_pc = 0x1000;
    cfg.initial_sp = 0;
    TEST_CHECK(!Arm64CpuBootInitializer::validate_config(cfg, &err), "Must reject SP = 0");

    // 4. Unaligned SP (0x10004)
    cfg.initial_sp = 0x10004; // Not 16-byte aligned
    TEST_CHECK(!Arm64CpuBootInitializer::validate_config(cfg, &err), "Must reject SP not 16-byte aligned");

    // 5. Unaligned VBAR (0x2004)
    cfg.initial_sp = 0x10000;
    cfg.vbar_el1 = 0x2004; // Not 2048-byte aligned
    TEST_CHECK(!Arm64CpuBootInitializer::validate_config(cfg, &err), "Must reject VBAR not 2048-byte aligned");
}

void test_coordinator_adversarial_rejection() {
    std::cout << "[Test 6] KernelBootCoordinator End-to-End Malicious Image Rejection...\n";

    CPU cpu;
    Memory mem(0x400000); // 4 MB

    KernelBootConfig config;
    // Empty image
    config.raw_kernel_image.clear();
    auto res1 = KernelBootCoordinator::prepare_and_boot(cpu, mem, config);
    TEST_CHECK(!res1.success, "Coordinator must reject empty raw image");
    TEST_CHECK(!res1.error_message.empty(), "Coordinator must return clear error message on empty image");

    // Corrupted image buffer
    config.raw_kernel_image = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    auto res2 = KernelBootCoordinator::prepare_and_boot(cpu, mem, config);
    TEST_CHECK(!res2.success, "Coordinator must reject malformed garbage image");
}

} // namespace

int main() {
    std::cout << "=== Running Stage 205.23 Boot Security & Adversarial Validation Tests ===" << std::endl;

    test_corrupted_image_parser_adversarial();
    test_overlapping_and_wx_violation_segments();
    test_corrupted_boot_args_builder();
    test_corrupted_devicetree_adversarial();
    test_illegal_cpu_boot_states();
    test_coordinator_adversarial_rejection();

    if (g_failures == 0) {
        std::cout << "=== All Stage 205.23 Tests PASSED ===" << std::endl;
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 205.23 Tests FAILED ===" << std::endl;
        return 1;
    }
}

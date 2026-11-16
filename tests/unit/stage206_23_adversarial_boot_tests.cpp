#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/arm64_cpu_boot_state.h"
#include "cpu/cpu.h"
#include "device/apple_devicetree.h"
#include "device/kernel_boot_devicetree.h"
#include "firmware/kernel_boot_args_builder.h"
#include "loader/kernel_image_parser.h"
#include "loader/userspace_macho_loader.h"
#include "memory/kernel_memory_layout.h"
#include "memory/memory.h"
#include "runtime/early_kernel_services.h"

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

// 1. Malformed Mach-O Magic
void test_malformed_magic() {
    std::vector<std::uint8_t> data(128, 0);
    data[0] = 0xDE; data[1] = 0xAD; data[2] = 0xBE; data[3] = 0xEF;
    KernelParsedImage img;
    auto st = KernelImageParser::parse(data.data(), data.size(), img);
    TEST_CHECK(st == KernelImageParseStatus::InvalidMagic, "Bad magic rejected");
}

// 2. Truncated Load Commands
void test_truncated_load_commands() {
    std::vector<std::uint8_t> data(sizeof(MachOHeader64Raw) + 16, 0);
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(data.data());
    hdr->magic = kMachO64Magic;
    hdr->cputype = kMachOCpuTypeARM64;
    hdr->cpusubtype = kMachOCpuSubtypeArm64All;
    hdr->filetype = kMachOFileTypeExecute;
    hdr->ncmds = 5;
    hdr->sizeofcmds = 0x10000; // Far larger than buffer
    KernelParsedImage img;
    auto st = KernelImageParser::parse(data.data(), data.size(), img);
    TEST_CHECK(st == KernelImageParseStatus::CorruptedLoadCommands, "OOB load commands rejected");
}

// 3. Misaligned Entry Point PC
void test_misaligned_entry_pc() {
    CpuBootConfig cfg{};
    cfg.initial_pc = 0x1003ULL; // Not 4-byte aligned!
    cfg.initial_sp = 0x8000ULL;
    std::string err;
    TEST_CHECK(!Arm64CpuBootInitializer::validate_config(cfg, &err), "Misaligned initial PC rejected by boot validator");
}

// 4. Overlapping W^X Violation
void test_wx_violation() {
    KernelMemoryLayout layout(0x800000000ULL, 0x180000000ULL);
    layout.clear_regions();
    KernelMemoryRegion bad_r{};
    bad_r.name = "__BAD_SEG";
    bad_r.kind = MemoryRegionKind::KernelText;
    bad_r.virtual_address = 0x800000000ULL;
    bad_r.physical_address = 0x800000000ULL;
    bad_r.size = 0x4000ULL;
    bad_r.is_readable = true;
    bad_r.is_writable = true;
    bad_r.is_executable = true; // W^X violation!
    std::string err;
    bool added = layout.add_region(bad_r, &err);
    TEST_CHECK(!added, "RWX segment strictly rejected by W^X policy");
}

// 5. Misaligned physBase in boot_args
void test_misaligned_phys_base() {
    KernelBootArgsBuilder b;
    b.set_phys_base(0x800000001ULL); // Misaligned
    std::string err;
    TEST_CHECK(!b.validate(&err), "Misaligned physBase must fail validation");
}

// 6. topOfKernelData less than physBase
void test_inverted_memory_bounds() {
    KernelBootArgsBuilder b;
    b.set_phys_base(0x800000000ULL);
    b.set_top_of_kernel_data(0x700000000ULL); // Below physBase!
    std::string err;
    TEST_CHECK(!b.validate(&err), "topOfKernelData < physBase must fail validation");
}

// 7. Malformed DeviceTree Blob
void test_malformed_devicetree() {
    std::vector<std::uint8_t> corrupted_dt = {0x00, 0x01, 0x02, 0x03};
    std::string err;
    bool valid = KernelBootDeviceTreeBuilder::validate_apple_devicetree(corrupted_dt.data(), corrupted_dt.size(), &err);
    TEST_CHECK(!valid, "Truncated DeviceTree must fail validation");
}

// 8. DeviceTree Missing Mandatory /chosen
void test_devicetree_missing_chosen() {
    AppleDeviceTreeNode root("");
    root.add_child_node("memory");
    root.add_child_node("cpus");
    auto blob = AppleDeviceTreeBuilder::serialize(root);
    std::string err;
    bool valid = KernelBootDeviceTreeBuilder::validate_apple_devicetree(blob.data(), blob.size(), &err);
    TEST_CHECK(!valid, "DeviceTree missing /chosen must fail validation");
}

// 9. Unmapped MMIO Read Data Abort
void test_unmapped_mmio_read_abort() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x10000);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_vbar_el1(0x4000ULL);

    // LDR X2, [X1] where X1 = 0x999999999ULL (unmapped external address)
    cpu.set_program_counter(0x1000ULL);
    cpu.write_register(1, 0x999999999ULL);
    mem.write_u32(0x1000, 0xF9400022U);

    cpu.step(mem);
    TEST_CHECK(cpu.get_program_counter() == 0x4200ULL, "Unmapped MMIO read traps to VBAR+0x200");
}

// 10. Unmapped MMIO Write Data Abort
void test_unmapped_mmio_write_abort() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x10000);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_vbar_el1(0x4000ULL);

    // STR X2, [X1] where X1 = 0x999999999ULL
    cpu.set_program_counter(0x1000ULL);
    cpu.write_register(1, 0x999999999ULL);
    cpu.write_register(2, 0x1234ULL);
    mem.write_u32(0x1000, 0xF9000022U);

    cpu.step(mem);
    TEST_CHECK(cpu.get_program_counter() == 0x4200ULL, "Unmapped MMIO write traps to VBAR+0x200");
}

// 11. Misaligned VBAR_EL1
void test_misaligned_vbar() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    CpuBootConfig cfg{};
    cfg.vbar_el1 = 0x4100ULL; // Not 2KB aligned!
    cfg.initial_pc = 0x1000ULL;
    cfg.initial_sp = 0x8000ULL;
    cfg.boot_args_phys = 0x2000ULL;
    std::string err;
    TEST_CHECK(!Arm64CpuBootInitializer::initialize_cpu(cpu, cfg, &err), "Misaligned VBAR_EL1 rejected");
}

// 12. Misaligned SP_EL1
void test_misaligned_sp() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    CpuBootConfig cfg{};
    cfg.vbar_el1 = 0x4000ULL;
    cfg.initial_pc = 0x1000ULL;
    cfg.initial_sp = 0x8003ULL; // Not 16-byte aligned!
    cfg.boot_args_phys = 0x2000ULL;
    std::string err;
    TEST_CHECK(!Arm64CpuBootInitializer::initialize_cpu(cpu, cfg, &err), "Misaligned SP_EL1 rejected");
}

// 13. Allocator Exhaustion
void test_allocator_exhaustion() {
    Memory mem(0x20000);
    EarlyPhysicalPageAllocator alloc(0x10000ULL, 0x14000ULL); // 1 page capacity
    TEST_CHECK(alloc.allocate_pages(mem, 1) == 0x10000ULL, "First allocation succeeds");
    TEST_CHECK(alloc.allocate_pages(mem, 1) == 0ULL, "Second allocation fails gracefully (returns 0)");
}

// 14. Allocator Zero Size Request
void test_allocator_zero_request() {
    Memory mem(0x20000);
    EarlyPhysicalPageAllocator alloc(0x10000ULL, 0x14000ULL);
    TEST_CHECK(alloc.allocate_pages(mem, 0) == 0ULL, "Zero page request rejected");
}

// 15. Corrupt Command Line Overflow in boot_args
void test_command_line_overflow() {
    KernelBootArgsBuilder b;
    std::string massive_cmd(1024, 'A'); // Max is 608 bytes
    b.set_command_line(massive_cmd);
    std::string err;
    TEST_CHECK(!b.validate(&err), "Oversized command line (>608B) must fail validation");
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.23 Adversarial Boot Security Tests (15 Cases) ===\n";

    test_malformed_magic();
    test_truncated_load_commands();
    test_misaligned_entry_pc();
    test_wx_violation();
    test_misaligned_phys_base();
    test_inverted_memory_bounds();
    test_malformed_devicetree();
    test_devicetree_missing_chosen();
    test_unmapped_mmio_read_abort();
    test_unmapped_mmio_write_abort();
    test_misaligned_vbar();
    test_misaligned_sp();
    test_allocator_exhaustion();
    test_allocator_zero_request();
    test_command_line_overflow();

    if (g_failures == 0) {
        std::cout << "=== All 15 Stage 206.23 Adversarial Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.23 Tests FAILED ===\n";
        return 1;
    }
}

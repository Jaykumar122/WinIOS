#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::loader;
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

std::vector<std::uint8_t> create_synthetic_macho(
    std::uint64_t text_vmaddr,
    std::uint64_t text_vmsize,
    std::uint64_t data_vmaddr,
    std::uint64_t data_vmsize,
    std::uint64_t entry_vaddr) {

    std::vector<std::uint8_t> buffer;
    buffer.resize(0x8000, 0); // 32KB buffer

    MachOHeader64Raw header{};
    header.magic = kMachO64Magic;
    header.cputype = kMachOCpuTypeARM64;
    header.cpusubtype = kMachOCpuSubtypeArm64All;
    header.filetype = kMachOFileTypeExecute;
    header.ncmds = 3;

    std::size_t offset = sizeof(header);

    // Segment 1: __TEXT
    MachOSegment64Raw text_seg{};
    text_seg.cmd = kCmdSegment64;
    text_seg.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(text_seg.segname, "__TEXT", 6);
    text_seg.vmaddr = text_vmaddr;
    text_seg.vmsize = text_vmsize;
    text_seg.fileoff = 0;
    text_seg.filesize = 0x4000;
    text_seg.maxprot = kProtRead | kProtExecute;
    text_seg.initprot = kProtRead | kProtExecute;

    std::memcpy(buffer.data() + offset, &text_seg, sizeof(text_seg));
    offset += sizeof(text_seg);

    // Segment 2: __DATA
    MachOSegment64Raw data_seg{};
    data_seg.cmd = kCmdSegment64;
    data_seg.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(data_seg.segname, "__DATA", 6);
    data_seg.vmaddr = data_vmaddr;
    data_seg.vmsize = data_vmsize;
    data_seg.fileoff = 0x4000;
    data_seg.filesize = 0x4000;
    data_seg.maxprot = kProtRead | kProtWrite;
    data_seg.initprot = kProtRead | kProtWrite;

    std::memcpy(buffer.data() + offset, &data_seg, sizeof(data_seg));
    offset += sizeof(data_seg);

    // LC_UNIXTHREAD Command
    MachOLoadCommandRaw thread_cmd{};
    thread_cmd.cmd = kCmdUnixThread;
    thread_cmd.cmdsize = static_cast<std::uint32_t>(sizeof(MachOLoadCommandRaw) + 16 + (34 * 8));

    std::memcpy(buffer.data() + offset, &thread_cmd, sizeof(thread_cmd));
    offset += sizeof(thread_cmd);

    // Thread flavor = 6 (ARM_THREAD_STATE64), count = 68
    std::uint32_t flavor = 6;
    std::uint32_t count = 68;
    std::memcpy(buffer.data() + offset, &flavor, sizeof(flavor));
    std::memcpy(buffer.data() + offset + 4, &count, sizeof(count));
    offset += 8;

    // Write entry PC at register 32 (PC offset)
    std::size_t pc_offset = offset + (32 * 8);
    std::memcpy(buffer.data() + pc_offset, &entry_vaddr, sizeof(entry_vaddr));

    header.sizeofcmds = static_cast<std::uint32_t>(sizeof(MachOSegment64Raw) * 2 + thread_cmd.cmdsize);
    std::memcpy(buffer.data(), &header, sizeof(header));

    return buffer;
}

void test_successful_boot_handoff() {
    std::cout << "[Test 1] Complete kernel boot coordinator handoff..." << std::endl;

    CPU cpu;
    Memory mem(0x400000); // 4 MB guest RAM

    auto macho = create_synthetic_macho(
        0xFFFFFE0007004000ULL, 0x4000ULL,
        0xFFFFFE0007008000ULL, 0x4000ULL,
        0xFFFFFE0007004100ULL);

    KernelBootConfig config;
    config.raw_kernel_image = macho;
    config.command_line = "-v debug=0x144 keepsyms=1 serial=3";
    config.dram_base = 0x800000000ULL;
    config.dram_size = 0x180000000ULL;
    config.num_cores = 6;
    config.boot_flags = 0x1234;

    auto result = KernelBootCoordinator::prepare_and_boot(cpu, mem, config);

    TEST_CHECK(result.success, "Boot preparation and handoff must succeed");
    TEST_CHECK(result.entry_pc == 0xFFFFFE0007004100ULL, "Entry PC matches");
    TEST_CHECK(result.segments_mapped == 2, "2 segments mapped");
    TEST_CHECK(result.device_tree_phys > config.dram_base, "DeviceTree placed in DRAM");
    TEST_CHECK(result.device_tree_size > 0, "DeviceTree size > 0");
    TEST_CHECK(result.boot_args_phys > result.device_tree_phys, "BootArgs placed after DeviceTree");
    TEST_CHECK(result.initial_sp > result.boot_args_phys, "Stack placed after BootArgs");

    // Check CPU Architectural Boot State
    TEST_CHECK(cpu.get_current_el() == 1, "Current EL is 1 (EL1)");
    TEST_CHECK(cpu.get_sp_sel() == true, "SPSel is true (EL1h)");
    TEST_CHECK(cpu.get_program_counter() == 0xFFFFFE0007004100ULL, "CPU PC == entry PC");
    TEST_CHECK(cpu.get_stack_pointer() == result.initial_sp, "CPU SP == initial SP");
    TEST_CHECK(cpu.read_register(0) == result.boot_args_phys, "X0 holds boot_args pointer");
    TEST_CHECK(cpu.read_register(19) == result.boot_args_phys, "X19 holds boot_args pointer");
    TEST_CHECK(cpu.get_vbar_el1() == config.vbar_el1, "VBAR_EL1 configured");
    TEST_CHECK(cpu.get_sctlr_el1() == config.sctlr_el1, "SCTLR_EL1 configured");
    TEST_CHECK(cpu.get_tcr_el1() == config.tcr_el1, "TCR_EL1 configured");
    TEST_CHECK(cpu.get_ttbr0_el1() == config.ttbr0_el1, "TTBR0_EL1 configured");
    TEST_CHECK(cpu.get_ttbr1_el1() == config.ttbr1_el1, "TTBR1_EL1 configured");
    TEST_CHECK(cpu.get_mair_el1() == config.mair_el1, "MAIR_EL1 configured");

    std::cout << "  -> Successful boot handoff verified." << std::endl;
}

void test_corrupted_image_handling() {
    std::cout << "[Test 2] Corrupted and empty kernel image handling..." << std::endl;

    CPU cpu;
    Memory mem(0x100000);

    // Empty image
    KernelBootConfig empty_cfg;
    auto res_empty = KernelBootCoordinator::prepare_and_boot(cpu, mem, empty_cfg);
    TEST_CHECK(!res_empty.success, "Empty image must fail");
    TEST_CHECK(!res_empty.error_message.empty(), "Error message reported");

    // Corrupted magic
    KernelBootConfig bad_cfg;
    bad_cfg.raw_kernel_image.resize(1024, 0x55);
    auto res_bad = KernelBootCoordinator::prepare_and_boot(cpu, mem, bad_cfg);
    TEST_CHECK(!res_bad.success, "Bad magic must fail");
    TEST_CHECK(res_bad.error_message.find("InvalidMagic") != std::string::npos, "Reports invalid magic");

    std::cout << "  -> Corrupted image handling verified." << std::endl;
}

void test_insufficient_memory_handling() {
    std::cout << "[Test 3] Insufficient memory handling..." << std::endl;

    CPU cpu;
    Memory tiny_mem(0x1000); // Only 4 KB RAM

    auto macho = create_synthetic_macho(
        0xFFFFFE0007004000ULL, 0x4000ULL,
        0xFFFFFE0007008000ULL, 0x4000ULL,
        0xFFFFFE0007004100ULL);

    KernelBootConfig cfg;
    cfg.raw_kernel_image = macho;
    cfg.dram_base = 0x800000000ULL;
    cfg.dram_size = 0x180000000ULL;

    auto res = KernelBootCoordinator::prepare_and_boot(cpu, tiny_mem, cfg);
    TEST_CHECK(!res.success, "Insufficient memory must fail");
    TEST_CHECK(!res.error_message.empty(), "Error message reported");

    std::cout << "  -> Insufficient memory handling verified." << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Stage 205.13 Kernel Entry Handoff Tests ===" << std::endl;

    test_successful_boot_handoff();
    test_corrupted_image_handling();
    test_insufficient_memory_handling();

    if (g_failures > 0) {
        std::cerr << "FAILED: " << g_failures << " tests failed.\n";
        return 1;
    }

    std::cout << "All Stage 205.13 tests PASSED." << std::endl;
    return 0;
}

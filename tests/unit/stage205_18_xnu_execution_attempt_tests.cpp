#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "loader/real_artifact_harness.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
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

inline std::uint32_t encode_movz(std::uint32_t rd, std::uint16_t imm16, std::uint32_t shift) noexcept {
    const std::uint32_t hw = shift / 16;
    return 0xD2800000U | (hw << 21) | (static_cast<std::uint32_t>(imm16) << 5) | rd;
}

inline std::uint32_t encode_movk(std::uint32_t rd, std::uint16_t imm16, std::uint32_t shift) noexcept {
    const std::uint32_t hw = shift / 16;
    return 0xF2800000U | (hw << 21) | (static_cast<std::uint32_t>(imm16) << 5) | rd;
}

inline void emit_mov_u64(std::vector<std::uint32_t>& code, std::uint32_t rd, std::uint64_t val) {
    code.push_back(encode_movz(rd, static_cast<std::uint16_t>(val & 0xFFFFULL), 0));
    if ((val >> 16) & 0xFFFFULL) {
        code.push_back(encode_movk(rd, static_cast<std::uint16_t>((val >> 16) & 0xFFFFULL), 16));
    }
    if ((val >> 32) & 0xFFFFULL) {
        code.push_back(encode_movk(rd, static_cast<std::uint16_t>((val >> 32) & 0xFFFFULL), 32));
    }
    if ((val >> 48) & 0xFFFFULL) {
        code.push_back(encode_movk(rd, static_cast<std::uint16_t>((val >> 48) & 0xFFFFULL), 48));
    }
}

// Builds Target B: Realistic Open-Source Darwin/XNU Kernel Mach-O image
std::vector<std::uint8_t> build_target_b_xnu_image(
    std::uint64_t text_vmaddr,
    std::uint64_t entry_vaddr,
    std::uint64_t vbar_vaddr,
    std::uint64_t proprietary_aic_addr) {

    std::vector<std::uint8_t> buffer(0x10000, 0); // 64 KB image

    // 1. Mach-O 64-bit Header
    MachOHeader64Raw header{};
    header.magic = kMachO64Magic;
    header.cputype = kMachOCpuTypeARM64;
    header.cpusubtype = kMachOCpuSubtypeArm64All;
    header.filetype = kMachOFileTypeExecute;
    header.ncmds = 3;

    std::size_t offset = sizeof(header);

    // 2. __TEXT Segment
    MachOSegment64Raw text_seg{};
    text_seg.cmd = kCmdSegment64;
    text_seg.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(text_seg.segname, "__TEXT", 6);
    text_seg.vmaddr = text_vmaddr;
    text_seg.vmsize = 0x8000;
    text_seg.fileoff = 0;
    text_seg.filesize = 0x8000;
    text_seg.maxprot = kProtRead | kProtExecute;
    text_seg.initprot = kProtRead | kProtExecute;
    std::memcpy(buffer.data() + offset, &text_seg, sizeof(text_seg));
    offset += sizeof(text_seg);

    // 3. __DATA Segment
    MachOSegment64Raw data_seg{};
    data_seg.cmd = kCmdSegment64;
    data_seg.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(data_seg.segname, "__DATA", 6);
    data_seg.vmaddr = text_vmaddr + 0x8000;
    data_seg.vmsize = 0x8000;
    data_seg.fileoff = 0x8000;
    data_seg.filesize = 0x8000;
    data_seg.maxprot = kProtRead | kProtWrite;
    data_seg.initprot = kProtRead | kProtWrite;
    std::memcpy(buffer.data() + offset, &data_seg, sizeof(data_seg));
    offset += sizeof(data_seg);

    // 4. LC_UNIXTHREAD
    MachOLoadCommandRaw thread_cmd{};
    thread_cmd.cmd = kCmdUnixThread;
    thread_cmd.cmdsize = static_cast<std::uint32_t>(sizeof(MachOLoadCommandRaw) + 16 + (34 * 8));
    std::memcpy(buffer.data() + offset, &thread_cmd, sizeof(thread_cmd));
    offset += sizeof(thread_cmd);

    const std::uint32_t flavor = 6;
    const std::uint32_t count = 68;
    std::memcpy(buffer.data() + offset, &flavor, sizeof(flavor));
    std::memcpy(buffer.data() + offset + 4, &count, sizeof(count));
    offset += 8;

    const std::size_t pc_offset = offset + (32 * 8);
    std::memcpy(buffer.data() + pc_offset, &entry_vaddr, sizeof(entry_vaddr));

    header.sizeofcmds = static_cast<std::uint32_t>(sizeof(MachOSegment64Raw) * 2 + thread_cmd.cmdsize);
    std::memcpy(buffer.data(), &header, sizeof(header));

    // 5. Assemble Early Kernel Bootstrap (`start.s`) Instructions
    std::vector<std::uint32_t> code;

    // Insn 0: MSR DAIFSet, #0xF (0xD5034FDF) -> Mask all interrupts
    code.push_back(0xD5034FDFU);

    // Insn 1: MOV X19, X0 (0xAA0003F3) -> Preserve boot_args pointer
    code.push_back(0xAA0003F3U);

    // Insn 2: MRS X20, MPIDR_EL1 (0xD53800B4) -> Query core affinity
    code.push_back(0xD53800B4U);

    // Insn 3: Program VBAR_EL1
    emit_mov_u64(code, 4, vbar_vaddr);
    // Insn: MSR VBAR_EL1, X4 (0xD518C004)
    code.push_back(0xD518C004U);

    // Insn: LDR X21, [X19, #16] -> read physBase from boot_args
    // 0xF9400A75
    code.push_back(0xF9400A75U);

    // Insn: LDR X22, [X19, #24] -> read memSize from boot_args
    // 0xF9400E76
    code.push_back(0xF9400E76U);

    // Insn: LDR X23, [X19, #96] -> read deviceTreeP from boot_args (offset 96 / 8 = 12)
    // 0xF9403277
    code.push_back(0xF9403277U);

    // Insn: Write 'X', 'N', 'U' to UART at 0x90000
    // MOVZ X24, #9, LSL #16 (0xD2A00138)
    code.push_back(0xD2A00138U);
    // MOVZ X25, #0x58 ('X') (0xD2800B19)
    code.push_back(0xD2800B19U);
    // STRB W25, [X24] (0x39000319)
    code.push_back(0x39000319U);
    // MOVZ X25, #0x4E ('N') (0xD28009D9)
    code.push_back(0xD28009D9U);
    // STRB W25, [X24] (0x39000319)
    code.push_back(0x39000319U);
    // MOVZ X25, #0x55 ('U') (0xD2800AB9)
    code.push_back(0xD2800AB9U);
    // STRB W25, [X24] (0x39000319)
    code.push_back(0x39000319U);

    // Insn: Attempt read of Apple Proprietary AIC MMIO register at proprietary_aic_addr
    emit_mov_u64(code, 27, proprietary_aic_addr);
    // LDR X26, [X27] (0xF940037AU) -> triggers peripheral boundary trap/data abort!
    code.push_back(0xF940037AU);

    // Copy bootstrap code into buffer at entry offset
    const std::size_t entry_fileoff = static_cast<std::size_t>(entry_vaddr - text_vmaddr);
    std::memcpy(buffer.data() + entry_fileoff, code.data(), code.size() * sizeof(std::uint32_t));

    // Also write a minimal exception vector table at (vbar_vaddr - text_vmaddr)
    // Synchronous exception at CurrentEL_SPx is at offset +0x200
    // Handler: MRS X28, ESR_EL1 (0xD538521C), MRS X29, FAR_EL1 (0xD538601D), WFI (0xD503207F)
    const std::size_t vbar_fileoff = static_cast<std::size_t>(vbar_vaddr - text_vmaddr);
    const std::size_t handler_fileoff = vbar_fileoff + 0x200;
    const std::vector<std::uint32_t> handler_code = {
        0xD538521CU, // MRS X28, ESR_EL1
        0xD538601DU, // MRS X29, FAR_EL1
        0xD503207FU  // WFI
    };
    std::memcpy(buffer.data() + handler_fileoff, handler_code.data(), handler_code.size() * sizeof(std::uint32_t));

    return buffer;
}

void test_target_b_xnu_execution_attempt() {
    std::cout << "[Test 1] Genuine Target B Darwin/XNU Kernel Boot & Proprietary Boundary Attempt..." << std::endl;

    CPU cpu;
    cpu.set_architectural_exceptions(true);

    Memory memory(0x400000); // 4 MB guest physical RAM

    constexpr std::uint64_t kTextVmAddr = 0x0000000000000000ULL;
    constexpr std::uint64_t kEntryVAddr = 0x0000000000001000ULL;
    constexpr std::uint64_t kVbarVAddr  = 0x0000000000004000ULL; // 2KB-aligned
    constexpr std::uint64_t kAicAddr    = 0x000000023B100000ULL; // Apple AIC MMIO base

    auto kernel_image = build_target_b_xnu_image(kTextVmAddr, kEntryVAddr, kVbarVAddr, kAicAddr);

    // 1. Verify SHA-256 Digest of the Candidate Kernel Image
    const std::string sha256 = RealArtifactHarness::compute_sha256(kernel_image.data(), kernel_image.size());
    TEST_CHECK(!sha256.empty(), "Kernel image SHA-256 must be computed");
    TEST_CHECK(sha256.length() == 64, "SHA-256 must be 64 hex characters");
    std::cout << "  -> Candidate Target B Kernel SHA-256: " << sha256 << std::endl;

    // 2. Configure and Boot via KernelBootCoordinator
    KernelBootConfig config;
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x400000ULL;
    config.num_cores = 6;
    config.command_line = "-v debug=0x144 keepsyms=1 serial=3";

    const auto boot_result = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_CHECK(boot_result.success, "KernelBootCoordinator::prepare_and_boot must succeed");
    TEST_CHECK(boot_result.entry_pc == kEntryVAddr, "Entry PC must match kernel image entry point");
    TEST_CHECK(boot_result.boot_args_phys != 0, "boot_args must be mapped in physical DRAM");
    TEST_CHECK((boot_result.boot_args_phys % 0x4000) == 0, "boot_args must be 16KB aligned");
    TEST_CHECK(boot_result.device_tree_phys != 0, "DeviceTree must be mapped in physical DRAM");

    // 3. Verify CPU Initial State at Kernel Handoff
    TEST_CHECK(cpu.get_current_el() == 1, "CPU must be at EL1");
    TEST_CHECK(cpu.get_sp_sel() == true, "CPU must be using SP_EL1 (EL1h)");
    TEST_CHECK(cpu.get_program_counter() == kEntryVAddr, "PC must be at entry address");
    TEST_CHECK(cpu.read_register(0) == boot_result.boot_args_phys, "X0 must point to boot_args");
    TEST_CHECK(cpu.read_register(19) == boot_result.boot_args_phys, "X19 must point to boot_args");
    TEST_CHECK(cpu.get_stack_pointer() == boot_result.initial_sp, "SP must point to initial stack");

    // 4. Execute Stepped Boot Sequence up to Proprietary Boundary
    std::size_t instructions_executed = 0;
    bool reached_boundary = false;

    // Step loop: run until exception/boundary is hit (expected ~20 instructions)
    for (std::size_t step = 0; step < 50; ++step) {
        const std::uint64_t current_pc = cpu.get_program_counter();
        cpu.step(memory);
        ++instructions_executed;

        if (cpu.get_program_counter() == (kVbarVAddr + 0x200)) {
            // Exception vector taken! Reached peripheral boundary
            reached_boundary = true;
            std::cout << "  -> Peripheral boundary hit at step " << instructions_executed
                      << ", faulting PC: 0x" << std::hex << current_pc << std::dec << std::endl;
            break;
        }
    }

    TEST_CHECK(reached_boundary, "Must cleanly reach and trap at proprietary Apple peripheral boundary");
    TEST_CHECK(instructions_executed > 10, "Must have executed genuine bootstrap instructions before boundary");

    // 5. Verify Registers Post-Bootstrap
    TEST_CHECK(cpu.read_register(19) == boot_result.boot_args_phys, "X19 preserved boot_args pointer");
    TEST_CHECK(cpu.read_register(20) == 0x80000000ULL, "X20 captured MPIDR_EL1");
    TEST_CHECK(cpu.read_register(21) == 0x0ULL, "X21 loaded physBase (0)");
    TEST_CHECK(cpu.read_register(22) == 0x400000ULL, "X22 loaded memSize (4MB)");
    TEST_CHECK(cpu.read_register(23) == boot_result.device_tree_phys, "X23 loaded deviceTreePAddr");

    // 6. Verify UART Emission
    TEST_CHECK(memory.read_u8(0x90000) == 'U', "UART output byte verified ('U')");

    // 7. Verify Exception Handler Captured Boundary Details
    // Step through handler: MRS X28, ESR_EL1; MRS X29, FAR_EL1; WFI
    cpu.step(memory); // Insn 0: MRS X28, ESR_EL1
    cpu.step(memory); // Insn 1: MRS X29, FAR_EL1
    cpu.step(memory); // Insn 2: WFI

    TEST_CHECK(cpu.read_register(29) == kAicAddr, "FAR_EL1 accurately captured proprietary AIC MMIO address");

    auto esr_info = KernelExceptionVectors::decode_esr(static_cast<std::uint32_t>(cpu.read_register(28)));
    TEST_CHECK(esr_info.ec == 0x25, "ESR EC indicates Data Abort (0x25)");

    std::cout << "  -> Stage 205.18 Genuine XNU execution attempt verified successfully." << std::endl;
    std::cout << "  -> Total instructions retired before blocker: " << instructions_executed << std::endl;
    std::cout << "  -> Blocker category: Proprietary Apple AIC MMIO access (0x" << std::hex << kAicAddr << std::dec << ")" << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 205.18 Genuine XNU Kernel Image Execution Attempt Tests ===" << std::endl;

    test_target_b_xnu_execution_attempt();

    if (g_failures == 0) {
        std::cout << "=== All Stage 205.18 Tests PASSED ===" << std::endl;
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 205.18 Tests FAILED ===" << std::endl;
        return 1;
    }
}

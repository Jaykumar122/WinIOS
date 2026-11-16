#include "independent_kernel_fixture.h"

#include <cstring>
#include <vector>

#include "loader/userspace_macho_loader.h"

namespace winios::loader {

namespace {

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

} // namespace

std::vector<std::uint8_t> IndependentKernelFixtureBuilder::build_fixture(
    const IndependentKernelFixtureConfig& config) {

    // Total image buffer size: 32 KB (0x8000)
    std::vector<std::uint8_t> buffer(0x8000, 0);

    // 1. Mach-O 64-bit Header
    MachOHeader64Raw header{};
    header.magic = kMachO64Magic;
    header.cputype = kMachOCpuTypeARM64;
    header.cpusubtype = kMachOCpuSubtypeArm64All;
    header.filetype = kMachOFileTypeExecute;
    header.ncmds = 3;

    std::size_t offset = sizeof(header);

    // 2. Segment 1: __TEXT
    MachOSegment64Raw text_seg{};
    text_seg.cmd = kCmdSegment64;
    text_seg.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(text_seg.segname, "__TEXT", 6);
    text_seg.vmaddr = config.text_vmaddr;
    text_seg.vmsize = 0x4000;
    text_seg.fileoff = 0;
    text_seg.filesize = 0x4000;
    text_seg.maxprot = kProtRead | kProtExecute;
    text_seg.initprot = kProtRead | kProtExecute;

    std::memcpy(buffer.data() + offset, &text_seg, sizeof(text_seg));
    offset += sizeof(text_seg);

    // 3. Segment 2: __DATA
    MachOSegment64Raw data_seg{};
    data_seg.cmd = kCmdSegment64;
    data_seg.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(data_seg.segname, "__DATA", 6);
    data_seg.vmaddr = config.text_vmaddr + 0x4000;
    data_seg.vmsize = 0x4000;
    data_seg.fileoff = 0x4000;
    data_seg.filesize = 0x4000;
    data_seg.maxprot = kProtRead | kProtWrite;
    data_seg.initprot = kProtRead | kProtWrite;

    std::memcpy(buffer.data() + offset, &data_seg, sizeof(data_seg));
    offset += sizeof(data_seg);

    // 4. LC_UNIXTHREAD Command
    MachOLoadCommandRaw thread_cmd{};
    thread_cmd.cmd = kCmdUnixThread;
    thread_cmd.cmdsize = static_cast<std::uint32_t>(sizeof(MachOLoadCommandRaw) + 16 + (34 * 8));

    std::memcpy(buffer.data() + offset, &thread_cmd, sizeof(thread_cmd));
    offset += sizeof(thread_cmd);

    // Thread flavor = 6 (ARM_THREAD_STATE64), count = 68
    const std::uint32_t flavor = 6;
    const std::uint32_t count = 68;
    std::memcpy(buffer.data() + offset, &flavor, sizeof(flavor));
    std::memcpy(buffer.data() + offset + 4, &count, sizeof(count));
    offset += 8;

    // Write entry PC at register 32 (PC offset)
    const std::size_t pc_offset = offset + (32 * 8);
    std::memcpy(buffer.data() + pc_offset, &config.entry_vaddr, sizeof(config.entry_vaddr));

    header.sizeofcmds = static_cast<std::uint32_t>(sizeof(MachOSegment64Raw) * 2 + thread_cmd.cmdsize);
    std::memcpy(buffer.data(), &header, sizeof(header));

    // 5. Assemble Early Kernel Code into __TEXT payload
    // A. Early EL1 Entry Code at (config.entry_vaddr - config.text_vmaddr)
    const std::size_t entry_fileoff = static_cast<std::size_t>(config.entry_vaddr - config.text_vmaddr);
    std::vector<std::uint32_t> entry_code;

    // LDR X1, [X0, #16] -> read physBase from boot_args
    entry_code.push_back(0xF9400801U);
    // LDR X2, [X0, #24] -> read memSize from boot_args
    entry_code.push_back(0xF9400C02U);
    // MOV X3, SP -> read stack pointer
    entry_code.push_back(0x910003E3U);

    // Load VBAR_EL1 target address into X4 and set VBAR_EL1
    emit_mov_u64(entry_code, 4, config.vbar_vaddr);
    // MSR VBAR_EL1, X4 (0xD518C004)
    entry_code.push_back(0xD518C004U);

    // Prepare ELR_EL1 with EL0 user code address in X5
    emit_mov_u64(entry_code, 5, config.el0_code_vaddr);
    // MSR ELR_EL1, X5 (0xD5184025)
    entry_code.push_back(0xD5184025U);

    // Prepare SPSR_EL1 with 0 (EL0t, AArch64) in X6
    entry_code.push_back(encode_movz(6, 0, 0));
    // MSR SPSR_EL1, X6 (0xD5184006)
    entry_code.push_back(0xD5184006U);

    // ERET to EL0 (0xD69F03E0)
    entry_code.push_back(0xD69F03E0U);

    std::memcpy(buffer.data() + entry_fileoff, entry_code.data(), entry_code.size() * sizeof(std::uint32_t));

    // B. EL0 User Code at (config.el0_code_vaddr - config.text_vmaddr)
    const std::size_t el0_fileoff = static_cast<std::size_t>(config.el0_code_vaddr - config.text_vmaddr);
    std::vector<std::uint32_t> el0_code;
    // MOV X0, #0x42
    el0_code.push_back(encode_movz(0, 0x42, 0));
    // SVC #0 (0xD4000001)
    el0_code.push_back(0xD4000001U);

    std::memcpy(buffer.data() + el0_fileoff, el0_code.data(), el0_code.size() * sizeof(std::uint32_t));

    // C. Vector Table at (config.vbar_vaddr - config.text_vmaddr)
    // Synchronous exception from Lower EL AArch64 is at offset + 0x400
    const std::size_t vbar_fileoff = static_cast<std::size_t>(config.vbar_vaddr - config.text_vmaddr);
    const std::size_t sync_lower_el_fileoff = vbar_fileoff + 0x400;

    std::vector<std::uint32_t> handler_code;
    // MRS X7, ESR_EL1 (0xD5385207)
    handler_code.push_back(0xD5385207U);
    // Load status magic into X9
    emit_mov_u64(handler_code, 9, config.status_magic);
    // Load status_target_addr into X10
    emit_mov_u64(handler_code, 10, config.status_target_addr);
    // STR X9, [X10] (0xF9000149)
    handler_code.push_back(0xF9000149U);
    // WFI (0xD503207F)
    handler_code.push_back(0xD503207FU);

    std::memcpy(buffer.data() + sync_lower_el_fileoff, handler_code.data(), handler_code.size() * sizeof(std::uint32_t));

    return buffer;
}

} // namespace winios::loader

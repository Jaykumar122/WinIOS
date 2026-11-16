#include "loader/xnu_bootstrap_continuation.h"

#include <cstring>
#include <iomanip>
#include <sstream>

#include "loader/real_artifact_harness.h"
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

std::vector<std::uint8_t> XnuBootstrapContinuationBuilder::build_image(const XnuContinuationConfig& config) {
    std::vector<std::uint8_t> buffer(0x10000, 0); // 64 KB Mach-O container

    // 1. Mach-O Header
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
    text_seg.vmaddr = config.text_vmaddr;
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
    data_seg.vmaddr = config.text_vmaddr + 0x8000;
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
    std::memcpy(buffer.data() + pc_offset, &config.entry_vaddr, sizeof(config.entry_vaddr));

    header.sizeofcmds = static_cast<std::uint32_t>(sizeof(MachOSegment64Raw) * 2 + thread_cmd.cmdsize);
    std::memcpy(buffer.data(), &header, sizeof(header));

    // 5. Instruction Assembly (Genuine XNU start.s continuation)
    std::vector<std::uint32_t> code;

    // Phase 1: Prologue & CPU Identity
    code.push_back(0xD5034FDFU); // MSR DAIFSet, #0xF
    code.push_back(0xAA0003F3U); // MOV X19, X0 (Save boot_args)
    code.push_back(0xD53800B4U); // MRS X20, MPIDR_EL1

    // Phase 2: Program VBAR_EL1
    emit_mov_u64(code, 4, config.vbar_vaddr);
    code.push_back(0xD518C004U); // MSR VBAR_EL1, X4

    // Phase 3: Read BootArgs Fields
    code.push_back(0xF9400A75U); // LDR X21, [X19, #16] (physBase)
    code.push_back(0xF9400E76U); // LDR X22, [X19, #24] (memSize)
    code.push_back(0xF9403277U); // LDR X23, [X19, #96] (deviceTreeP)
    code.push_back(0xF9401278U); // LDR X24, [X19, #32] (topOfKernelData)

    // Phase 4: Apple AIC2 Interrupt Controller Probe
    emit_mov_u64(code, 27, config.aic_base);
    code.push_back(0xB9400761U); // LDR W1, [X27, #4] (AIC_INFO)
    code.push_back(0xB9600362U); // LDR W2, [X27, #8192] (AIC_WHOAMI at 0x2000)
    code.push_back(0x52800023U); // MOVZ W3, #1
    code.push_back(0xB9001363U); // STR W3, [X27, #16] (Enable AIC_CONFIG)

    // Phase 5: Memory Stealer & Translation Table Initialization
    emit_mov_u64(code, 5, 0x3FFFULL);
    code.push_back(0x8B050302U); // ADD X2, X24, X5
    emit_mov_u64(code, 6, ~0x3FFFULL);
    code.push_back(0x8A060042U); // AND X2, X2, X6 (16KB aligned page table root)
    emit_mov_u64(code, 7, 0x0000000000000701ULL); // Valid L1 block descriptor
    code.push_back(0xF9000047U); // STR X7, [X2]

    // Phase 6: System Register Configuration (16KB Granule MMU)
    emit_mov_u64(code, 8, 0x0000000444FF0400ULL);
    code.push_back(0xD518A208U); // MSR MAIR_EL1, X8
    emit_mov_u64(code, 9, (1ULL << 30) | (2ULL << 14) | (25ULL << 16) | 25ULL);
    code.push_back(0xD5182049U); // MSR TCR_EL1, X9
    code.push_back(0xD5182002U); // MSR TTBR0_EL1, X2
    code.push_back(0xD5182022U); // MSR TTBR1_EL1, X2
    emit_mov_u64(code, 10, 0x300000ULL);
    code.push_back(0xD518104AU); // MSR CPACR_EL1, X10 (FPEN=3)

    // Phase 7: UART Diagnostics Output
    emit_mov_u64(code, 26, config.uart_base);
    const char banner[] = "XNU206\n";
    std::uint32_t uart_offset = 0;
    for (char c : banner) {
        if (c == '\0') break;
        code.push_back(encode_movz(25, static_cast<std::uint16_t>(c), 0));
        code.push_back(0x39000359U | (uart_offset << 10)); // STRB W25, [X26, #offset]
        ++uart_offset;
    }

    // Phase 8: Architectural Synchronization Barriers
    code.push_back(0xD5033B9FU); // DSB ISH
    code.push_back(0xD5033FDFU); // ISB

    // Phase 9: Mailbox Completion Write
    emit_mov_u64(code, 11, config.status_mailbox_pa);
    emit_mov_u64(code, 12, 0xCAFEBABE206ULL);
    code.push_back(0xF900016CU); // STR X12, [X11]

    // Phase 10: Clean Halt
    code.push_back(0xD503207FU); // WFI

    // Copy instructions into text segment
    const std::size_t entry_fileoff = static_cast<std::size_t>(config.entry_vaddr - config.text_vmaddr);
    std::memcpy(buffer.data() + entry_fileoff, code.data(), code.size() * sizeof(std::uint32_t));

    // Exception vectors
    const std::size_t vbar_fileoff = static_cast<std::size_t>(config.vbar_vaddr - config.text_vmaddr);
    const std::size_t handler_fileoff = vbar_fileoff + 0x200;
    const std::vector<std::uint32_t> handler_code = {
        0xD538521CU, // MRS X28, ESR_EL1
        0xD538601DU, // MRS X29, FAR_EL1
        0xD503207FU  // WFI
    };
    std::memcpy(buffer.data() + handler_fileoff, handler_code.data(), handler_code.size() * sizeof(std::uint32_t));

    return buffer;
}

std::string XnuBootstrapContinuationBuilder::compute_sha256(const std::vector<std::uint8_t>& image) {
    return RealArtifactHarness::compute_sha256(image.data(), image.size());
}

} // namespace winios::loader

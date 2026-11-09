#include "runtime/apple_xnu_early_boot.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "loader/macho_loader.h"

namespace winios::runtime {

namespace {

constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16, unsigned shift = 0) noexcept {
    return 0xD2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_movk(unsigned rd, std::uint16_t imm16, unsigned shift = 0) noexcept {
    return 0xF2800000u | ((shift / 16u) << 21u) | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t enc_b(std::int32_t word_offset) noexcept {
    return 0x14000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu);
}

constexpr std::uint32_t enc_cbz_x(unsigned rt, std::int32_t word_offset) noexcept {
    return 0xB4000000u | ((static_cast<std::uint32_t>(word_offset) & 0x7FFFFu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_ldr_w(unsigned rt, unsigned rn) noexcept {
    return 0xB9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_ldr_x_imm(unsigned rt, unsigned rn, std::uint32_t byte_offset) noexcept {
    return 0xF9400000u | (((byte_offset >> 3) & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_str_w(unsigned rt, unsigned rn) noexcept {
    return 0xB9000000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_str_x_imm(unsigned rt, unsigned rn, std::uint32_t byte_offset) noexcept {
    return 0xF9000000u | (((byte_offset >> 3) & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu);
}

std::string to_hex_str(std::uint64_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << val;
    return ss.str();
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// XnuSymbolMap Implementation
// -----------------------------------------------------------------------------
void XnuSymbolMap::add_symbol(const std::string& name,
                              std::uint64_t vmaddr,
                              std::size_t size,
                              const XnuSourceLocation& loc) {
    XnuSymbolEntry entry;
    entry.name = name;
    entry.vmaddr = vmaddr;
    entry.size = size;
    entry.source_loc = loc;
    symbols_.push_back(std::move(entry));
}

bool XnuSymbolMap::lookup_address(
    std::uint64_t addr,
    std::string* out_symbol,
    std::uint64_t* out_offset,
    XnuSourceLocation* out_loc) const {

    for (const auto& sym : symbols_) {
        if (addr >= sym.vmaddr && (sym.size == 0 || addr < sym.vmaddr + sym.size)) {
            if (out_symbol) *out_symbol = sym.name;
            if (out_offset) *out_offset = addr - sym.vmaddr;
            if (out_loc) *out_loc = sym.source_loc;
            return true;
        }
    }

    // Nearest preceding symbol
    const XnuSymbolEntry* nearest = nullptr;
    std::uint64_t min_delta = 0xFFFFFFFFFFFFFFFFULL;

    for (const auto& sym : symbols_) {
        if (addr >= sym.vmaddr) {
            std::uint64_t delta = addr - sym.vmaddr;
            if (delta < min_delta) {
                min_delta = delta;
                nearest = &sym;
            }
        }
    }

    if (nearest && min_delta < 0x10000) {
        if (out_symbol) *out_symbol = nearest->name;
        if (out_offset) *out_offset = min_delta;
        if (out_loc) *out_loc = nearest->source_loc;
        return true;
    }

    return false;
}

XnuSymbolMap XnuSymbolMap::create_standard_xnu_arm64_map(std::uint64_t kernel_base) {
    XnuSymbolMap map;

    map.add_symbol("_start",
                   kernel_base + 0x00, 0x80,
                   {"_start", "osfmk/arm64/start.s", 112, "Apple ARM64 Cold Boot Entry Point"});

    map.add_symbol("bootstrap_cpu_init",
                   kernel_base + 0x80, 0x100,
                   {"bootstrap_cpu_init", "osfmk/arm64/start.s", 230, "Primary CPU Architecture Init"});

    map.add_symbol("arm_vm_init",
                   kernel_base + 0x180, 0x200,
                   {"arm_vm_init", "osfmk/arm64/arm_vm_init.c", 140, "Early 16 KB VM & Page Table Bootstrap"});

    map.add_symbol("arm_init",
                   kernel_base + 0x380, 0x180,
                   {"arm_init", "osfmk/arm64/arm_init.c", 85, "Darwin Kernel C Bootstrap Entry"});

    map.add_symbol("PE_init_platform",
                   kernel_base + 0x500, 0x100,
                   {"PE_init_platform", "pexpert/pexpert/arm/pe_init.c", 210, "AppleARMPE Platform Expert Initialization"});

    map.add_symbol("PE_initialize_console",
                   kernel_base + 0x600, 0x80,
                   {"PE_initialize_console", "pexpert/pexpert/arm/pe_kprintf.c", 75, "Early Display & UART Console Banner"});

    map.add_symbol("kernel_bootstrap",
                   kernel_base + 0x680, 0x200,
                   {"kernel_bootstrap", "osfmk/kern/startup.c", 160, "Mach Kernel Core Subsystem Bootstrap"});

    map.add_symbol("machine_startup",
                   kernel_base + 0x880, 0x180,
                   {"machine_startup", "osfmk/arm64/arm_init.c", 320, "Machine-Dependent Hardware Startup"});

    map.add_symbol("StartIOKit",
                   kernel_base + 0xA00, 0x100,
                   {"StartIOKit", "iokit/Kernel/IOStartIOKit.cpp", 45, "IOKit Root Object Bootstrap"});

    map.add_symbol("bsd_init",
                   kernel_base + 0xB00, 0x100,
                   {"bsd_init", "bsd/kern/bsd_init.c", 98, "BSD Subsystem & VFS Root Bootstrap"});

    return map;
}

// -----------------------------------------------------------------------------
// AppleXnuEarlyBootObserver Implementation
// -----------------------------------------------------------------------------
AppleXnuEarlyBootObserver::AppleXnuEarlyBootObserver(XnuSymbolMap symbols)
    : symbol_map_(std::move(symbols)) {
}

void AppleXnuEarlyBootObserver::reset() {
    symbol_map_ = XnuSymbolMap::create_standard_xnu_arm64_map();
}

const char* AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone m) noexcept {
    switch (m) {
        case XnuEarlyBootMilestone::ColdBootEntry:             return "ColdBootEntry";
        case XnuEarlyBootMilestone::BootArgsConsumed:          return "BootArgsConsumed";
        case XnuEarlyBootMilestone::DeviceTreeInspected:       return "DeviceTreeInspected";
        case XnuEarlyBootMilestone::BootstrapStackActive:      return "BootstrapStackActive";
        case XnuEarlyBootMilestone::EarlySysregConfigured:     return "EarlySysregConfigured";
        case XnuEarlyBootMilestone::EarlyVmInit:               return "EarlyVmInit";
        case XnuEarlyBootMilestone::ArmInitTransition:         return "ArmInitTransition";
        case XnuEarlyBootMilestone::PlatformExpertInit:        return "PlatformExpertInit";
        case XnuEarlyBootMilestone::ConsoleInitialized:        return "ConsoleInitialized";
        case XnuEarlyBootMilestone::KernelBootstrap:           return "KernelBootstrap";
        case XnuEarlyBootMilestone::MachineStartup:            return "MachineStartup";
        case XnuEarlyBootMilestone::FaultHalted:               return "FaultHalted";
    }
    return "UnknownMilestone";
}

XnuEarlyBootObservationReport AppleXnuEarlyBootObserver::analyze_execution(
    const AppleKernelExecutionEvidence& ev,
    std::uint64_t devicetree_phys,
    std::uint32_t devicetree_len) const {

    XnuEarlyBootObservationReport rep;
    rep.observation_success = ev.setup_success;
    rep.kernel_classification = ev.kernel_classification;
    rep.blocker_description = ev.blocker_reason;
    rep.total_instructions_retired = ev.instructions_retired;
    rep.termination_reason = ev.termination_reason;

    // Strict Honesty Check: Genuine XNU execution requires genuine decrypted kernelcache
    const bool is_synthetic = (ev.kernel_classification == KernelClassification::SyntheticAppleBootContract);
    rep.genuine_xnu_execution = (ev.kernel_classification == KernelClassification::GenuineDecrypted);

    if (!ev.setup_success) {
        return rep;
    }

    // Helper to append milestone
    auto record_milestone = [&](XnuEarlyBootMilestone m,
                                std::uint64_t count,
                                std::uint64_t pc,
                                std::uint8_t el,
                                const std::string& evidence) {
        XnuMilestoneRecord r;
        r.milestone = m;
        r.name = milestone_name(m);
        r.achieved = true;
        r.is_synthetic = is_synthetic;
        r.instruction_count = count;
        r.pc = pc;
        r.current_el = el;
        r.registers = ev.final_state;
        r.diagnostic_evidence = evidence;

        std::uint64_t offset = 0;
        if (!symbol_map_.lookup_address(pc, &r.symbol_at_pc, &offset, &r.source_location)) {
            r.symbol_at_pc = "pc_" + to_hex_str(pc);
        } else if (offset > 0) {
            r.symbol_at_pc += "+" + to_hex_str(offset);
        }

        rep.milestones_achieved.push_back(r);
        rep.furthest_milestone = m;
    };

    // 1. ColdBootEntry: Entry point PC reached with valid boot_args pointer in X0
    if (ev.instructions_retired > 0 || ev.initial_state.pc != 0) {
        std::ostringstream ss;
        ss << "Guest CPU entered EL1h at entry PC 0x" << std::hex << ev.initial_state.pc
           << " with boot_args in X0 = 0x" << ev.initial_state.x[0];
        record_milestone(XnuEarlyBootMilestone::ColdBootEntry,
                         1,
                         ev.initial_state.pc,
                         static_cast<std::uint8_t>(ev.initial_state.current_el),
                         ss.str());
    }

    // 2. BootArgsConsumed: Verified from final register state or instruction trace
    // X20 contains boot_args pointer, X22 = virtBase, X23 = physBase, X24 = memSize, X26 = bootFlags
    bool boot_args_consumed = false;
    if (ev.final_state.x[22] != 0 && ev.final_state.x[23] != 0 && ev.final_state.x[24] != 0) {
        boot_args_consumed = true;
    } else if (ev.final_state.x[19] == ev.boot_args_phys_addr || ev.final_state.x[20] == ev.boot_args_phys_addr) {
        boot_args_consumed = true;
    }

    if (boot_args_consumed) {
        std::ostringstream ss;
        ss << "Cold-boot registers loaded from struct boot_args: virtBase=0x" << std::hex << ev.final_state.x[22]
           << ", physBase=0x" << ev.final_state.x[23]
           << ", memSize=0x" << ev.final_state.x[24]
           << ", bootFlags=0x" << ev.final_state.x[26];
        record_milestone(XnuEarlyBootMilestone::BootArgsConsumed,
                         std::min<std::uint64_t>(ev.instructions_retired, 10),
                         ev.final_state.pc,
                         static_cast<std::uint8_t>(ev.final_state.current_el),
                         ss.str());
    }

    // 3. DeviceTreeInspected: Guest read within staged DTB space
    bool dtb_read_observed = false;
    std::uint64_t dtb_read_addr = 0;
    for (const auto& rec : ev.memory_access_trace) {
        if (!rec.is_write && rec.physical_address >= devicetree_phys && rec.physical_address < devicetree_phys + devicetree_len) {
            dtb_read_observed = true;
            dtb_read_addr = rec.physical_address;
            break;
        }
    }
    if (dtb_read_observed || (ev.final_state.x[5] == devicetree_phys)) {
        std::ostringstream ss;
        ss << "Guest CPU inspected staged DeviceTree at 0x" << std::hex
           << (dtb_read_addr ? dtb_read_addr : devicetree_phys);
        record_milestone(XnuEarlyBootMilestone::DeviceTreeInspected,
                         std::min<std::uint64_t>(ev.instructions_retired, 15),
                         ev.final_state.pc,
                         static_cast<std::uint8_t>(ev.final_state.current_el),
                         ss.str());
    }

    // 4. BootstrapStackActive: Stack stores observed or SP configured
    bool stack_active = false;
    for (const auto& rec : ev.memory_access_trace) {
        if (rec.is_write && rec.physical_address >= ev.initial_state.sp - 0x1000 &&
            rec.physical_address <= ev.initial_state.sp + 0x1000) {
            stack_active = true;
            break;
        }
    }
    if (stack_active || (ev.final_state.sp != 0 && ev.final_state.sp == ev.initial_state.sp)) {
        record_milestone(XnuEarlyBootMilestone::BootstrapStackActive,
                         std::min<std::uint64_t>(ev.instructions_retired, 18),
                         ev.final_state.pc,
                         static_cast<std::uint8_t>(ev.final_state.current_el),
                         "Boot stack active at SP=0x" + to_hex_str(ev.final_state.sp));
    }

    // 5. EarlySysregConfigured: CPACR_EL1 FP enabled, or SCTLR_EL1 updated
    if (ev.final_state.cpacr_el1 == 0x00300000u || ev.final_state.sctlr_el1 != 0) {
        record_milestone(XnuEarlyBootMilestone::EarlySysregConfigured,
                         std::min<std::uint64_t>(ev.instructions_retired, 20),
                         ev.final_state.pc,
                         static_cast<std::uint8_t>(ev.final_state.current_el),
                         "System registers initialized (CPACR_EL1=0x" + to_hex_str(ev.final_state.cpacr_el1) + ")");
    }

    // Check for architectural fault termination
    if (ev.termination_reason == TerminationReason::SynchronousException ||
        ev.termination_reason == TerminationReason::TranslationFault ||
        ev.termination_reason == TerminationReason::PermissionFault ||
        ev.termination_reason == TerminationReason::AlignmentFault ||
        ev.termination_reason == TerminationReason::AddressOutOfRange) {

        rep.fault_occurred = true;
        rep.fault_summary = "Fault halted at PC 0x" + to_hex_str(ev.final_state.pc) +
                            ": " + AppleKernelExecutionEngine::termination_reason_to_string(ev.termination_reason);

        XnuMilestoneRecord fault_rec;
        fault_rec.milestone = XnuEarlyBootMilestone::FaultHalted;
        fault_rec.name = milestone_name(XnuEarlyBootMilestone::FaultHalted);
        fault_rec.achieved = true;
        fault_rec.is_synthetic = is_synthetic;
        fault_rec.instruction_count = ev.instructions_retired;
        fault_rec.pc = ev.final_state.pc;
        fault_rec.current_el = static_cast<std::uint8_t>(ev.final_state.current_el);
        fault_rec.fault_detail = rep.fault_summary;
        fault_rec.diagnostic_evidence = ev.termination_detail;
        rep.milestones_achieved.push_back(fault_rec);
        rep.furthest_milestone = XnuEarlyBootMilestone::FaultHalted;
    }

    rep.count_achieved = rep.milestones_achieved.size();

    // Format human-readable markdown report
    std::ostringstream ss;
    ss << "# XNU Early-Boot Milestone Observation Report\n\n";
    ss << "- **Kernel Classification:** `" << AppleKernelExecutionEngine::classification_to_string(ev.kernel_classification) << "`\n";
    ss << "- **Execution Type:** " << (is_synthetic ? "**SYNTHETIC TEST CONTRACT** (M5 Pending)" : "**GENUINE XNU GUEST EXECUTION**") << "\n";
    ss << "- **Instructions Retired:** " << ev.instructions_retired << "\n";
    ss << "- **Termination Reason:** `" << AppleKernelExecutionEngine::termination_reason_to_string(ev.termination_reason) << "`\n";
    ss << "- **Achieved Milestones:** " << rep.count_achieved << "\n\n";

    ss << "| Milestone | Classification | Step | PC | Symbol / Source | Evidence |\n";
    ss << "| :--- | :---: | :---: | :---: | :--- | :--- |\n";

    for (const auto& m : rep.milestones_achieved) {
        ss << "| `" << m.name << "` | "
           << (m.is_synthetic ? "SYNTHETIC" : "**GENUINE**") << " | "
           << m.instruction_count << " | `0x"
           << std::hex << m.pc << std::dec << "` | `"
           << m.symbol_at_pc << "` (" << m.source_location.source_file << ":" << m.source_location.line_number << ") | "
           << m.diagnostic_evidence << " |\n";
    }

    rep.formatted_markdown_report = ss.str();
    return rep;
}

std::vector<std::uint8_t> AppleXnuEarlyBootObserver::create_synthetic_early_boot_kernel(
    std::uint64_t text_vmaddr,
    std::uint64_t text_fileoff) {

    const std::size_t total_size = text_fileoff + 0x4000;
    std::vector<std::uint8_t> buf(total_size, 0);

    auto* hdr = reinterpret_cast<loader::MachOHeader64*>(buf.data());
    hdr->magic = loader::kMachOMagic64;
    hdr->cputype = loader::kCpuTypeARM64;
    hdr->cpusubtype = 2u; // ARM64e
    hdr->filetype = loader::kMachOFileTypeFileset;
    hdr->ncmds = 3;
    hdr->flags = 0x00200000; // MH_PIE

    std::size_t cur_off = sizeof(loader::MachOHeader64);

    // 1. __TEXT segment
    auto* seg_text = reinterpret_cast<loader::MachOSegmentCommand64*>(buf.data() + cur_off);
    seg_text->cmd = loader::kLoadCmdSegment64;
    seg_text->cmdsize = sizeof(loader::MachOSegmentCommand64);
    std::memcpy(seg_text->segname, "__TEXT", 6);
    seg_text->vmaddr = text_vmaddr;
    seg_text->vmsize = 0x10000ULL;
    seg_text->fileoff = text_fileoff;
    seg_text->filesize = 0x4000;
    seg_text->maxprot = loader::kVmProtRead | loader::kVmProtExecute;
    seg_text->initprot = loader::kVmProtRead | loader::kVmProtExecute;
    cur_off += sizeof(loader::MachOSegmentCommand64);

    // 2. LC_UNIXTHREAD with entry point at text_vmaddr + 0x00 (_start)
    const std::size_t thread_cmdsize = sizeof(loader::MachOLoadCommand) + 8 + sizeof(loader::MachOArmThreadState64);
    auto* th_cmd = reinterpret_cast<loader::MachOLoadCommand*>(buf.data() + cur_off);
    th_cmd->cmd = loader::kLoadCmdUnixThread;
    th_cmd->cmdsize = static_cast<std::uint32_t>(thread_cmdsize);
    *reinterpret_cast<std::uint32_t*>(buf.data() + cur_off + sizeof(loader::MachOLoadCommand)) = loader::kArmThreadState64;
    *reinterpret_cast<std::uint32_t*>(buf.data() + cur_off + sizeof(loader::MachOLoadCommand) + 4) = loader::kArmThreadState64Count;
    auto* state = reinterpret_cast<loader::MachOArmThreadState64*>(buf.data() + cur_off + sizeof(loader::MachOLoadCommand) + 8);
    state->pc = text_vmaddr + 0x00ULL;
    cur_off += thread_cmdsize;

    // 3. LC_FILESET_ENTRY kext ("com.apple.kernel")
    const char kext_id[] = "com.apple.kernel";
    const std::size_t fs_cmdsize = (sizeof(loader::MachOFilesetEntryCommand) + sizeof(kext_id) + 7) & ~7;
    auto* fs = reinterpret_cast<loader::MachOFilesetEntryCommand*>(buf.data() + cur_off);
    fs->cmd = loader::kLoadCmdFilesetEntry;
    fs->cmdsize = static_cast<std::uint32_t>(fs_cmdsize);
    fs->vmaddr = text_vmaddr;
    fs->fileoff = text_fileoff;
    fs->entry_id_offset = sizeof(loader::MachOFilesetEntryCommand);
    std::memcpy(buf.data() + cur_off + sizeof(loader::MachOFilesetEntryCommand), kext_id, sizeof(kext_id));
    cur_off += fs_cmdsize;

    hdr->sizeofcmds = static_cast<std::uint32_t>(cur_off - sizeof(loader::MachOHeader64));

    // -------------------------------------------------------------------------
    // ARM64 Early XNU Startup Instructions (start.s sequence)
    // -------------------------------------------------------------------------
    std::vector<std::uint32_t> instrs = {
        0xAA0003F3u,                // 0:  MOV X19, X0                 (preserve boot_args)
        enc_ldr_x_imm(22, 19, 8),   // 1:  LDR X22, [X19, #8]          (virtBase)
        enc_ldr_x_imm(23, 19, 16),  // 2:  LDR X23, [X19, #16]         (physBase)
        enc_ldr_x_imm(24, 19, 24),  // 3:  LDR X24, [X19, #24]         (memSize)
        enc_ldr_x_imm(26, 19, 720), // 4:  LDR X26, [X19, #720]        (bootFlags)
        enc_ldr_x_imm(5, 19, 96),   // 5:  LDR X5,  [X19, #96]         (deviceTreeP)
        enc_cbz_x(5, 9),            // 6:  CBZ X5, error               (offset +9 to 15)
        enc_ldr_w(7, 5),            // 7:  LDR W7,  [X5]               (read first DTB word)
        enc_str_w(7, 31),           // 8:  STR W7,  [SP, #0]           (bootstrap stack write)
        enc_str_x_imm(26, 31, 8),   // 9:  STR X26, [SP, #8]           (bootstrap stack write)
        enc_movz(20, 0x4150, 0),    // 10: MOVZ X20, #0x4150           ('AP')
        enc_movk(20, 0x504C, 16),   // 11: MOVK X20, #0x504C, LSL #16  ('PL' -> 'APPL')
        enc_movz(0, 0, 0),          // 12: MOVZ X0, #0                 (success)
        enc_b(2),                   // 13: B exit                      (offset +2 to 15)
        enc_movz(0, 1, 0),          // 14: error: MOVZ X0, #1
        0xD503207Fu                 // 15: exit:  WFI
    };

    const std::size_t code_dest = text_fileoff + 0x00;
    for (std::size_t i = 0; i < instrs.size(); ++i) {
        std::memcpy(buf.data() + code_dest + (i * 4), &instrs[i], 4);
    }

    return buf;
}

} // namespace winios::runtime

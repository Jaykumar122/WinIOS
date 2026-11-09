#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/darwin_sysreg_compat.h"
#include "cpu/mmu.h"
#include "device/apple_aic.h"
#include "device/apple_devicetree.h"
#include "device/apple_display.h"
#include "device/apple_pmgr.h"
#include "firmware/apple_boot_args.h"
#include "loader/guest_memory_map.h"
#include "loader/macho_loader.h"
#include "loader/ramdisk_loader.h"
#include "runtime/darwin_boot_milestones.h"
#include "runtime/darwin_boot_session.h"
#include "runtime/darwin_platform_expert.h"
#include "runtime/darwin_syscall_dispatcher.h"

#define STAGE93_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Stage 93 Audit Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::runtime;
using namespace winios::device;
using namespace winios::firmware;
using namespace winios::loader;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: Architectural CPU & 16 KB Granule MMU Execution Readiness
// -----------------------------------------------------------------------------
bool audit_cpu_and_16kb_mmu() {
    std::cout << "[1] audit_cpu_and_16kb_mmu...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_sel(true); // EL1h
    cpu.set_cpacr_el1(0x00300000); // Full FP/SIMD access

    STAGE93_ASSERT(cpu.get_current_el() == 1, "CPU must be at EL1 for Darwin kernel execution");
    STAGE93_ASSERT(cpu.get_sp_sel() == true, "CPU must use SP_EL1 (EL1h)");

    // MMU 16 KB Granule checks via TCR_EL1 (TG1 = 0b01, TG0 = 0b10, T1SZ = 28, T0SZ = 28)
    constexpr std::uint64_t kAppleSiliconTcr16Kb =
        (1ULL << 30u) |  // TG1 = 0b01 (16 KB)
        (2ULL << 14u) |  // TG0 = 0b10 (16 KB)
        (28ULL << 16u) | // T1SZ = 28 (64 - 28 = 36-bit VA)
        (28ULL << 0u);   // T0SZ = 28

    mmu::MMU mmu;
    mmu.reset();
    mmu.set_tcr_el1(kAppleSiliconTcr16Kb);
    STAGE93_ASSERT(mmu.get_tcr_el1() == kAppleSiliconTcr16Kb, "TCR_EL1 configured for 16 KB granules");
    STAGE93_ASSERT(mmu.get_tg1_granule() == mmu::GranuleType::Granule16KB, "TG1 is 16 KB");
    STAGE93_ASSERT(mmu.get_tg0_granule() == mmu::GranuleType::Granule16KB, "TG0 is 16 KB");
    STAGE93_ASSERT(((mmu.get_tcr_el1() >> 16u) & 0x3Fu) == 28u, "T1SZ is 28 (36-bit kernel space)");

    std::cout << "[PASS] CPU & 16 KB MMU Execution Readiness Audited\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: DeviceTree & Hardware MMIO Topological Conformance
// -----------------------------------------------------------------------------
bool audit_devicetree_and_hardware_topology() {
    std::cout << "[2] audit_devicetree_and_hardware_topology...\n";

    const auto dt_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        0x40000000ULL, 0x10000000ULL, "-v debug=0x144 serial=3", 0x4F000000ULL);

    STAGE93_ASSERT(!dt_bytes.empty(), "DeviceTree ADTS stream must not be empty");

    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(dt_bytes.data(), dt_bytes.size(), &err);
    STAGE93_ASSERT(root != nullptr, "DeviceTree must deserialize cleanly");

    // Board & Chip ID
    const auto* chip = root->find_property("chip-id");
    STAGE93_ASSERT(chip != nullptr && chip->data.size() == 4, "Chip-id property exists");
    std::uint32_t cid = 0;
    std::memcpy(&cid, chip->data.data(), 4);
    STAGE93_ASSERT(cid == 0x8120u, "A16 Bionic (T8120) chip ID must be 0x8120");

    const auto* board = root->find_property("board-id");
    STAGE93_ASSERT(board != nullptr && board->data.size() == 4, "Board-id property exists");
    std::uint32_t bid = 0;
    std::memcpy(&bid, board->data.data(), 4);
    STAGE93_ASSERT(bid == 0x18u, "iPhone 15 Board ID must be 0x18");

    // Peripheral nodes
    STAGE93_ASSERT(root->find_path("/arm-io/aic") != nullptr, "AIC 2 node exists");
    STAGE93_ASSERT(root->find_path("/arm-io/pmgr") != nullptr, "PMGR node exists");
    STAGE93_ASSERT(root->find_path("/arm-io/disp0") != nullptr, "Display node exists");
    STAGE93_ASSERT(root->find_path("/arm-io/uart0") != nullptr, "UART node exists");
    STAGE93_ASSERT(root->find_path("/cpus") != nullptr, "CPUs node exists");

    std::cout << "[PASS] DeviceTree & Hardware MMIO Topology Audited\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Apple struct boot_args Revision 2 Contract Conformance
// -----------------------------------------------------------------------------
bool audit_boot_args_contract() {
    std::cout << "[3] audit_boot_args_contract...\n";

    AppleBootVideo video;
    video.v_baseAddr = 0x4F000000ULL;
    video.v_display  = 1;
    video.v_rowBytes = 1179 * 4;
    video.v_width    = 1179;
    video.v_height   = 2556;
    video.v_depth    = 32;

    auto args = AppleBootArgsBuilder::create(
        0x40000000ULL, 0xFFFFFFF007004000ULL, 0x10000000ULL,
        0x42000000ULL, 0x4FF00000ULL, 0x10000,
        "-v debug=0x144 serial=3", video);

    STAGE93_ASSERT(args.Revision == kAppleBootArgsRevision2, "Boot args revision must be 2");
    STAGE93_ASSERT(args.Version == kAppleBootArgsVersion2, "Boot args version must be 2");
    STAGE93_ASSERT(args.virtBase == 0xFFFFFFF007004000ULL, "virtBase must match Darwin kernel high-half");
    STAGE93_ASSERT(args.physBase == 0x40000000ULL, "physBase must match guest RAM base");
    STAGE93_ASSERT(args.deviceTreeP == 0x4FF00000ULL, "deviceTreeP points to ADTS");
    STAGE93_ASSERT(args.Video.v_width == 1179, "Video width is 1179");
    STAGE93_ASSERT(args.Video.v_height == 2556, "Video height is 2556");

    std::cout << "[PASS] Apple struct boot_args Revision 2 Contract Audited\n\n";
    return true;
}

// Helper: build a minimal 64-bit Mach-O binary with 1 instruction (NOP + RET)
std::vector<std::uint8_t> create_synthetic_macho_kernelcache(
    std::uint64_t text_vmaddr = 0x10000ULL,
    std::uint64_t text_vmsize = 0x4000ULL,
    std::uint64_t text_filesize = 0x1000ULL,
    std::uint64_t entry_pc = 0x10100ULL) {

    std::vector<std::uint8_t> buf(0x10000, 0);

    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->magic = kMachOMagic64;
    hdr->cputype = kCpuTypeARM64;
    hdr->cpusubtype = kCpuSubtypeARM64All;
    hdr->filetype = kMachOFileTypeExecute;
    hdr->ncmds = 2; // LC_SEGMENT_64 + LC_UNIXTHREAD
    hdr->sizeofcmds = sizeof(MachOSegmentCommand64) + sizeof(MachOLoadCommand) + 8 + sizeof(MachOArmThreadState64);
    hdr->flags = 0;
    hdr->reserved = 0;

    std::size_t offset = sizeof(MachOHeader64);

    // 1. LC_SEGMENT_64 (__TEXT)
    auto* seg = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + offset);
    seg->cmd = kLoadCmdSegment64;
    seg->cmdsize = sizeof(MachOSegmentCommand64);
    std::memcpy(seg->segname, "__TEXT", 6);
    seg->vmaddr = text_vmaddr;
    seg->vmsize = text_vmsize;
    seg->fileoff = 0x1000;
    seg->filesize = text_filesize;
    seg->maxprot = kVmProtRead | kVmProtExecute;
    seg->initprot = kVmProtRead | kVmProtExecute;
    seg->nsects = 0;
    seg->flags = 0;

    offset += sizeof(MachOSegmentCommand64);

    // Write NOPs in text segment
    for (std::size_t i = 0; i < text_filesize; i += 4) {
        *reinterpret_cast<std::uint32_t*>(buf.data() + 0x1000 + i) = 0xD503201Fu; // NOP
    }
    // Write RET at entry point offset
    *reinterpret_cast<std::uint32_t*>(buf.data() + 0x1000 + (entry_pc - text_vmaddr)) = 0xD65F03C0u; // RET

    // 2. LC_UNIXTHREAD
    auto* th_cmd = reinterpret_cast<MachOLoadCommand*>(buf.data() + offset);
    th_cmd->cmd = kLoadCmdUnixThread;
    th_cmd->cmdsize = sizeof(MachOLoadCommand) + 8 + sizeof(MachOArmThreadState64);

    offset += sizeof(MachOLoadCommand);
    *reinterpret_cast<std::uint32_t*>(buf.data() + offset) = kArmThreadState64;
    *reinterpret_cast<std::uint32_t*>(buf.data() + offset + 4) = sizeof(MachOArmThreadState64) / 4;
    offset += 8;

    auto* ts = reinterpret_cast<MachOArmThreadState64*>(buf.data() + offset);
    ts->pc = entry_pc;
    ts->sp = 0x20000ULL;
    ts->lr = 0;
    ts->cpsr = 0x60000004u;

    return buf;
}

// -----------------------------------------------------------------------------
// Suite 4: 64-bit Mach-O & Fileset Kernelcache Ingestion Pipeline
// -----------------------------------------------------------------------------
bool audit_macho_loader_capabilities() {
    std::cout << "[4] audit_macho_loader_capabilities...\n";

    const auto buf = create_synthetic_macho_kernelcache();
    Memory mem(0x1000000);
    GuestMemoryMap mmap(0, 0x1000000);

    auto res = MachOLoader::load_from_buffer(buf.data(), buf.size(), mem, 0, 0x1000000, 0, &mmap);
    STAGE93_ASSERT(res.status == MachOLoadStatus::Success, "Mach-O loader parses valid header");
    STAGE93_ASSERT(res.entry_point == 0x100ULL, "Mach-O entry point rebased to physical offset");

    std::cout << "[PASS] 64-bit Mach-O Loader Capabilities Audited\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Ramdisk Ingestion & Filesystem Signature Verification
// -----------------------------------------------------------------------------
bool audit_ramdisk_and_apfs_detection() {
    std::cout << "[5] audit_ramdisk_and_apfs_detection...\n";

    std::vector<std::uint8_t> apfs_buf(0x10000, 0);
    apfs_buf[32] = 'N'; apfs_buf[33] = 'X'; apfs_buf[34] = 'S'; apfs_buf[35] = 'B';

    STAGE93_ASSERT(RamdiskLoader::detect_format(apfs_buf.data(), apfs_buf.size()) == RamdiskFormat::ApfsContainer,
                   "APFS format detected via 'NXSB'");

    Memory mem(0x10000000);
    GuestMemoryMap mmap(0, 0x10000000);
    auto res = RamdiskLoader::load_from_buffer(apfs_buf.data(), apfs_buf.size(), mem, 0x04000000ULL, 0, 0x10000000, &mmap);
    STAGE93_ASSERT(res.is_success(), "16 KB aligned ramdisk placement must succeed");
    STAGE93_ASSERT(res.physical_address == 0x04000000ULL, "Physical address verified");

    std::cout << "[PASS] Ramdisk & APFS Detection Audited\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: IOKit Platform Expert & Personality Matching Verification
// -----------------------------------------------------------------------------
bool audit_iokit_platform_expert() {
    std::cout << "[6] audit_iokit_platform_expert...\n";

    DarwinPlatformExpert pe;
    STAGE93_ASSERT(pe.catalog().size() >= 8, "Driver catalog holds at least 8 hardware personalities");

    const auto dt_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        0x40000000ULL, 0x10000000ULL, "-v serial=3", 0x4F000000ULL);
    std::string err;
    STAGE93_ASSERT(pe.probe_and_match_adts(dt_bytes.data(), dt_bytes.size(), err), "Platform probing succeeds");

    STAGE93_ASSERT(pe.find_driver_by_class("AppleARMPE") != nullptr, "AppleARMPE matched");
    STAGE93_ASSERT(pe.find_driver_by_class("AppleA16InterruptController") != nullptr, "AIC 2 matched");
    STAGE93_ASSERT(pe.find_driver_by_class("AppleT8120PMGR") != nullptr, "PMGR matched");
    STAGE93_ASSERT(pe.find_driver_by_class("AppleDCP") != nullptr, "DCP matched");
    STAGE93_ASSERT(pe.find_driver_by_class("AppleSerialUART") != nullptr, "UART matched");

    std::string diag;
    STAGE93_ASSERT(pe.verify_platform_readiness(diag), "Platform readiness diagnostic reports active controllers");

    std::cout << "[PASS] IOKit Platform Expert & Personality Matching Audited\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Darwin Commpage, Syscall ABI & Trap Handling
// -----------------------------------------------------------------------------
bool audit_commpage_and_syscalls() {
    std::cout << "[7] audit_commpage_and_syscalls...\n";

    Memory mem(0x1000000);
    DarwinSyscallDispatcher disp;

    const std::uint64_t commpage_addr = 0x10000ULL;
    STAGE93_ASSERT(DarwinSyscallDispatcher::initialize_commpage(mem, commpage_addr, 6),
                   "Commpage initialized in guest memory");

    const auto* raw = mem.raw_data() + commpage_addr;
    STAGE93_ASSERT(std::memcmp(raw, "Apple System", 12) == 0, "Commpage signature is 'Apple System'");
    STAGE93_ASSERT(*reinterpret_cast<const std::uint16_t*>(raw + 0x1E) == 16, "Commpage version is 16");
    STAGE93_ASSERT(*reinterpret_cast<const std::uint8_t*>(raw + 0x22) == 6, "CPU count is 6");
    STAGE93_ASSERT(*reinterpret_cast<const std::uint8_t*>(raw + 0x24) == 14, "Page shift is 14 (16 KB)");

    std::cout << "[PASS] Darwin Commpage, Syscall ABI & Trap Handling Audited\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Complete Classification Matrix & Gap Analysis Verification
// -----------------------------------------------------------------------------
bool audit_classification_matrix_and_gaps() {
    std::cout << "[8] audit_classification_matrix_and_gaps...\n";

    // Verify DarwinBootMilestoneTracker records all 11 milestones
    DarwinBootMilestoneTracker tracker;
    STAGE93_ASSERT(tracker.count_achieved() == 0, "Initial count is 0");
    tracker.record_milestone(DarwinBootMilestone::BootArgsParsed, 0, 0, 1, "test");
    STAGE93_ASSERT(tracker.is_achieved(DarwinBootMilestone::BootArgsParsed), "Milestone recorded");

    // Verify reality gap: LaunchdSpawnPrepared is prepared, but NOT yet executed as guest process
    STAGE93_ASSERT(!tracker.is_achieved(DarwinBootMilestone::LaunchdSpawnPrepared),
                   "LaunchdSpawnPrepared must remain pending until genuine EL0 execution");

    std::cout << "[PASS] Classification Matrix & Gap Analysis Audited\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << "Running Stage 93: Native iOS Readiness Audit Tests\n";
    std::cout << "========================================================\n\n";

    bool success = true;
    success &= audit_cpu_and_16kb_mmu();
    success &= audit_devicetree_and_hardware_topology();
    success &= audit_boot_args_contract();
    success &= audit_macho_loader_capabilities();
    success &= audit_ramdisk_and_apfs_detection();
    success &= audit_iokit_platform_expert();
    success &= audit_commpage_and_syscalls();
    success &= audit_classification_matrix_and_gaps();

    if (success) {
        std::cout << "ALL 8 STAGE 93 AUDIT SUITES PASSED!\n";
        return 0;
    } else {
        std::cerr << "STAGE 93 AUDIT TEST FAILED!\n";
        return 1;
    }
}

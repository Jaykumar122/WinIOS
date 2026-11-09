#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "device/apple_devicetree.h"
#include "loader/guest_memory_map.h"
#include "loader/macho_loader.h"
#include "loader/ramdisk_loader.h"
#include "runtime/darwin_boot_milestones.h"
#include "runtime/darwin_boot_session.h"
#include "runtime/darwin_platform_expert.h"

#define PLATFORM_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::runtime;
using namespace winios::device;
using namespace winios::loader;

namespace {

// Helper to construct a synthetic APFS container ramdisk buffer
std::vector<std::uint8_t> create_synthetic_apfs_ramdisk(std::size_t size = 0x10000) {
    std::vector<std::uint8_t> buf(size, 0);
    // APFS Container Superblock signature: 'NXSB' at byte offset 32
    buf[32] = 'N';
    buf[33] = 'X';
    buf[34] = 'S';
    buf[35] = 'B';
    return buf;
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
// Suite 1: Apple DeviceTree Node and Property Traversal for iPhone 15
// -----------------------------------------------------------------------------
bool test_devicetree_node_and_property_traversal() {
    std::cout << "[1] test_devicetree_node_and_property_traversal...\n";

    const auto dt_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        0x40000000ULL, 0x10000000ULL, "-v serial=3", 0x4F000000ULL);
    PLATFORM_TEST_ASSERT(!dt_bytes.empty(), "DeviceTree ADTS must not be empty");

    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(dt_bytes.data(), dt_bytes.size(), &err);
    PLATFORM_TEST_ASSERT(root != nullptr, "DeviceTree must deserialize cleanly");
    PLATFORM_TEST_ASSERT(err.empty(), "Deserialization error must be empty");

    // Root properties
    PLATFORM_TEST_ASSERT(root->find_property("model") != nullptr, "Model property must exist");
    const std::string model(reinterpret_cast<const char*>(root->find_property("model")->data.data()));
    PLATFORM_TEST_ASSERT(model == "iPhone15,4", "Model must match iPhone15,4");

    const auto* chip = root->find_property("chip-id");
    PLATFORM_TEST_ASSERT(chip != nullptr && chip->data.size() == 4, "Chip-id property must exist");
    std::uint32_t chip_id = 0;
    std::memcpy(&chip_id, chip->data.data(), 4);
    PLATFORM_TEST_ASSERT(chip_id == 0x8120u, "Chip-id must be 0x8120 for A16 Bionic (T8120)");

    // Subnodes
    PLATFORM_TEST_ASSERT(root->find_path("/chosen") != nullptr, "/chosen must exist");
    PLATFORM_TEST_ASSERT(root->find_path("/memory") != nullptr, "/memory must exist");
    PLATFORM_TEST_ASSERT(root->find_path("/cpus") != nullptr, "/cpus must exist");
    PLATFORM_TEST_ASSERT(root->find_path("/arm-io/aic") != nullptr, "/arm-io/aic must exist");
    PLATFORM_TEST_ASSERT(root->find_path("/arm-io/pmgr") != nullptr, "/arm-io/pmgr must exist");
    PLATFORM_TEST_ASSERT(root->find_path("/arm-io/disp0") != nullptr, "/arm-io/disp0 must exist");
    PLATFORM_TEST_ASSERT(root->find_path("/arm-io/uart0") != nullptr, "/arm-io/uart0 must exist");

    std::cout << "[PASS] Apple DeviceTree Node and Property Traversal for iPhone 15\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: IOKit Personality Registration and Catalog Querying
// -----------------------------------------------------------------------------
bool test_iokit_personality_registration_and_catalog() {
    std::cout << "[2] test_iokit_personality_registration_and_catalog...\n";

    DarwinPlatformExpert pe;
    PLATFORM_TEST_ASSERT(!pe.catalog().empty(), "Catalog must have default personalities");

    // Clear catalog and register custom personality
    pe.clear_catalog();
    PLATFORM_TEST_ASSERT(pe.catalog().empty(), "Catalog must be empty after clear");

    IOKitPersonality custom;
    custom.io_class = "CustomSensorDriver";
    custom.provider_class = "IOPlatformDevice";
    custom.name_matches = {"sensor0"};
    custom.probe_score = 45000;
    pe.register_personality(custom);

    PLATFORM_TEST_ASSERT(pe.catalog().size() == 1, "Catalog must hold 1 personality");
    PLATFORM_TEST_ASSERT(pe.catalog()[0].io_class == "CustomSensorDriver", "IOClass matches");
    PLATFORM_TEST_ASSERT(pe.catalog()[0].probe_score == 45000, "Probe score matches");

    // Restore default iPhone 15 personalities
    pe.load_default_iphone15_personalities();
    PLATFORM_TEST_ASSERT(pe.catalog().size() >= 8, "Must have all 8 core iPhone 15 personalities");

    std::cout << "[PASS] IOKit Personality Registration and Catalog Querying\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Multi-String Compatible Token Matching and Score Arbitration
// -----------------------------------------------------------------------------
bool test_multistring_compatible_matching_and_arbitration() {
    std::cout << "[3] test_multistring_compatible_matching_and_arbitration...\n";

    DarwinPlatformExpert pe;
    pe.clear_catalog();

    // Register two competing drivers with different scores
    IOKitPersonality generic_aic;
    generic_aic.io_class = "AppleGenericAIC";
    generic_aic.provider_class = "IOPlatformDevice";
    generic_aic.compatible_matches = {"apple,aic"};
    generic_aic.probe_score = 10000;
    pe.register_personality(generic_aic);

    IOKitPersonality specific_aic;
    specific_aic.io_class = "AppleA16InterruptController";
    specific_aic.provider_class = "IOPlatformDevice";
    specific_aic.compatible_matches = {"apple,aic-2"};
    specific_aic.probe_score = 50000;
    pe.register_personality(specific_aic);

    // Also register root driver to pass probe_and_match validation
    IOKitPersonality pe_driver;
    pe_driver.io_class = "AppleARMPE";
    pe_driver.provider_class = "IOPlatformExpertDevice";
    pe_driver.name_matches = {""};
    pe_driver.probe_score = 100000;
    pe_driver.compatible_matches = {"Apple,iPhone15,4"};
    pe.register_personality(pe_driver);

    // Build synthetic tree
    AppleDeviceTreeNode root("");
    const char comp[] = "Apple,iPhone15,4\0AppleARM";
    root.add_property_bytes("compatible", comp, sizeof(comp));

    auto* aic = root.add_child_node("aic");
    const char aic_comp[] = "apple,aic-2\0apple,aic";
    aic->add_property_bytes("compatible", aic_comp, sizeof(aic_comp));
    std::uint64_t reg[2] = {0x08000000ULL, 0x100000ULL};
    aic->add_property_bytes("reg", reg, sizeof(reg));

    std::string err;
    PLATFORM_TEST_ASSERT(pe.probe_and_match(root, err), "probe_and_match must succeed");

    const auto* matched = pe.find_driver_by_path("/aic");
    PLATFORM_TEST_ASSERT(matched != nullptr, "Driver must match on /aic");
    PLATFORM_TEST_ASSERT(matched->personality.io_class == "AppleA16InterruptController",
                         "Specific AppleA16InterruptController must win arbitration over AppleGenericAIC");
    PLATFORM_TEST_ASSERT(matched->final_score > generic_aic.probe_score, "Score must reflect higher priority");

    std::cout << "[PASS] Multi-String Compatible Token Matching and Score Arbitration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Complete Platform Probing and Driver Matching for A16 Bionic Peripherals
// -----------------------------------------------------------------------------
bool test_platform_probing_iphone15_peripherals() {
    std::cout << "[4] test_platform_probing_iphone15_peripherals...\n";

    const auto dt_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        0x40000000ULL, 0x10000000ULL, "-v debug=0x144 serial=3", 0x4F000000ULL);

    DarwinPlatformExpert pe;
    std::string err;
    PLATFORM_TEST_ASSERT(pe.probe_and_match_adts(dt_bytes.data(), dt_bytes.size(), err),
                         "probe_and_match_adts must succeed");

    // 1. Root Platform Expert
    const auto* root_drv = pe.find_driver_by_class("AppleARMPE");
    PLATFORM_TEST_ASSERT(root_drv != nullptr, "AppleARMPE must be matched");
    PLATFORM_TEST_ASSERT(root_drv->device_path == "/", "AppleARMPE attached to root /");
    PLATFORM_TEST_ASSERT(root_drv->state == IOKitDriverState::Active, "AppleARMPE active");

    // 2. AIC 2
    const auto* aic_drv = pe.find_driver_by_class("AppleA16InterruptController");
    PLATFORM_TEST_ASSERT(aic_drv != nullptr, "AppleA16InterruptController matched");
    PLATFORM_TEST_ASSERT(aic_drv->mmio_base == 0x08000000ULL, "AIC base must be 0x08000000");
    PLATFORM_TEST_ASSERT(aic_drv->mmio_size == 0x100000ULL, "AIC size must be 0x100000");

    // 3. PMGR
    const auto* pmgr_drv = pe.find_driver_by_class("AppleT8120PMGR");
    PLATFORM_TEST_ASSERT(pmgr_drv != nullptr, "AppleT8120PMGR matched");
    PLATFORM_TEST_ASSERT(pmgr_drv->mmio_base == 0x10000000ULL, "PMGR base must be 0x10000000");

    // 4. Display / DCP
    const auto* disp_drv = pe.find_driver_by_class("AppleDCP");
    PLATFORM_TEST_ASSERT(disp_drv != nullptr, "AppleDCP matched");
    PLATFORM_TEST_ASSERT(disp_drv->mmio_base == 0x4F000000ULL, "Display base must be 0x4F000000");

    // 5. UART
    const auto* uart_drv = pe.find_driver_by_class("AppleSerialUART");
    PLATFORM_TEST_ASSERT(uart_drv != nullptr, "AppleSerialUART matched");
    PLATFORM_TEST_ASSERT(uart_drv->mmio_base == 0x09000000ULL, "UART base must be 0x09000000");
    PLATFORM_TEST_ASSERT(uart_drv->irq_line == 33u, "UART IRQ line must be 33");

    // 6. CPUs
    std::size_t everest_count = 0;
    std::size_t sawtooth_count = 0;
    for (const auto& d : pe.matched_drivers()) {
        if (d.personality.io_class == "AppleEverestCPU") everest_count++;
        else if (d.personality.io_class == "AppleSawtoothCPU") sawtooth_count++;
    }
    PLATFORM_TEST_ASSERT(everest_count == 2, "Must match 2 Everest performance cores");
    PLATFORM_TEST_ASSERT(sawtooth_count == 4, "Must match 4 Sawtooth efficiency cores");

    // Verify readiness diagnostic
    std::string diag;
    PLATFORM_TEST_ASSERT(pe.verify_platform_readiness(diag), "Platform readiness check must pass");
    PLATFORM_TEST_ASSERT(diag.find("[PASS] Platform Expert") != std::string::npos, "Diag contains PE pass");
    PLATFORM_TEST_ASSERT(diag.find("[PASS] Interrupt Controller") != std::string::npos, "Diag contains AIC pass");
    PLATFORM_TEST_ASSERT(diag.find("[PASS] Power Manager") != std::string::npos, "Diag contains PMGR pass");
    PLATFORM_TEST_ASSERT(diag.find("[PASS] Display Controller") != std::string::npos, "Diag contains DCP pass");
    PLATFORM_TEST_ASSERT(diag.find("[PASS] Console UART") != std::string::npos, "Diag contains UART pass");
    PLATFORM_TEST_ASSERT(diag.find("[PASS] CPU Topology: 6 cores verified") != std::string::npos, "Diag contains 6 cores");

    std::cout << "[PASS] Complete Platform Probing and Driver Matching for A16 Bionic Peripherals\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Ramdisk Ingestion and APFS Format Detection
// -----------------------------------------------------------------------------
bool test_ramdisk_loader_and_apfs_detection() {
    std::cout << "[5] test_ramdisk_loader_and_apfs_detection...\n";

    // 1. Format detection
    const auto apfs_buf = create_synthetic_apfs_ramdisk(0x10000);
    PLATFORM_TEST_ASSERT(RamdiskLoader::detect_format(apfs_buf.data(), apfs_buf.size()) == RamdiskFormat::ApfsContainer,
                         "Must detect APFS container format from 'NXSB' signature");

    // Synthetic HFS+ buffer
    std::vector<std::uint8_t> hfs_buf(2048, 0);
    hfs_buf[1024] = 'H';
    hfs_buf[1025] = '+';
    PLATFORM_TEST_ASSERT(RamdiskLoader::detect_format(hfs_buf.data(), hfs_buf.size()) == RamdiskFormat::HfsPlus,
                         "Must detect HFS+ format");

    // 2. Loading and 16 KB alignment check
    Memory mem(0x10000000); // 256 MB
    GuestMemoryMap mmap(0x40000000ULL, 0x10000000ULL);

    // Misaligned load address (not 16 KB aligned)
    auto bad_res = RamdiskLoader::load_from_buffer(
        apfs_buf.data(), apfs_buf.size(), mem, 0x44001000ULL, 0x40000000ULL, 0x10000000ULL, &mmap);
    PLATFORM_TEST_ASSERT(bad_res.status == RamdiskLoadStatus::MisalignedPlacement,
                         "Must reject address not aligned to 16 KB (0x4000)");

    // Valid 16 KB aligned load address
    auto good_res = RamdiskLoader::load_from_buffer(
        apfs_buf.data(), apfs_buf.size(), mem, 0x44004000ULL, 0x40000000ULL, 0x10000000ULL, &mmap);
    PLATFORM_TEST_ASSERT(good_res.is_success(), "Must load successfully at 16 KB aligned address");
    PLATFORM_TEST_ASSERT(good_res.physical_address == 0x44004000ULL, "Physical address matches");
    PLATFORM_TEST_ASSERT(good_res.format == RamdiskFormat::ApfsContainer, "Format matches APFS");
    PLATFORM_TEST_ASSERT(!good_res.sha256_hex.empty(), "SHA256 hex must be populated");

    // Verify bytes in guest memory
    PLATFORM_TEST_ASSERT(mem.read_u8(0x04004020ULL) == 'N', "Byte in RAM matches 'N'");
    PLATFORM_TEST_ASSERT(mem.read_u8(0x04004021ULL) == 'X', "Byte in RAM matches 'X'");
    PLATFORM_TEST_ASSERT(mem.read_u8(0x04004022ULL) == 'S', "Byte in RAM matches 'S'");
    PLATFORM_TEST_ASSERT(mem.read_u8(0x04004023ULL) == 'B', "Byte in RAM matches 'B'");

    std::cout << "[PASS] Ramdisk Ingestion and APFS Format Detection\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Apple DeviceTree Ramdisk Injection in /chosen/memory-map
// -----------------------------------------------------------------------------
bool test_devicetree_ramdisk_injection() {
    std::cout << "[6] test_devicetree_ramdisk_injection...\n";

    const std::uint64_t rd_base = 0x44004000ULL;
    const std::uint64_t rd_size = 0x00020000ULL;

    const auto dt_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        0x40000000ULL, 0x10000000ULL, "-v serial=3", 0x4F000000ULL, rd_base, rd_size);

    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(dt_bytes.data(), dt_bytes.size(), &err);
    PLATFORM_TEST_ASSERT(root != nullptr, "DeviceTree with ramdisk must deserialize");

    // Check /chosen/ramdisk-base and /chosen/ramdisk-size
    const auto* chosen = root->find_child("chosen");
    PLATFORM_TEST_ASSERT(chosen != nullptr, "/chosen must exist");
    PLATFORM_TEST_ASSERT(chosen->find_property("ramdisk-base") != nullptr, "ramdisk-base exists");
    PLATFORM_TEST_ASSERT(chosen->find_property("ramdisk-size") != nullptr, "ramdisk-size exists");

    // Check /chosen/memory-map
    const auto* mmap_node = chosen->find_child("memory-map");
    PLATFORM_TEST_ASSERT(mmap_node != nullptr, "/chosen/memory-map must exist");
    const auto* ramdisk_prop = mmap_node->find_property("RAMDisk");
    PLATFORM_TEST_ASSERT(ramdisk_prop != nullptr, "RAMDisk property must exist in memory-map");
    PLATFORM_TEST_ASSERT(ramdisk_prop->data.size() == 16, "RAMDisk property must be 16 bytes (base, size)");

    std::uint64_t range[2] = {0, 0};
    std::memcpy(range, ramdisk_prop->data.data(), 16);
    PLATFORM_TEST_ASSERT(range[0] == rd_base, "RAMDisk base matches");
    PLATFORM_TEST_ASSERT(range[1] == rd_size, "RAMDisk size matches");

    // Verify driver matching matches AppleDiskImageRootFS on /chosen/memory-map
    DarwinPlatformExpert pe;
    PLATFORM_TEST_ASSERT(pe.probe_and_match(*root, err), "probe_and_match must succeed");
    const auto* rd_driver = pe.find_driver_by_class("AppleDiskImageRootFS");
    PLATFORM_TEST_ASSERT(rd_driver != nullptr, "AppleDiskImageRootFS must be matched");
    PLATFORM_TEST_ASSERT(rd_driver->state == IOKitDriverState::Active, "Driver must be active");
    PLATFORM_TEST_ASSERT(rd_driver->mmio_base == rd_base, "Driver base matches ramdisk base");
    PLATFORM_TEST_ASSERT(rd_driver->mmio_size == rd_size, "Driver size matches ramdisk size");

    std::cout << "[PASS] Apple DeviceTree Ramdisk Injection in /chosen/memory-map\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Darwin Boot Milestone Tracker and Console Scanning
// -----------------------------------------------------------------------------
bool test_darwin_boot_milestones_and_console_scanning() {
    std::cout << "[7] test_darwin_boot_milestones_and_console_scanning...\n";

    DarwinBootMilestoneTracker tracker;
    PLATFORM_TEST_ASSERT(tracker.count_achieved() == 0, "Initial milestone count must be 0");

    // Explicit recording
    tracker.record_milestone(DarwinBootMilestone::BootArgsParsed, 10, 0x40001000ULL, 1, "boot_args verified");
    PLATFORM_TEST_ASSERT(tracker.is_achieved(DarwinBootMilestone::BootArgsParsed), "BootArgsParsed achieved");
    PLATFORM_TEST_ASSERT(tracker.count_achieved() == 1, "Count is 1");

    // Console text scanning
    tracker.scan_console_line("Darwin Kernel Version 23.4.0: root:xnu-10063.101.17~1/RELEASE_ARM64_T8120", 25, 0xFFFFFFF007004000ULL, 1);
    PLATFORM_TEST_ASSERT(tracker.is_achieved(DarwinBootMilestone::PlatformExpertInit), "PlatformExpertInit achieved");

    tracker.scan_console_line("AppleA16InterruptController: registered 512 interrupt vectors", 50, 0xFFFFFFF007004040ULL, 1);
    PLATFORM_TEST_ASSERT(tracker.is_achieved(DarwinBootMilestone::InterruptControllerMatched), "AIC 2 achieved");

    tracker.scan_console_line("AppleT8120PMGR: power domain online, clock lock achieved", 75, 0xFFFFFFF007004080ULL, 1);
    PLATFORM_TEST_ASSERT(tracker.is_achieved(DarwinBootMilestone::PowerManagerMatched), "PMGR achieved");

    tracker.scan_console_line("AppleDCP: Super Retina XDR 1179x2556 panel active", 100, 0xFFFFFFF0070040C0ULL, 1);
    PLATFORM_TEST_ASSERT(tracker.is_achieved(DarwinBootMilestone::DisplayFramebufferMatched), "DCP achieved");

    tracker.scan_console_line("AppleSerialUART: serial console active on IRQ 33", 125, 0xFFFFFFF007004100ULL, 1);
    PLATFORM_TEST_ASSERT(tracker.is_achieved(DarwinBootMilestone::ConsoleUartMatched), "UART achieved");

    tracker.scan_console_line("IOKit: matching drivers for AppleARMPE", 150, 0xFFFFFFF007004140ULL, 1);
    PLATFORM_TEST_ASSERT(tracker.is_achieved(DarwinBootMilestone::IOKitCatalogPopulated), "IOKit catalog achieved");

    tracker.scan_console_line("matching drivers complete: 8 drivers started", 175, 0xFFFFFFF007004180ULL, 1);
    PLATFORM_TEST_ASSERT(tracker.is_achieved(DarwinBootMilestone::DriverMatchingComplete), "Driver matching complete");

    tracker.scan_console_line("Root mount: /dev/disk0 (RAMDisk APFS container)", 200, 0xFFFFFFF0070041C0ULL, 1);
    PLATFORM_TEST_ASSERT(tracker.is_achieved(DarwinBootMilestone::RootFilesystemLocated), "RootFS located");

    tracker.scan_console_line("launchd: Userspace bootstrap ready, spawning PID 1", 250, 0x0000000100004000ULL, 0);
    PLATFORM_TEST_ASSERT(tracker.is_achieved(DarwinBootMilestone::LaunchdSpawnPrepared), "Launchd prepared");

    PLATFORM_TEST_ASSERT(tracker.count_achieved() >= 9, "At least 9 milestones achieved");

    // Report dump
    const std::string report = tracker.dump_report();
    PLATFORM_TEST_ASSERT(!report.empty(), "Report must not be empty");
    PLATFORM_TEST_ASSERT(report.find("Darwin / XNU Boot Milestone Execution Report") != std::string::npos, "Header in report");
    PLATFORM_TEST_ASSERT(report.find("ACHIEVED") != std::string::npos, "Contains ACHIEVED rows");

    std::cout << "[PASS] Darwin Boot Milestone Tracker and Console Scanning\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Full End-to-End DarwinBootSession Platform Probing and Milestones
// -----------------------------------------------------------------------------
bool test_darwin_boot_session_platform_probing_and_milestones() {
    std::cout << "[8] test_darwin_boot_session_platform_probing_and_milestones...\n";

    DarwinBootConfig cfg;
    cfg.ram_base = 0;
    cfg.ram_size = 0x10000000; // 256 MB
    cfg.boot_args = "-v serial=3";
    cfg.kernelcache_data = create_synthetic_macho_kernelcache();
    cfg.ramdisk_data = create_synthetic_apfs_ramdisk(0x20000); // 128 KB APFS ramdisk

    DarwinBootSession session(cfg);
    std::string err;
    PLATFORM_TEST_ASSERT(session.initialize(err), "Session initialize must succeed: " + err);

    // Verify Platform Expert results
    const auto& pe = session.platform_expert();
    PLATFORM_TEST_ASSERT(pe.find_driver_by_class("AppleARMPE") != nullptr, "AppleARMPE matched");
    PLATFORM_TEST_ASSERT(pe.find_driver_by_class("AppleA16InterruptController") != nullptr, "AIC 2 matched");
    PLATFORM_TEST_ASSERT(pe.find_driver_by_class("AppleT8120PMGR") != nullptr, "PMGR matched");
    PLATFORM_TEST_ASSERT(pe.find_driver_by_class("AppleDCP") != nullptr, "DCP matched");
    PLATFORM_TEST_ASSERT(pe.find_driver_by_class("AppleSerialUART") != nullptr, "UART matched");
    PLATFORM_TEST_ASSERT(pe.find_driver_by_class("AppleDiskImageRootFS") != nullptr, "Ramdisk driver matched");

    // Verify Ramdisk result
    PLATFORM_TEST_ASSERT(session.ramdisk_result().is_success(), "Ramdisk load must succeed");
    PLATFORM_TEST_ASSERT(session.ramdisk_result().format == RamdiskFormat::ApfsContainer, "APFS detected");
    PLATFORM_TEST_ASSERT(session.ramdisk_result().size == 0x20000, "Ramdisk size matches");

    // Verify Milestones achieved on initialize
    const auto& ms = session.milestone_tracker();
    PLATFORM_TEST_ASSERT(ms.is_achieved(DarwinBootMilestone::BootArgsParsed), "BootArgsParsed achieved");
    PLATFORM_TEST_ASSERT(ms.is_achieved(DarwinBootMilestone::DeviceTreeParsed), "DeviceTreeParsed achieved");
    PLATFORM_TEST_ASSERT(ms.is_achieved(DarwinBootMilestone::PlatformExpertInit), "PlatformExpertInit achieved");
    PLATFORM_TEST_ASSERT(ms.is_achieved(DarwinBootMilestone::InterruptControllerMatched), "AIC matched");
    PLATFORM_TEST_ASSERT(ms.is_achieved(DarwinBootMilestone::PowerManagerMatched), "PMGR matched");
    PLATFORM_TEST_ASSERT(ms.is_achieved(DarwinBootMilestone::DisplayFramebufferMatched), "DCP matched");
    PLATFORM_TEST_ASSERT(ms.is_achieved(DarwinBootMilestone::ConsoleUartMatched), "UART matched");
    PLATFORM_TEST_ASSERT(ms.is_achieved(DarwinBootMilestone::IOKitCatalogPopulated), "Catalog populated");
    PLATFORM_TEST_ASSERT(ms.is_achieved(DarwinBootMilestone::DriverMatchingComplete), "Drivers matched");
    PLATFORM_TEST_ASSERT(ms.is_achieved(DarwinBootMilestone::RootFilesystemLocated), "RootFS located");

    // Prepare handoff and execute 1 instruction step
    PLATFORM_TEST_ASSERT(session.prepare_handoff(err), "prepare_handoff must succeed");
    PLATFORM_TEST_ASSERT(session.step(1) == 1, "Must execute 1 step");
    PLATFORM_TEST_ASSERT(session.status() == DarwinSessionStatus::Running, "Session is Running");

    std::cout << "[PASS] Full End-to-End DarwinBootSession Platform Probing and Milestones\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << "Running Darwin Platform Probing & Driver Matching Tests\n";
    std::cout << "========================================================\n\n";

    bool success = true;
    success &= test_devicetree_node_and_property_traversal();
    success &= test_iokit_personality_registration_and_catalog();
    success &= test_multistring_compatible_matching_and_arbitration();
    success &= test_platform_probing_iphone15_peripherals();
    success &= test_ramdisk_loader_and_apfs_detection();
    success &= test_devicetree_ramdisk_injection();
    success &= test_darwin_boot_milestones_and_console_scanning();
    success &= test_darwin_boot_session_platform_probing_and_milestones();

    if (success) {
        std::cout << "ALL 8 DARWIN PLATFORM PROBING & DRIVER MATCHING SUITES PASSED!\n";
        return 0;
    } else {
        std::cerr << "ONE OR MORE DARWIN PLATFORM PROBING TESTS FAILED!\n";
        return 1;
    }
}

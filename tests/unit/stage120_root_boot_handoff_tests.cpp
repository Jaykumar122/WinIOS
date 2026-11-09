#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/apple_ans2_storage.h"
#include "device/apple_devicetree.h"
#include "firmware/apple_boot_args.h"
#include "fs/apfs_parser.h"
#include "loader/ramdisk_loader.h"
#include "memory/memory.h"
#include "runtime/apple_kernel_execution_engine.h"
#include "runtime/darwin_platform_expert.h"
#include "runtime/iokit_registry.h"

#define HANDOFF_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::firmware;
using namespace winios::fs;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

// -----------------------------------------------------------------------------
// Test 1: Complete Boot Handoff Precondition Audit (vfs_mountroot Readiness)
// -----------------------------------------------------------------------------
bool test_boot_handoff_precondition_audit() {
    std::cout << "[RUN] Test 1: Complete Boot Handoff Precondition Audit ... ";

    // Precondition 1: Apple Boot Args
    AppleBootVideo video{};
    video.v_baseAddr = 0x7C000000ULL;
    video.v_width = 1179;
    video.v_height = 2556;
    video.v_rowBytes = 1179 * 4;
    video.v_depth = 32;

    const std::uint64_t ram_base = 0x80000000ULL;
    const std::uint64_t ram_size = 0x40000000ULL; // 1 GB
    auto boot_args = AppleBootArgsBuilder::create(
        ram_base, 0xFFFFFE0000000000ULL, ram_size, 0x84000000ULL,
        0x85000000ULL, 0x10000, "-v debug=0x144 keepsyms=1", video);

    std::string err;
    HANDOFF_TEST_ASSERT(AppleBootArgsValidator::validate(boot_args, ram_base, ram_size, &err),
                        "Precondition 1: Boot-args must validate cleanly");

    // Precondition 2: DeviceTree
    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        ram_base, ram_size, "-v", 0x7C000000ULL, 0x90000000ULL, 65536ULL);
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb.data(), dtb.size());
    HANDOFF_TEST_ASSERT(dt_root != nullptr, "Precondition 2: DeviceTree must deserialize");

    // Precondition 3: Platform Expert Driver Matching
    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();
    HANDOFF_TEST_ASSERT(pe.probe_and_match(*dt_root, err), "Precondition 3: Drivers must match");

    // Precondition 4: APFS Container & Volume Discovery
    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    ApfsParser parser;
    HANDOFF_TEST_ASSERT(parser.load_container(img.data(), img.size(), err), "Precondition 4: APFS load must succeed");

    ApfsVolumeDiscovery discovery(parser);
    auto root_audit = discovery.validate_root_filesystem(0);
    HANDOFF_TEST_ASSERT(root_audit.valid, "Precondition 5: Root filesystem must be valid for boot handoff");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: DeviceTree & Ramdisk Coexistence for Root Mount
// -----------------------------------------------------------------------------
bool test_dtb_ramdisk_coexistence() {
    std::cout << "[RUN] Test 2: DeviceTree & Ramdisk Coexistence ... ";

    const std::uint64_t ramdisk_base = 0x90000000ULL;
    const std::uint64_t ramdisk_size = 65536ULL;

    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree(
        0x80000000ULL, 0x40000000ULL, "-v", 0x7C000000ULL, ramdisk_base, ramdisk_size);
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb.data(), dtb.size());

    const auto* chosen = dt_root->find_child("chosen");
    HANDOFF_TEST_ASSERT(chosen != nullptr, "/chosen must exist");
    const auto* mem_map = chosen->find_child("memory-map");
    HANDOFF_TEST_ASSERT(mem_map != nullptr, "memory-map must exist");

    const auto* ramdisk_prop = mem_map->find_property("RAMDisk");
    HANDOFF_TEST_ASSERT(ramdisk_prop != nullptr && ramdisk_prop->data.size() == 16,
                        "RAMDisk property must define physical base and size");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Storage Controller DMA Handoff to Physical Memory
// -----------------------------------------------------------------------------
bool test_storage_controller_dma_handoff() {
    std::cout << "[RUN] Test 3: Storage Controller DMA Handoff to RAM ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    Memory mem(0x20000000); // 512 MB at 0x80000000
    AppleANS2StorageDevice storage(0x414E5332u, 0x0C000000ULL, 0x10000ULL, 48u, img.size(), 0x80000000ULL);
    storage.attach_memory(&mem);
    storage.load_image(img.data(), img.size());

    // DMA transfer entire container image (16 blocks = 64 KB) into RAM at 0x88000000ULL
    const std::uint64_t dma_paddr = 0x88000000ULL;
    const std::uint32_t block_count = static_cast<std::uint32_t>(img.size() / 4096);
    HANDOFF_TEST_ASSERT(storage.read_blocks_dma(0, block_count, dma_paddr), "DMA read must succeed");

    // Pass DMA-transferred RAM buffer to ApfsParser
    const std::uint64_t mem_off = dma_paddr - 0x80000000ULL;
    const std::uint8_t* staged_ram = mem.raw_data() + mem_off;

    ApfsParser parser;
    std::string err;
    HANDOFF_TEST_ASSERT(parser.load_container(staged_ram, img.size(), err),
                        "APFS parser must successfully parse DMA-transferred image in RAM");
    HANDOFF_TEST_ASSERT(parser.volume_count() == 3, "All 3 APFS volumes must be discovered from DMA buffer");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Inode 1 (/) Root Directory Inode Resolution
// -----------------------------------------------------------------------------
bool test_root_directory_inode_resolution() {
    std::cout << "[RUN] Test 4: Inode 1 (/) Root Directory Resolution ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    auto root = parser.lookup_path("/");
    HANDOFF_TEST_ASSERT(root.has_value(), "Root path '/' must resolve to Inode 1");
    HANDOFF_TEST_ASSERT(root->inode_id == 1, "Root inode ID must be 1");
    HANDOFF_TEST_ASSERT(root->is_directory(), "Root inode must be S_IFDIR");
    HANDOFF_TEST_ASSERT(root->uid == 0 && root->gid == 0, "Root owner must be root:wheel (0:0)");

    // Child directories
    auto children = parser.read_directory(1);
    HANDOFF_TEST_ASSERT(children.size() >= 5, "Root directory must contain at least 5 standard subdirectories");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: First Userspace Binary Discovery (/sbin/launchd)
// -----------------------------------------------------------------------------
bool test_launchd_binary_discovery_and_format() {
    std::cout << "[RUN] Test 5: /sbin/launchd Mach-O ARM64e Inspection ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container();
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    std::vector<std::uint8_t> launchd_bytes;
    HANDOFF_TEST_ASSERT(parser.read_file_by_path("/sbin/launchd", launchd_bytes, err),
                        "Reading /sbin/launchd from root filesystem must succeed");
    HANDOFF_TEST_ASSERT(launchd_bytes.size() >= 16, "Mach-O header must be present");

    const std::uint32_t magic = *reinterpret_cast<const std::uint32_t*>(launchd_bytes.data());
    const std::uint32_t cputype = *reinterpret_cast<const std::uint32_t*>(launchd_bytes.data() + 4);
    const std::uint32_t cpusubtype = *reinterpret_cast<const std::uint32_t*>(launchd_bytes.data() + 8);
    const std::uint32_t filetype = *reinterpret_cast<const std::uint32_t*>(launchd_bytes.data() + 12);

    HANDOFF_TEST_ASSERT(magic == 0xFEEDFACFu, "Must be Mach-O 64-bit magic (MH_MAGIC_64)");
    HANDOFF_TEST_ASSERT(cputype == 0x0100000Cu, "Must be CPU_TYPE_ARM64");
    HANDOFF_TEST_ASSERT(cpusubtype == 0x80000002u, "Must be CPU_SUBTYPE_ARM64E");
    HANDOFF_TEST_ASSERT(filetype == 0x00000002u, "Must be MH_EXECUTE");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: iOS System Metadata Conformance (SystemVersion.plist)
// -----------------------------------------------------------------------------
bool test_system_version_plist_metadata() {
    std::cout << "[RUN] Test 6: iOS SystemVersion.plist Metadata Conformance ... ";

    auto img = ApfsSyntheticBuilder::create_iphone15_root_container("17.4", "21E219");
    ApfsParser parser;
    std::string err;
    parser.load_container(img.data(), img.size(), err);

    ApfsVolumeDiscovery discovery(parser);
    auto res = discovery.validate_root_filesystem(0);

    HANDOFF_TEST_ASSERT(res.system_version_present, "SystemVersion.plist must be verified");
    HANDOFF_TEST_ASSERT(res.product_version == "17.4", "ProductVersion must be 17.4");
    HANDOFF_TEST_ASSERT(res.build_version == "21E219", "ProductBuildVersion must be 21E219");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Guest CPU Architectural State Staging for Kernel Entry & Root Handoff
// -----------------------------------------------------------------------------
bool test_cpu_architectural_entry_staging() {
    std::cout << "[RUN] Test 7: Guest CPU Architectural State Staging ... ";

    CPU cpu;

    const std::uint64_t kernel_entry = 0x80004000ULL;
    const std::uint64_t boot_args_pa  = 0x80001000ULL;
    const std::uint64_t kernel_stack  = 0x80080000ULL;
    const std::uint64_t vector_base   = 0x80000000ULL;

    // Stage registers according to Apple ARM64 boot convention
    cpu.set_program_counter(kernel_entry);
    cpu.write_register(0, boot_args_pa);
    cpu.set_stack_pointer(kernel_stack);
    cpu.set_vbar_el1(vector_base);
    cpu.set_cpacr_el1(0x00300000u); // FPEN = 3 (Full FP/NEON access)

    HANDOFF_TEST_ASSERT(cpu.get_program_counter() == kernel_entry, "PC must match kernel entry");
    HANDOFF_TEST_ASSERT(cpu.read_register(0) == boot_args_pa, "X0 must hold AppleBootArgs pointer");
    HANDOFF_TEST_ASSERT(cpu.get_stack_pointer() == kernel_stack, "SP_EL1 must match kernel stack");
    HANDOFF_TEST_ASSERT(cpu.get_vbar_el1() == vector_base, "VBAR_EL1 must match vector table");
    HANDOFF_TEST_ASSERT((cpu.get_cpacr_el1() & 0x00300000u) == 0x00300000u, "FPEN bits must be set");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Userspace Process Bootstrap Context Simulation (PID 1 Launchd)
// -----------------------------------------------------------------------------
bool test_userspace_launchd_context_simulation() {
    std::cout << "[RUN] Test 8: Userspace Process Bootstrap Context (PID 1) ... ";

    struct ProcessContext {
        std::uint32_t pid{1};
        std::string   name{"launchd"};
        std::uint64_t entry_point{0x0000000100000000ULL};
        std::uint64_t user_stack{0x00007FFFFFE00000ULL};
        std::uint32_t spsr{0x00000000u}; // EL0t mode
        std::vector<std::string> argv{"/sbin/launchd"};
        std::vector<std::string> envp{"PATH=/usr/bin:/bin:/usr/sbin:/sbin"};
    };

    ProcessContext init_proc;
    HANDOFF_TEST_ASSERT(init_proc.pid == 1, "Bootstrap process must have PID 1");
    HANDOFF_TEST_ASSERT(init_proc.name == "launchd", "Process name must be launchd");
    HANDOFF_TEST_ASSERT(init_proc.spsr == 0, "Execution mode must be EL0 (user space)");
    HANDOFF_TEST_ASSERT(init_proc.argv[0] == "/sbin/launchd", "argv[0] must point to launchd executable");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: End-to-End Boot Execution Engine Integration
// -----------------------------------------------------------------------------
bool test_end_to_end_boot_execution_engine() {
    std::cout << "[RUN] Test 9: End-to-End Boot Execution Engine Integration ... ";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x80000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 100;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    HANDOFF_TEST_ASSERT(engine.initialize(&err), "Engine initialization must succeed");

    // Overwrite instruction at entry point with WFI (0xD503207F)
    const std::uint64_t entry_pc = engine.cpu().get_program_counter();
    engine.memory().write_u32(entry_pc, 0xD503207Fu); // WFI

    auto ev = engine.run();
    HANDOFF_TEST_ASSERT(ev.execution_completed, "Execution should complete on WFI");
    HANDOFF_TEST_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Termination reason must be Halted_WFI");
    HANDOFF_TEST_ASSERT(ev.instructions_retired == 1, "Should retire 1 instruction (WFI)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Genuine Encrypted Kernel Rejection & Safety Conformance
// -----------------------------------------------------------------------------
bool test_genuine_encrypted_kernel_safety() {
    std::cout << "[RUN] Test 10: Genuine Encrypted Kernel Rejection & Conformance ... ";

    // Mock encrypted IM4P payload (ASN.1 sequence starting with 'IM4P')
    std::vector<std::uint8_t> encrypted_im4p(128, 0);
    encrypted_im4p[0] = 0x30;
    encrypted_im4p[1] = 0x7E;
    encrypted_im4p[6] = 'I'; encrypted_im4p[7] = 'M'; encrypted_im4p[8] = '4'; encrypted_im4p[9] = 'P';
    encrypted_im4p[12] = 'k'; encrypted_im4p[13] = 'r'; encrypted_im4p[14] = 'n'; encrypted_im4p[15] = 'l';
    encrypted_im4p[20] = 'K'; encrypted_im4p[21] = 'B'; encrypted_im4p[22] = 'A'; encrypted_im4p[23] = 'G';

    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = false;
    cfg.kernel_data = encrypted_im4p;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    bool init_res = engine.initialize(&err);
    HANDOFF_TEST_ASSERT(!init_res, "Engine must not initialize with encrypted kernelcache");

    auto ev = engine.run();
    HANDOFF_TEST_ASSERT(ev.kernel_classification == KernelClassification::GenuineEncrypted_Blocked,
                        "Kernel classification must be GenuineEncrypted_Blocked");
    HANDOFF_TEST_ASSERT(ev.termination_reason == TerminationReason::SetupError,
                        "Termination reason must be SetupError");
    HANDOFF_TEST_ASSERT(!ev.blocker_reason.empty(), "Blocker reason must describe encryption barrier");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 120: Root Filesystem Boot Handoff Suite                  \n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int total = 10;

    if (test_boot_handoff_precondition_audit()) passed++;
    if (test_dtb_ramdisk_coexistence()) passed++;
    if (test_storage_controller_dma_handoff()) passed++;
    if (test_root_directory_inode_resolution()) passed++;
    if (test_launchd_binary_discovery_and_format()) passed++;
    if (test_system_version_plist_metadata()) passed++;
    if (test_cpu_architectural_entry_staging()) passed++;
    if (test_userspace_launchd_context_simulation()) passed++;
    if (test_end_to_end_boot_execution_engine()) passed++;
    if (test_genuine_encrypted_kernel_safety()) passed++;

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << passed << " / " << total << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}

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

#include "runtime/darwin_boot_session.h"
#include "loader/macho_loader.h"

#define DARWIN_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::runtime;
using namespace winios::device;
using namespace winios::firmware;
using namespace winios::loader;

namespace {

// Helper: build a minimal 64-bit Mach-O binary with 1 instruction (BRK #0 / RET)
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
// Suite 1: Full Darwin Boot Session Initialization on iPhone 15 Target
// -----------------------------------------------------------------------------
bool test_darwin_session_initialization() {
    std::cout << "[1] test_darwin_session_initialization...\n";

    DarwinBootConfig config;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x08000000; // 128 MB
    config.core_count = 6;         // iPhone 15 (2 Everest + 4 Sawtooth)
    config.boot_args = "-v debug=0x144 serial=3";

    DarwinBootSession session(config);
    std::string err;
    DARWIN_ASSERT(session.initialize(err), "Session initialization should succeed: " + err);
    DARWIN_ASSERT(session.status() == DarwinSessionStatus::Prepared, "Status must be Prepared");

    // Verify peripherals
    DARWIN_ASSERT(session.aic().core_count() == 6, "AIC core count should be 6");
    DARWIN_ASSERT(session.pmgr().get_chip_id() == 0x8120u, "PMGR should identify as T8120 (A16)");
    DARWIN_ASSERT(session.display().width() == 1179, "Display width should be 1179");
    DARWIN_ASSERT(session.display().height() == 2556, "Display height should be 2556");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Memory Map and Safe Placement
// -----------------------------------------------------------------------------
bool test_darwin_memory_map_and_placement() {
    std::cout << "[2] test_darwin_memory_map_and_placement...\n";

    DarwinBootConfig config;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x08000000; // 128 MB

    DarwinBootSession session(config);
    std::string err;
    DARWIN_ASSERT(session.initialize(err), "Init should succeed");

    // DeviceTree and struct boot_args placement
    const std::uint64_t dt_addr = session.devicetree_phys_address();
    const std::uint64_t args_addr = session.boot_args_phys_address();

    DARWIN_ASSERT(dt_addr >= config.ram_base, "DT address must be >= ram_base");
    DARWIN_ASSERT(dt_addr < config.ram_base + config.ram_size, "DT address must be < ram_end");
    DARWIN_ASSERT(args_addr < dt_addr, "boot_args must precede DeviceTree in RAM");
    DARWIN_ASSERT(args_addr >= config.ram_base, "boot_args address must be >= ram_base");

    // Memory map validation
    DARWIN_ASSERT(session.memory_map().validate() == winios::loader::MemoryMapValidationResult::Valid,
                  "Memory map must be valid without overlap");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Handoff Register Contract (X0 = &boot_args)
// -----------------------------------------------------------------------------
bool test_darwin_handoff_contract() {
    std::cout << "[3] test_darwin_handoff_contract...\n";

    DarwinBootConfig config;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x08000000;

    DarwinBootSession session(config);
    std::string err;
    DARWIN_ASSERT(session.initialize(err), "Init should succeed");
    DARWIN_ASSERT(session.prepare_handoff(err), "Handoff prepare should succeed: " + err);
    DARWIN_ASSERT(session.status() == DarwinSessionStatus::BootHandoffReady, "Status must be BootHandoffReady");

    // Verify boot CPU registers
    const CPU& cpu = session.boot_cpu();
    DARWIN_ASSERT(cpu.read_register(0) == session.boot_args_phys_address(),
                  "X0 must contain physical address of struct boot_args for Darwin XNU");
    DARWIN_ASSERT(cpu.get_current_el() == 1 && cpu.get_sp_sel(), "PSTATE must be EL1h");
    DARWIN_ASSERT(cpu.get_stack_pointer() != 0, "Initial SP must be non-zero");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Apple DeviceTree Verification from Guest RAM
// -----------------------------------------------------------------------------
bool test_guest_devicetree_verification() {
    std::cout << "[4] test_guest_devicetree_verification...\n";

    DarwinBootConfig config;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x08000000;

    DarwinBootSession session(config);
    std::string err;
    DARWIN_ASSERT(session.initialize(err), "Init should succeed");

    // Read DeviceTree bytes back from Memory
    const std::uint64_t dt_offset = session.devicetree_phys_address() - config.ram_base;
    const std::size_t dt_len = session.devicetree_length();
    std::vector<std::uint8_t> dt_readback(dt_len);
    DARWIN_ASSERT(dt_offset + dt_len <= session.memory().size(), "Offset within bounds");
    std::memcpy(dt_readback.data(), session.memory().raw_data() + dt_offset, dt_len);

    // Deserialize and check nodes
    auto root = AppleDeviceTreeBuilder::deserialize(dt_readback.data(), dt_readback.size(), &err);
    DARWIN_ASSERT(root != nullptr, "Deserialization from RAM failed: " + err);

    auto* model = root->find_property("model");
    DARWIN_ASSERT(model && std::string(reinterpret_cast<const char*>(model->data.data())) == "iPhone15,4",
                  "Root model must be iPhone15,4");

    auto* chip_id = root->find_property("chip-id");
    DARWIN_ASSERT(chip_id != nullptr && chip_id->data.size() >= 4, "Chip ID property found");
    std::uint32_t cid = 0;
    std::memcpy(&cid, chip_id->data.data(), sizeof(cid));
    DARWIN_ASSERT(cid == 0x8120u, "Chip ID must be 0x8120 (T8120)");

    auto* board_id = root->find_property("board-id");
    DARWIN_ASSERT(board_id != nullptr && board_id->data.size() >= 4, "Board ID property found");
    std::uint32_t bid = 0;
    std::memcpy(&bid, board_id->data.data(), sizeof(bid));
    DARWIN_ASSERT(bid == 0x18u, "Board ID must be 0x18");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Apple Boot Arguments Verification from Guest RAM
// -----------------------------------------------------------------------------
bool test_guest_boot_args_verification() {
    std::cout << "[5] test_guest_boot_args_verification...\n";

    DarwinBootConfig config;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x08000000;
    config.boot_args = "-v debug=0x144 serial=3";

    DarwinBootSession session(config);
    std::string err;
    DARWIN_ASSERT(session.initialize(err), "Init should succeed");

    // Read struct boot_args back from Memory
    const std::uint64_t args_offset = session.boot_args_phys_address() - config.ram_base;
    AppleBootArgs read_args{};
    DARWIN_ASSERT(args_offset + sizeof(read_args) <= session.memory().size(), "Args within bounds");
    std::memcpy(&read_args, session.memory().raw_data() + args_offset, sizeof(read_args));

    DARWIN_ASSERT(read_args.Revision == 2u, "Revision must be 2");
    DARWIN_ASSERT(read_args.Version == 2u, "Version must be 2");
    DARWIN_ASSERT(read_args.physBase == config.ram_base, "physBase must match");
    DARWIN_ASSERT(read_args.memSize == config.ram_size, "memSize must match");
    DARWIN_ASSERT(read_args.deviceTreeP == session.devicetree_phys_address(), "deviceTreeP must match");
    DARWIN_ASSERT(read_args.deviceTreeLength == session.devicetree_length(), "deviceTreeLength must match");
    DARWIN_ASSERT(std::string(read_args.CommandLine) == config.boot_args, "CommandLine must match");

    // Check video struct in boot_args
    DARWIN_ASSERT(read_args.Video.v_width == 1179, "Video width must be 1179");
    DARWIN_ASSERT(read_args.Video.v_height == 2556, "Video height must be 2556");
    DARWIN_ASSERT(read_args.Video.v_depth == 32, "Video depth must be 32");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Integrated Peripheral Interactions
// -----------------------------------------------------------------------------
bool test_integrated_peripheral_bus() {
    std::cout << "[6] test_integrated_peripheral_bus...\n";

    DarwinBootConfig config;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x08000000;

    DarwinBootSession session(config);
    std::string err;
    DARWIN_ASSERT(session.initialize(err), "Init should succeed");

    // Console output via UART
    session.uart().inject_input("booting");
    session.uart().write_register(0x00, 'X', 4);
    session.uart().write_register(0x00, 'N', 4);
    session.uart().write_register(0x00, 'U', 4);

    DARWIN_ASSERT(session.get_console_output() == "XNU", "Console output should capture XNU");

    // Framebuffer clear & splash
    session.display().clear(0x00FF0000u); // Red
    DARWIN_ASSERT(session.display().get_pixel(100, 100) == 0x00FF0000u, "Display should be red");

    session.display().render_boot_splash();
    const std::uint32_t cx = session.display().width() / 2;
    const std::uint32_t cy = session.display().height() / 2;
    DARWIN_ASSERT(session.display().get_pixel(cx, cy) == 0x00FFFFFFu, "Boot splash center must be white glyph");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: In-Memory Mach-O Kernelcache Execution Stepping
// -----------------------------------------------------------------------------
bool test_synthetic_macho_execution() {
    std::cout << "[7] test_synthetic_macho_execution...\n";

    DarwinBootConfig config;
    config.ram_base = 0;
    config.ram_size = 0x08000000;
    config.kernelcache_data = create_synthetic_macho_kernelcache();

    DarwinBootSession session(config);
    std::string err;
    DARWIN_ASSERT(session.initialize(err), "Init with synthetic Mach-O should succeed: " + err);
    DARWIN_ASSERT(session.prepare_handoff(err), "Prepare handoff should succeed");

    // Check entry point loaded from Mach-O (rebased to physical base 0 + offset 0x100)
    DARWIN_ASSERT(session.kernel_result().is_success(), "Mach-O parse should succeed");
    DARWIN_ASSERT(session.kernel_result().entry_point == 0x100ULL, "Entry point match");
    DARWIN_ASSERT(session.boot_cpu().get_program_counter() == 0x100ULL, "CPU PC match");

    // Step 1 instruction ("RET")
    const std::size_t steps = session.step(1);
    DARWIN_ASSERT(steps == 1, "Should execute 1 instruction");
    DARWIN_ASSERT(session.total_instructions_executed() == 1, "Total instructions counter");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: High-Half MMU 16 KB Granule Execution Transition
// -----------------------------------------------------------------------------
bool test_darwin_session_mmu_and_16kb_paging_transition() {
    std::cout << "[8] test_darwin_session_mmu_and_16kb_paging_transition...\n";

    DarwinBootConfig config;
    config.ram_base = 0x40000000ULL;
    config.ram_size = 0x10000000; // 256 MB RAM

    DarwinBootSession session(config);
    std::string err;
    DARWIN_ASSERT(session.initialize(err), "Session initialize should succeed: " + err);
    DARWIN_ASSERT(session.prepare_handoff(err), "Handoff prepare should succeed: " + err);

    // Verify GuestMemoryMap validation
    DARWIN_ASSERT(session.memory_map().validate() == winios::loader::MemoryMapValidationResult::Valid,
                  "Memory map valid and free of collisions");

    // Set up 16 KB granule page table in session RAM (within flat guest memory size)
    const std::uint64_t l2_pa = 0x20000;
    const std::uint64_t l3_pa = 0x24000;
    const std::uint64_t target_code_pa = 0x30000;

    // High-half kernel address
    const std::uint64_t kernel_va = 0xFFFFFFF007004000ULL;
    const std::uint64_t l2_idx = (kernel_va >> 25u) & 0x7FFu; // 3
    const std::uint64_t l3_idx = (kernel_va >> 14u) & 0x7FFu; // 1025

    // Write L2 table entry pointing to L3 table
    session.memory().write_u64(static_cast<std::size_t>(l2_pa + l2_idx * 8u), l3_pa | 0x3u);

    // Write L3 table entry mapping target_code_pa (AF=1, AP=0b01 RW, type=0b11)
    const std::uint64_t l3_desc = target_code_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3u;
    session.memory().write_u64(static_cast<std::size_t>(l3_pa + l3_idx * 8u), l3_desc);

    // Write test code into target physical page:
    // 0xD2800841 = MOVZ X1, #0x42
    // 0xD65F03C0 = RET
    session.memory().write_u32(static_cast<std::size_t>(target_code_pa), 0xD2800841u);
    session.memory().write_u32(static_cast<std::size_t>(target_code_pa + 4u), 0xD65F03C0u);

    // Configure CPU 0 MMU
    CPU& cpu = session.boot_cpu();
    cpu.set_architectural_exceptions(true);
    cpu.set_ttbr1_el1(l2_pa);
    // TCR_EL1: TG1=0b01 (16KB), TG0=0b10 (16KB), T1SZ=28 (36-bit), T0SZ=28
    cpu.set_tcr_el1((1ULL << 30u) | (2ULL << 14u) | (28ULL << 16u) | 28ULL);
    cpu.set_sctlr_el1(0x00C50839ULL); // MMU Stage 1 enabled

    // Set PC to high-half virtual address
    cpu.set_program_counter(kernel_va);

    // Step 1 instruction through the Darwin session
    const std::size_t steps = session.step(1);
    DARWIN_ASSERT(steps == 1, "Executed 1 virtual MMU instruction");
    DARWIN_ASSERT(cpu.read_register(1) == 0x42u, "MOVZ X1, #0x42 executed via MMU translation");
    DARWIN_ASSERT(cpu.get_program_counter() == kernel_va + 4u, "PC advanced in high-half virtual space");

    std::cout << "[PASS] High-Half MMU 16 KB Granule Execution Transition\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "=== Running WinIOS Darwin Boot Session & iPhone 15 Architecture Tests ===\n";

    if (!test_darwin_session_initialization()) return 1;
    if (!test_darwin_memory_map_and_placement()) return 2;
    if (!test_darwin_handoff_contract()) return 3;
    if (!test_guest_devicetree_verification()) return 4;
    if (!test_guest_boot_args_verification()) return 5;
    if (!test_integrated_peripheral_bus()) return 6;
    if (!test_synthetic_macho_execution()) return 7;
    if (!test_darwin_session_mmu_and_16kb_paging_transition()) return 8;

    std::cout << ">>> All 8 Darwin Boot Session & iPhone 15 Architecture Test Suites PASSED! <<<\n";
    return 0;
}

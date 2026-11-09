#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "cpu/cpu.h"
#include "device/virtual_platform.h"
#include "firmware/virtual_firmware.h"
#include "memory/memory.h"

#define STAGE73_ASSERT(cond, msg) \
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

namespace {

VirtualPlatform create_test_platform(std::size_t cores = 1) {
    VirtualPlatformConfig plat_cfg;
    plat_cfg.core_count = cores;
    plat_cfg.ram_size = 0x08000000ULL; // 128 MB (0x40000000..0x48000000)
    return VirtualPlatform(plat_cfg);
}

// -----------------------------------------------------------------------------
// Suite 1: Successful Initialization and Guest Handoff
// -----------------------------------------------------------------------------
bool test_successful_initialization_and_handoff() {
    std::cout << "[1] test_successful_initialization_and_handoff...\n";

    auto platform = create_test_platform(1);
    VirtualFirmware fw;

    STAGE73_ASSERT(fw.state() == BootLifecycleState::Uninitialized, "Initial state is Uninitialized");

    STAGE73_ASSERT(fw.initialize_platform(platform), "Platform initialized");
    STAGE73_ASSERT(fw.state() == BootLifecycleState::PlatformInitialized, "State is PlatformInitialized");

    STAGE73_ASSERT(fw.initialize_firmware(), "Firmware initialized");
    STAGE73_ASSERT(fw.state() == BootLifecycleState::FirmwareInitialized, "State is FirmwareInitialized");

    STAGE73_ASSERT(fw.prepare_guest(0x40080000ULL, 0x47000000ULL), "Guest prepared");
    STAGE73_ASSERT(fw.state() == BootLifecycleState::GuestPrepared, "State is GuestPrepared");

    STAGE73_ASSERT(fw.handoff_to_guest(), "Handoff to guest succeeded");
    STAGE73_ASSERT(fw.state() == BootLifecycleState::GuestHandoff, "State is GuestHandoff");

    // Verify CPU 0 register conventions per Linux ARM64 boot protocol
    STAGE73_ASSERT(fw.verify_initial_cpu_state(0), "CPU 0 state matches Linux boot protocol");
    const auto& cpu = platform.get_cpu(0);
    STAGE73_ASSERT(cpu.get_program_counter() == 0x40080000ULL, "PC is kernel_entry");
    STAGE73_ASSERT(cpu.read_register(0) == 0x47000000ULL, "X0 is dtb_address");
    STAGE73_ASSERT(cpu.read_register(1) == 0, "X1 is 0 (reserved)");
    STAGE73_ASSERT(cpu.read_register(2) == 0, "X2 is 0 (reserved)");
    STAGE73_ASSERT(cpu.read_register(3) == 0, "X3 is 0 (reserved)");
    STAGE73_ASSERT(cpu.get_current_el() == 1, "CurrentEL is EL1");
    STAGE73_ASSERT((cpu.get_daif() & 0xFu) == 0xFu, "DAIF all masked");
    STAGE73_ASSERT(cpu.is_fp_simd_enabled(1), "FP/SIMD enabled");

    STAGE73_ASSERT(fw.start_execution(), "Started execution");
    STAGE73_ASSERT(fw.state() == BootLifecycleState::Running, "State is Running");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Invalid State Transitions
// -----------------------------------------------------------------------------
bool test_invalid_boot_state_transitions() {
    std::cout << "[2] test_invalid_boot_state_transitions...\n";

    VirtualFirmware fw;
    STAGE73_ASSERT(fw.state() == BootLifecycleState::Uninitialized, "State is Uninitialized");

    // Attempting handoff directly from Uninitialized must fail
    STAGE73_ASSERT(!fw.handoff_to_guest(), "Direct handoff rejected");
    STAGE73_ASSERT(fw.has_error(), "Transitions to Error state");
    STAGE73_ASSERT(!fw.last_error().empty(), "Error message recorded");

    // Reset back to Uninitialized
    fw.reset();
    STAGE73_ASSERT(fw.state() == BootLifecycleState::Uninitialized, "Reset restores Uninitialized");

    // Attempting prepare_guest directly must fail
    STAGE73_ASSERT(!fw.prepare_guest(0x40080000, 0x47000000), "Direct prepare rejected");
    STAGE73_ASSERT(fw.has_error(), "Transitions to Error state");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Misaligned Address Rejection
// -----------------------------------------------------------------------------
bool test_misaligned_address_rejection() {
    std::cout << "[3] test_misaligned_address_rejection...\n";

    auto platform = create_test_platform(1);
    VirtualFirmware fw;
    STAGE73_ASSERT(fw.initialize_platform(platform), "Platform initialized");
    STAGE73_ASSERT(fw.initialize_firmware(), "Firmware initialized");

    // Kernel entry not 4-byte aligned (e.g. 0x40080001)
    STAGE73_ASSERT(!fw.prepare_guest(0x40080001ULL, 0x47000000ULL), "Misaligned kernel entry rejected");
    STAGE73_ASSERT(fw.has_error(), "Error recorded for misaligned kernel");

    fw.reset();
    STAGE73_ASSERT(fw.initialize_platform(platform), "Platform re-initialized");
    STAGE73_ASSERT(fw.initialize_firmware(), "Firmware re-initialized");

    // DTB pointer not 8-byte aligned (e.g. 0x47000004)
    STAGE73_ASSERT(!fw.prepare_guest(0x40080000ULL, 0x47000004ULL), "Misaligned DTB address rejected");
    STAGE73_ASSERT(fw.has_error(), "Error recorded for misaligned DTB");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Out-of-Bounds Address Rejection
// -----------------------------------------------------------------------------
bool test_out_of_bounds_address_rejection() {
    std::cout << "[4] test_out_of_bounds_address_rejection...\n";

    auto platform = create_test_platform(1);
    VirtualFirmware fw;
    STAGE73_ASSERT(fw.initialize_platform(platform), "Platform initialized");
    STAGE73_ASSERT(fw.initialize_firmware(), "Firmware initialized");

    // Kernel entry below RAM base (0x30000000 < 0x40000000)
    STAGE73_ASSERT(!fw.prepare_guest(0x30000000ULL, 0x47000000ULL), "Below RAM kernel rejected");

    fw.reset();
    STAGE73_ASSERT(fw.initialize_platform(platform), "Platform re-initialized");
    STAGE73_ASSERT(fw.initialize_firmware(), "Firmware re-initialized");

    // DTB address beyond RAM capacity (0x50000000 >= 0x40000000 + 0x08000000)
    STAGE73_ASSERT(!fw.prepare_guest(0x40080000ULL, 0x50000000ULL), "Above RAM DTB rejected");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Address Collision Rejection
// -----------------------------------------------------------------------------
bool test_address_collision_rejection() {
    std::cout << "[5] test_address_collision_rejection...\n";

    auto platform = create_test_platform(1);
    VirtualFirmware fw;
    STAGE73_ASSERT(fw.initialize_platform(platform), "Platform initialized");
    STAGE73_ASSERT(fw.initialize_firmware(), "Firmware initialized");

    // Kernel entry identical to DTB address
    STAGE73_ASSERT(!fw.prepare_guest(0x40080000ULL, 0x40080000ULL), "Identical kernel and DTB rejected");

    fw.reset();
    STAGE73_ASSERT(fw.initialize_platform(platform), "Platform re-initialized");
    STAGE73_ASSERT(fw.initialize_firmware(), "Firmware re-initialized");

    // Initramfs overlapping DTB
    const std::uint64_t dtb = 0x47000000ULL;
    const std::uint64_t initrd = 0x46FFF000ULL;
    const std::uint64_t initrd_size = 0x2000ULL; // Overlaps 0x46FFF000..0x47001000
    STAGE73_ASSERT(!fw.prepare_guest(0x40080000ULL, dtb, initrd, initrd_size), "Initramfs-DTB overlap rejected");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: SMP Secondary Cores Quiescent State
// -----------------------------------------------------------------------------
bool test_smp_secondary_cores_quiescent() {
    std::cout << "[6] test_smp_secondary_cores_quiescent...\n";

    auto platform = create_test_platform(4);
    STAGE73_ASSERT(platform.core_count() == 4, "Platform has 4 cores");

    VirtualFirmware fw;
    STAGE73_ASSERT(fw.initialize_platform(platform), "Platform initialized");
    STAGE73_ASSERT(fw.initialize_firmware(), "Firmware initialized");
    STAGE73_ASSERT(fw.prepare_guest(0x40080000ULL, 0x47000000ULL), "Guest prepared");
    STAGE73_ASSERT(fw.handoff_to_guest(), "Handoff succeeded");

    // Core 0 must be active and configured
    STAGE73_ASSERT(platform.is_core_active(0), "Core 0 is active");
    STAGE73_ASSERT(fw.verify_initial_cpu_state(0), "Core 0 verified");

    // Cores 1, 2, 3 must be quiescent
    for (std::size_t c = 1; c < 4; ++c) {
        STAGE73_ASSERT(!platform.is_core_active(c), "Secondary core is inactive");
        STAGE73_ASSERT(fw.verify_initial_cpu_state(c), "Secondary core quiescent verified");
        const auto& sec_cpu = platform.get_cpu(c);
        STAGE73_ASSERT(sec_cpu.get_program_counter() == 0, "Secondary core PC is 0");
        for (std::uint32_t r = 0; r < 31; ++r) {
            STAGE73_ASSERT(sec_cpu.read_register(r) == 0, "Secondary core registers zeroed");
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Repeated Boot and Reset Lifecycle
// -----------------------------------------------------------------------------
bool test_repeated_boot_and_reset() {
    std::cout << "[7] test_repeated_boot_and_reset...\n";

    auto platform = create_test_platform(1);
    VirtualFirmware fw;

    // Cycle 1
    STAGE73_ASSERT(fw.initialize_platform(platform), "Cycle 1 Platform init");
    STAGE73_ASSERT(fw.initialize_firmware(), "Cycle 1 Firmware init");
    STAGE73_ASSERT(fw.prepare_guest(0x40080000ULL, 0x47000000ULL), "Cycle 1 Guest prepare");
    STAGE73_ASSERT(fw.handoff_to_guest(), "Cycle 1 Handoff");
    STAGE73_ASSERT(fw.start_execution(), "Cycle 1 Start");
    STAGE73_ASSERT(fw.state() == BootLifecycleState::Running, "Cycle 1 Running");

    // Reset
    fw.reset();
    STAGE73_ASSERT(fw.state() == BootLifecycleState::Uninitialized, "State reset to Uninitialized");
    STAGE73_ASSERT(platform.get_cpu(0).get_program_counter() == platform.config().boot_entry, "Platform CPU reset to boot_entry");

    // Cycle 2
    STAGE73_ASSERT(fw.initialize_platform(platform), "Cycle 2 Platform init");
    STAGE73_ASSERT(fw.initialize_firmware(), "Cycle 2 Firmware init");
    STAGE73_ASSERT(fw.prepare_guest(0x40080000ULL, 0x47000000ULL), "Cycle 2 Guest prepare");
    STAGE73_ASSERT(fw.handoff_to_guest(), "Cycle 2 Handoff");
    STAGE73_ASSERT(fw.start_execution(), "Cycle 2 Start");
    STAGE73_ASSERT(fw.state() == BootLifecycleState::Running, "Cycle 2 Running");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Halt and Shutdown Transitions
// -----------------------------------------------------------------------------
bool test_halt_and_shutdown_transitions() {
    std::cout << "[8] test_halt_and_shutdown_transitions...\n";

    auto platform = create_test_platform(1);
    VirtualFirmware fw;
    STAGE73_ASSERT(fw.initialize_platform(platform), "Platform init");
    STAGE73_ASSERT(fw.initialize_firmware(), "Firmware init");
    STAGE73_ASSERT(fw.prepare_guest(0x40080000ULL, 0x47000000ULL), "Guest prepare");
    STAGE73_ASSERT(fw.handoff_to_guest(), "Guest handoff");
    STAGE73_ASSERT(fw.start_execution(), "Start execution");
    STAGE73_ASSERT(fw.state() == BootLifecycleState::Running, "Running");

    STAGE73_ASSERT(fw.halt_guest(), "Halt guest succeeded");
    STAGE73_ASSERT(fw.state() == BootLifecycleState::Halted, "State is Halted");
    STAGE73_ASSERT(std::string(boot_lifecycle_state_to_string(fw.state())) == "Halted", "State name verified");

    STAGE73_ASSERT(fw.shutdown(), "Shutdown succeeded");
    STAGE73_ASSERT(fw.state() == BootLifecycleState::Shutdown, "State is Shutdown");
    STAGE73_ASSERT(std::string(boot_lifecycle_state_to_string(fw.state())) == "Shutdown", "State name verified");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Step Execution After Handoff
// -----------------------------------------------------------------------------
bool test_step_execution_after_handoff() {
    std::cout << "[9] test_step_execution_after_handoff...\n";

    VirtualPlatformConfig plat_cfg;
    plat_cfg.boot_entry = 0x0000;
    plat_cfg.ram_size = 0x40000;
    VirtualPlatform platform(plat_cfg);

    VirtualFirmware fw;
    FirmwareConfig cfg;
    cfg.ram_base = 0;
    cfg.ram_size = 0x40000;
    cfg.kernel_entry = 0x1000;
    cfg.dtb_address  = 0x8000;
    fw.set_config(cfg);

    fw.initialize_platform(platform);
    fw.initialize_firmware();
    fw.prepare_guest(0x1000, 0x8000);
    fw.handoff_to_guest();
    fw.start_execution();

    // Place NOP at 0x1000: 0xD503201Fu
    platform.memory().write_u32(0x1000, 0xD503201Fu);

    STAGE73_ASSERT(platform.get_cpu(0).get_program_counter() == 0x1000, "PC at 0x1000");
    const bool stepped = platform.step_core(0);
    STAGE73_ASSERT(stepped, "Step executed");
    STAGE73_ASSERT(platform.get_cpu(0).get_program_counter() == 0x1004, "PC advanced to 0x1004");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Preservation of Existing ELF Execution
// -----------------------------------------------------------------------------
bool test_preservation_of_existing_elf_execution() {
    std::cout << "[10] test_preservation_of_existing_elf_execution...\n";

    CPU cpu;
    Memory mem(0x20000);
    cpu.reset();

    // Write simple program: MOV X0, #99 (0xD2800C60u)
    mem.write_u32(0x0, 0xD2800C60u);
    cpu.set_program_counter(0x0);

    cpu.step(mem);
    STAGE73_ASSERT(cpu.read_register(0) == 99, "X0 = 99 in standalone execution");
    STAGE73_ASSERT(cpu.get_program_counter() == 4, "PC advanced to 4");

    return true;
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 73: Virtual Firmware Architecture & Boot Lifecycle Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*func)(), const char* name) {
        if (func()) {
            std::cout << "[PASS] " << name << "\n\n";
            ++passed;
        } else {
            std::cerr << "[FAIL] " << name << "\n\n";
            ++failed;
        }
    };

    run_test(test_successful_initialization_and_handoff, "Successful Initialization and Guest Handoff");
    run_test(test_invalid_boot_state_transitions, "Invalid Boot State Transitions");
    run_test(test_misaligned_address_rejection, "Misaligned Address Rejection");
    run_test(test_out_of_bounds_address_rejection, "Out-of-Bounds Address Rejection");
    run_test(test_address_collision_rejection, "Address Collision Rejection");
    run_test(test_smp_secondary_cores_quiescent, "SMP Secondary Cores Quiescent State");
    run_test(test_repeated_boot_and_reset, "Repeated Boot and Reset Lifecycle");
    run_test(test_halt_and_shutdown_transitions, "Halt and Shutdown Transitions");
    run_test(test_step_execution_after_handoff, "Step Execution After Handoff");
    run_test(test_preservation_of_existing_elf_execution, "Preservation of Existing ELF Execution");

    std::cout << "Stage 73 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

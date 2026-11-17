// ============================================================================
// Stage 213.20: Minimal Home-Screen-Like Guest Shell Execution Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/compositor.h"
#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "loader/embedded_interactive_shell_workload_213.inl"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::kernel;
using namespace winios::loader;
using namespace winios::memory;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_guest_shell_execution_and_touch_interaction() {
    std::cout << "[Test 1] Verify Genuine Freestanding ARM64 Mach-O Shell Execution at EL0...\n";

    // 1. Validate Mach-O Header
    std::string err;
    TEST_CHECK(kGuestShellMacho_len > 0, "Guest shell binary size > 0");
    TEST_CHECK(MachoUserLoader::validate_header(kGuestShellMacho_data, kGuestShellMacho_len, &err),
               "Guest shell Mach-O header is valid ARM64");

    // 2. Set up Memory and Address Space (16KB granule)
    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* shell_proc = proc_mgr.create_process("guest_shell", 1);
    TEST_CHECK(shell_proc != nullptr, "Created shell process with PID 1");

    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "shell_vm");

    // 3. Load Mach-O segments into user address space
    std::vector<std::uint8_t> bin(kGuestShellMacho_data, kGuestShellMacho_data + kGuestShellMacho_len);
    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_CHECK(img.valid, "Guest shell Mach-O segments loaded into EL0 address space");
    TEST_CHECK(img.entry_point != 0, "Entry point is non-zero");

    // 4. Map user stack
    constexpr std::uint64_t kUserStackTop = 0x70000ULL;
    std::uint64_t stack_bottom_va = 0;
    TEST_CHECK(uas.allocate_user_stack(kUserStackTop, 0x10000, stack_bottom_va), "Allocate user stack region");

    // 5. Initialize Graphics Subsystem
    DisplayConfiguration disp_cfg{};
    disp_cfg.width = 800;
    disp_cfg.height = 1200;
    disp_cfg.row_stride = 800 * 4;

    auto virtual_display = std::make_shared<VirtualDisplay>(disp_cfg);
    auto compositor = std::make_shared<Compositor>(virtual_display);
    auto surface_mgr = std::make_unique<SurfaceResourceManager>();
    auto window_mgr = std::make_shared<WindowManager>();
    auto input_router = std::make_unique<InputRouter>(window_mgr);

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);
    disp.set_surface_resource_manager(surface_mgr.get());
    disp.set_window_manager(window_mgr.get());
    disp.set_compositor(compositor.get());
    disp.set_input_router(input_router.get());

    // 6. Set up CPU registers and MMU for EL0 entry
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x00300000ULL);

    const std::uint64_t vbar_pa = ppm.allocate_page();
    const std::uint64_t vbar_va = 0x80000ULL;
    cpu.set_vbar_el1(vbar_va);
    mem.write_u32(static_cast<std::size_t>(vbar_pa + 0x400u), 0xD503201Fu); // NOP
    uas.map_user_page(vbar_va, vbar_pa, memory::PagePermissions::UserExecute);

    const std::uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa());
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));

    cpu.set_current_el(0);
    cpu.set_program_counter(img.entry_point);
    cpu.set_sp_el0(kUserStackTop - 64);

    // Run guest instructions to startup and create window
    std::uint64_t instructions_executed = 0;
    constexpr std::uint64_t kMaxInstructions = 1000;

    while (instructions_executed < kMaxInstructions) {
        cpu.step(mem);
        instructions_executed++;

        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            disp.handle_syscall(cpu, *shell_proc, uas, sched);
            cpu.set_current_el(0);
            cpu.set_program_counter(cpu.get_elr_el1());

            // If window was created and initial frame presented, break to inject input
            if (window_mgr->window_count() > 0 && compositor->stats().total_frames > 0) {
                break;
            }
        }
    }

    TEST_CHECK(instructions_executed > 10, "Guest shell executed ARM64 instructions at EL0");
    TEST_CHECK(console.get_output().find("[SHELL] WinIOS Home Shell Initialized") != std::string::npos,
               "Shell announced startup via SYS_write stdout");
    TEST_CHECK(window_mgr->window_count() == 1, "Shell window created in WindowManager");
    TEST_CHECK(surface_mgr->total_surface_count() == 1, "Shell surface created in SurfaceResourceManager");
    TEST_CHECK(compositor->stats().total_frames >= 1, "Compositor presented initial shell frame");

    // 7. Inject Touch on App 1 (Counter) Icon at (150, 250)
    InputEvent touch_app1{};
    touch_app1.type = InputEventType::TouchBegin;
    touch_app1.touch.contact_id = 0;
    touch_app1.touch.x = 150;
    touch_app1.touch.y = 250;
    touch_app1.timestamp_us = 10000;
    input_router->route_event(touch_app1);

    // Step shell until it consumes event
    for (int step = 0; step < 100; ++step) {
        cpu.step(mem);
        instructions_executed++;

        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            disp.handle_syscall(cpu, *shell_proc, uas, sched);
            cpu.set_current_el(0);
            cpu.set_program_counter(cpu.get_elr_el1());

            if (console.get_output().find("[SHELL] Launching Counter App") != std::string::npos) {
                break;
            }
        }
    }

    TEST_CHECK(console.get_output().find("[SHELL] Launching Counter App") != std::string::npos,
               "Shell detected touch on App 1 and output launch announcement");

    // 8. Inject Touch on App 2 (Notes) Icon at (400, 250)
    InputEvent touch_app2{};
    touch_app2.type = InputEventType::TouchBegin;
    touch_app2.touch.contact_id = 0;
    touch_app2.touch.x = 400;
    touch_app2.touch.y = 250;
    touch_app2.timestamp_us = 20000;
    input_router->route_event(touch_app2);

    for (int step = 0; step < 100; ++step) {
        cpu.step(mem);
        instructions_executed++;

        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            disp.handle_syscall(cpu, *shell_proc, uas, sched);
            cpu.set_current_el(0);
            cpu.set_program_counter(cpu.get_elr_el1());

            if (console.get_output().find("[SHELL] Launching Notes App") != std::string::npos) {
                break;
            }
        }
    }

    TEST_CHECK(console.get_output().find("[SHELL] Launching Notes App") != std::string::npos,
               "Shell detected touch on App 2 and output launch announcement");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.20: Guest Shell Execution Tests   \n";
    std::cout << "====================================================\n";

    test_guest_shell_execution_and_touch_interaction();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

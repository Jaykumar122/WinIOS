// ============================================================================
// Stage 213.21: Interactive Guest Applications Execution Tests
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

void test_counter_app_execution_and_touch() {
    std::cout << "[Test 1] Verify Counter App Launch, Touch Increment and Clean Exit...\n";

    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* app_proc = proc_mgr.create_process("counter_app", 20);
    TEST_CHECK(app_proc != nullptr, "Created counter app process with PID 20");

    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "counter_vm");

    std::vector<std::uint8_t> bin(kCounterAppMacho_data, kCounterAppMacho_data + kCounterAppMacho_len);
    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_CHECK(img.valid, "Counter app Mach-O loaded into address space");

    constexpr std::uint64_t kStackTop = 0x70000ULL;
    std::uint64_t stack_bottom_va = 0;
    TEST_CHECK(uas.allocate_user_stack(kStackTop, 0x10000, stack_bottom_va), "Allocate user stack");

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
    cpu.set_sp_el0(kStackTop - 64);

    // Run until window created & initial frame presented
    for (int step = 0; step < 200; ++step) {
        cpu.step(mem);
        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            disp.handle_syscall(cpu, *app_proc, uas, sched);
            cpu.set_current_el(0);
            cpu.set_program_counter(cpu.get_elr_el1());
            if (window_mgr->window_count() == 1 && compositor->stats().total_frames > 0) {
                break;
            }
        }
    }

    TEST_CHECK(console.get_output().find("[APP1] Counter App Initialized") != std::string::npos,
               "Counter App announced startup");
    TEST_CHECK(window_mgr->window_count() == 1, "Counter App window created");

    // Inject touch on Increment button at screen (200, 490) (window is at [50, 150], local (150, 340))
    InputEvent touch_inc{};
    touch_inc.type = InputEventType::TouchBegin;
    touch_inc.touch.contact_id = 0;
    touch_inc.touch.x = 200;
    touch_inc.touch.y = 490;
    touch_inc.timestamp_us = 10000;
    input_router->route_event(touch_inc);

    for (int step = 0; step < 100; ++step) {
        cpu.step(mem);
        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            disp.handle_syscall(cpu, *app_proc, uas, sched);
            cpu.set_current_el(0);
            cpu.set_program_counter(cpu.get_elr_el1());
            if (console.get_output().find("[APP1] Button Tapped: Counter++") != std::string::npos) {
                break;
            }
        }
    }

    TEST_CHECK(console.get_output().find("[APP1] Button Tapped: Counter++") != std::string::npos,
               "Counter App handled touch and incremented counter");

    // Inject touch on Exit button at screen (550, 490) (local (500, 340))
    InputEvent touch_exit{};
    touch_exit.type = InputEventType::TouchBegin;
    touch_exit.touch.contact_id = 0;
    touch_exit.touch.x = 550;
    touch_exit.touch.y = 490;
    touch_exit.timestamp_us = 20000;
    input_router->route_event(touch_exit);

    for (int step = 0; step < 100; ++step) {
        cpu.step(mem);
        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            disp.handle_syscall(cpu, *app_proc, uas, sched);
            cpu.set_current_el(0);
            cpu.set_program_counter(cpu.get_elr_el1());
            if (app_proc->is_terminated()) {
                break;
            }
        }
    }

    TEST_CHECK(console.get_output().find("[APP1] Counter App Exiting") != std::string::npos,
               "Counter App announced clean exit");
    TEST_CHECK(window_mgr->window_count() == 0, "Counter App window destroyed on exit");
    TEST_CHECK(surface_mgr->total_surface_count() == 0, "Counter App surface destroyed on exit");
    TEST_CHECK(surface_mgr->total_allocated_bytes() == 0, "All display memory reclaimed");
}

void test_notes_app_execution_and_touch() {
    std::cout << "[Test 2] Verify Notes App Launch, Checkbox Touch and Clean Close...\n";

    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* app_proc = proc_mgr.create_process("notes_app", 30);
    TEST_CHECK(app_proc != nullptr, "Created notes app process with PID 30");

    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "notes_vm");

    std::vector<std::uint8_t> bin(kNotesAppMacho_data, kNotesAppMacho_data + kNotesAppMacho_len);
    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_CHECK(img.valid, "Notes app Mach-O loaded into address space");

    constexpr std::uint64_t kStackTop = 0x70000ULL;
    std::uint64_t stack_bottom_va = 0;
    TEST_CHECK(uas.allocate_user_stack(kStackTop, 0x10000, stack_bottom_va), "Allocate user stack");

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
    cpu.set_sp_el0(kStackTop - 64);

    // Step until window initialized
    for (int step = 0; step < 200; ++step) {
        cpu.step(mem);
        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            disp.handle_syscall(cpu, *app_proc, uas, sched);
            cpu.set_current_el(0);
            cpu.set_program_counter(cpu.get_elr_el1());
            if (window_mgr->window_count() == 1 && compositor->stats().total_frames > 0) {
                break;
            }
        }
    }

    TEST_CHECK(console.get_output().find("[APP2] Notes App Initialized") != std::string::npos,
               "Notes App announced startup");
    TEST_CHECK(window_mgr->window_count() == 1, "Notes App window created");

    // Inject touch on Checkbox at screen (125, 375) (window is at [50, 150], local (75, 225))
    InputEvent touch_check{};
    touch_check.type = InputEventType::TouchBegin;
    touch_check.touch.contact_id = 0;
    touch_check.touch.x = 125;
    touch_check.touch.y = 375;
    touch_check.timestamp_us = 15000;
    input_router->route_event(touch_check);

    for (int step = 0; step < 100; ++step) {
        cpu.step(mem);
        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            disp.handle_syscall(cpu, *app_proc, uas, sched);
            cpu.set_current_el(0);
            cpu.set_program_counter(cpu.get_elr_el1());
            if (console.get_output().find("[APP2] Note Item Checked") != std::string::npos) {
                break;
            }
        }
    }

    TEST_CHECK(console.get_output().find("[APP2] Note Item Checked") != std::string::npos,
               "Notes App handled touch and toggled checkbox");

    // Inject touch on Close Note button at screen (400, 885) (local (350, 735))
    InputEvent touch_close{};
    touch_close.type = InputEventType::TouchBegin;
    touch_close.touch.contact_id = 0;
    touch_close.touch.x = 400;
    touch_close.touch.y = 885;
    touch_close.timestamp_us = 25000;
    input_router->route_event(touch_close);

    for (int step = 0; step < 100; ++step) {
        cpu.step(mem);
        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            disp.handle_syscall(cpu, *app_proc, uas, sched);
            cpu.set_current_el(0);
            cpu.set_program_counter(cpu.get_elr_el1());
            if (app_proc->is_terminated()) {
                break;
            }
        }
    }

    TEST_CHECK(console.get_output().find("[APP2] Notes App Exiting") != std::string::npos,
               "Notes App announced clean exit");
    TEST_CHECK(window_mgr->window_count() == 0, "Notes App window destroyed on close");
    TEST_CHECK(surface_mgr->total_surface_count() == 0, "Notes App surface destroyed on close");
    TEST_CHECK(surface_mgr->total_allocated_bytes() == 0, "All display memory reclaimed");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.21: Interactive Apps Tests        \n";
    std::cout << "====================================================\n";

    test_counter_app_execution_and_touch();
    test_notes_app_execution_and_touch();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

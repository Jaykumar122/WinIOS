// ============================================================================
// Stage 213.23: Complete End-to-End Interactive Demonstration
// Shell -> App 1 (Counter) -> Touch -> Exit -> Shell -> App 2 (Notes) -> Touch -> Exit -> Shell
// ============================================================================
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/compositor.h"
#include "graphics/display_snapshot.h"
#include "graphics/display_surface.h"
#include "graphics/frame_scheduler.h"
#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "input/virtual_input_device.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "loader/embedded_interactive_shell_workload_213.inl"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "ui/display_window.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::kernel;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::ui;

namespace {

int g_failures = 0;

#define E2E_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[E2E_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [E2E_OK] " << (msg) << "\n"; \
        } \
    } while (0)

std::string get_screenshot_path(const std::string& filename) {
    if (std::filesystem::exists("WinIOS/artifacts") || std::filesystem::exists("WinIOS")) {
        std::filesystem::create_directories("WinIOS/artifacts/campaign-213");
        return "WinIOS/artifacts/campaign-213/" + filename;
    }
    std::filesystem::create_directories("artifacts/campaign-213");
    return "artifacts/campaign-213/" + filename;
}

struct GuestProcessInstance {
    std::uint32_t pid{0};
    BsdProcess* proc{nullptr};
    std::unique_ptr<UserAddressSpace> uas;
    std::uint64_t entry_point{0};
    std::uint64_t stack_top{0x70000ULL};

    GuestProcessInstance(std::uint32_t p, BsdProcessManager& proc_mgr, PhysicalPageManager& ppm, Memory& mem, const std::string& name)
        : pid(p) {
        proc = proc_mgr.create_process(name, p);
        const std::uint64_t root_l2 = ppm.allocate_page();
        uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2, name + "_vm");
    }
};

bool step_guest_process(
    GuestProcessInstance& inst,
    CPU& cpu,
    Memory& mem,
    UserSyscallDispatcher& disp,
    PriorityScheduler& sched,
    std::uint64_t max_steps,
    std::uint64_t& out_retired) {

    // Configure CPU MMU for this process
    cpu.get_mmu().set_ttbr0_el1(inst.uas->root_table_pa());
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_current_el(0);
    cpu.set_program_counter(inst.entry_point);
    cpu.set_sp_el0(inst.stack_top - 64);

    for (std::uint64_t s = 0; s < max_steps; ++s) {
        if (!inst.proc || inst.proc->is_terminated()) {
            return true;
        }

        cpu.step(mem);
        out_retired++;

        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            disp.handle_syscall(cpu, *inst.proc, *inst.uas, sched);
            cpu.set_current_el(0);
            cpu.set_program_counter(cpu.get_elr_el1());
            inst.entry_point = cpu.get_elr_el1();
            inst.stack_top = cpu.get_sp_el0() + 64;
            continue;
        }

        inst.entry_point = cpu.get_program_counter();
        inst.stack_top = cpu.get_sp_el0() + 64;
    }
    return true;
}

void test_end_to_end_interactive_flow() {
    std::cout << "[E2E Demonstration] Initiating 13-Step Full Interactive Workload...\n";

    // 1. Initialize Memory and Hardware
    Memory mem(0x2000000); // 32 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x00300000ULL);

    const std::uint64_t vbar_pa = ppm.allocate_page();
    const std::uint64_t vbar_va = 0x80000ULL;
    cpu.set_vbar_el1(vbar_va);
    mem.write_u32(static_cast<std::size_t>(vbar_pa + 0x400u), 0xD503201Fu); // NOP

    const std::uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    // 2. Setup Virtual Display & Host Presentation Window
    DisplayConfiguration disp_cfg{};
    disp_cfg.width = 800;
    disp_cfg.height = 1200;
    disp_cfg.row_stride = 800 * 4;

    auto virtual_display = std::make_shared<VirtualDisplay>(disp_cfg);
    auto compositor = std::make_shared<Compositor>(virtual_display);
    auto surface_mgr = std::make_unique<SurfaceResourceManager>();
    auto window_mgr = std::make_shared<WindowManager>();
    auto input_router = std::make_unique<InputRouter>(window_mgr);
    VirtualInputDevice input_device(1);

    disp.set_surface_resource_manager(surface_mgr.get());
    disp.set_window_manager(window_mgr.get());
    disp.set_compositor(compositor.get());
    disp.set_input_router(input_router.get());

    input_device.set_dispatch_callback([&](const InputEvent& evt) {
        input_router->route_event(evt);
    });

    DisplayWindowConfig host_win_cfg{};
    host_win_cfg.native_width = disp_cfg.width;
    host_win_cfg.native_height = disp_cfg.height;
    host_win_cfg.headless = true;

    DisplayWindow host_win(host_win_cfg);
    E2E_CHECK(host_win.create(), "Step 1: Host presentation window created");

    // 3. Launch Shell (PID 1)
    GuestProcessInstance shell(1, proc_mgr, ppm, mem, "guest_shell");
    shell.uas->map_user_page(vbar_va, vbar_pa, memory::PagePermissions::UserExecute);

    std::vector<std::uint8_t> shell_bin(kGuestShellMacho_data, kGuestShellMacho_data + kGuestShellMacho_len);
    auto shell_img = MachoUserLoader::load_image(shell_bin, *shell.uas, ppm, mem);
    E2E_CHECK(shell_img.valid, "Step 2: Shell ARM64 Mach-O loaded into PID 1");
    shell.entry_point = shell_img.entry_point;

    std::uint64_t stack_bot = 0;
    shell.uas->allocate_user_stack(shell.stack_top, 0x10000, stack_bot);

    std::uint64_t total_guest_instructions = 0;
    step_guest_process(shell, cpu, mem, disp, sched, 100, total_guest_instructions);

    E2E_CHECK(window_mgr->window_count() == 1, "Step 3: Shell created window");
    E2E_CHECK(compositor->stats().total_frames >= 1, "Step 4: Compositor composited initial shell frame");

    host_win.present_frame(virtual_display->raw_data(), disp_cfg.width, disp_cfg.height);
    E2E_CHECK(host_win.save_screenshot_bmp(get_screenshot_path("screenshot_01_shell_homescreen.bmp")), "Captured screenshot 1: Home Screen Shell");

    // 4. Touch App 1 (Counter) Icon at (150, 250)
    input_device.inject_touch_begin(0, 150, 250, 10000ULL);
    step_guest_process(shell, cpu, mem, disp, sched, 50, total_guest_instructions);
    input_device.inject_touch_end(0, 15000ULL);

    E2E_CHECK(console.get_output().find("[SHELL] Launching Counter App") != std::string::npos,
              "Step 5: Shell received touch input and launched Counter App");

    // 5. Spawn Counter App (PID 2)
    GuestProcessInstance counter_app(2, proc_mgr, ppm, mem, "counter_app");
    counter_app.uas->map_user_page(vbar_va, vbar_pa, memory::PagePermissions::UserExecute);

    std::vector<std::uint8_t> counter_bin(kCounterAppMacho_data, kCounterAppMacho_data + kCounterAppMacho_len);
    auto counter_img = MachoUserLoader::load_image(counter_bin, *counter_app.uas, ppm, mem);
    E2E_CHECK(counter_img.valid, "Step 6: Counter App ARM64 Mach-O loaded into PID 2");
    counter_app.entry_point = counter_img.entry_point;
    counter_app.uas->allocate_user_stack(counter_app.stack_top, 0x10000, stack_bot);

    step_guest_process(counter_app, cpu, mem, disp, sched, 100, total_guest_instructions);
    E2E_CHECK(window_mgr->window_count() == 2, "Step 7: Counter App window created (2 active windows)");

    host_win.present_frame(virtual_display->raw_data(), disp_cfg.width, disp_cfg.height);
    E2E_CHECK(host_win.save_screenshot_bmp(get_screenshot_path("screenshot_02_counter_app_launched.bmp")), "Captured screenshot 2: Counter App Launched");

    // 6. Touch "+1 Increment" button at screen (200, 490) in Counter App
    input_device.inject_touch_begin(0, 200, 490, 25000ULL);
    step_guest_process(counter_app, cpu, mem, disp, sched, 50, total_guest_instructions);
    input_device.inject_touch_end(0, 30000ULL);

    E2E_CHECK(console.get_output().find("[APP1] Button Tapped: Counter++") != std::string::npos,
              "Step 8: Counter App processed guest touch event at EL0");

    host_win.present_frame(virtual_display->raw_data(), disp_cfg.width, disp_cfg.height);
    E2E_CHECK(host_win.save_screenshot_bmp(get_screenshot_path("screenshot_03_counter_app_incremented.bmp")), "Captured screenshot 3: Counter App Incremented");

    // 7. Touch "Exit" button at screen (550, 490) in Counter App
    input_device.inject_touch_begin(0, 550, 490, 40000ULL);
    step_guest_process(counter_app, cpu, mem, disp, sched, 50, total_guest_instructions);
    input_device.inject_touch_end(0, 45000ULL);

    E2E_CHECK(counter_app.proc->is_terminated(),
              "Step 9: Counter App exited cleanly via SYS_exit");
    E2E_CHECK(window_mgr->window_count() == 1, "Window count returned to 1 (Shell)");
    E2E_CHECK(window_mgr->focused_pid() == 1, "Focus returned to Shell");

    host_win.present_frame(virtual_display->raw_data(), disp_cfg.width, disp_cfg.height);
    E2E_CHECK(host_win.save_screenshot_bmp(get_screenshot_path("screenshot_04_return_to_shell.bmp")), "Captured screenshot 4: Return to Shell");

    // 8. Touch App 2 (Notes) Icon at (400, 250)
    input_device.inject_touch_begin(0, 400, 250, 50000ULL);
    step_guest_process(shell, cpu, mem, disp, sched, 50, total_guest_instructions);
    input_device.inject_touch_end(0, 55000ULL);

    E2E_CHECK(console.get_output().find("[SHELL] Launching Notes App") != std::string::npos,
              "Step 10: Shell received touch input and launched Notes App");

    // 9. Spawn Notes App (PID 3)
    GuestProcessInstance notes_app(3, proc_mgr, ppm, mem, "notes_app");
    notes_app.uas->map_user_page(vbar_va, vbar_pa, memory::PagePermissions::UserExecute);

    std::vector<std::uint8_t> notes_bin(kNotesAppMacho_data, kNotesAppMacho_data + kNotesAppMacho_len);
    auto notes_img = MachoUserLoader::load_image(notes_bin, *notes_app.uas, ppm, mem);
    E2E_CHECK(notes_img.valid, "Step 11: Notes App ARM64 Mach-O loaded into PID 3");
    notes_app.entry_point = notes_img.entry_point;
    notes_app.uas->allocate_user_stack(notes_app.stack_top, 0x10000, stack_bot);

    step_guest_process(notes_app, cpu, mem, disp, sched, 100, total_guest_instructions);
    host_win.present_frame(virtual_display->raw_data(), disp_cfg.width, disp_cfg.height);
    E2E_CHECK(host_win.save_screenshot_bmp(get_screenshot_path("screenshot_05_notes_app_launched.bmp")), "Captured screenshot 5: Notes App Launched");

    // 10. Touch Checkbox at screen (125, 375) in Notes App
    input_device.inject_touch_begin(0, 125, 375, 60000ULL);
    step_guest_process(notes_app, cpu, mem, disp, sched, 50, total_guest_instructions);
    input_device.inject_touch_end(0, 65000ULL);

    E2E_CHECK(console.get_output().find("[APP2] Note Item Checked") != std::string::npos,
              "Step 12: Notes App processed guest touch and toggled checkbox");

    host_win.present_frame(virtual_display->raw_data(), disp_cfg.width, disp_cfg.height);
    E2E_CHECK(host_win.save_screenshot_bmp(get_screenshot_path("screenshot_06_notes_app_checked.bmp")), "Captured screenshot 6: Notes App Checked");

    // 11. Touch Close Note button at screen (400, 885) in Notes App
    input_device.inject_touch_begin(0, 400, 885, 70000ULL);
    step_guest_process(notes_app, cpu, mem, disp, sched, 50, total_guest_instructions);
    input_device.inject_touch_end(0, 75000ULL);

    E2E_CHECK(notes_app.proc->is_terminated(),
              "Step 13: Notes App exited cleanly via SYS_exit");
    E2E_CHECK(window_mgr->window_count() == 1, "Window count returned to 1 (Shell)");

    // 12. Final closeout
    host_win.present_frame(virtual_display->raw_data(), disp_cfg.width, disp_cfg.height);
    E2E_CHECK(host_win.save_screenshot_bmp(get_screenshot_path("screenshot_07_shell_closeout.bmp")), "Captured screenshot 7: Shell Closeout");
    host_win.close();

    std::cout << "\n[E2E Metrics]\n";
    std::cout << "  - Total Retired Guest ARM64 Instructions: " << total_guest_instructions << "\n";
    std::cout << "  - Total Display Syscalls: " << disp.stats().total_display_syscalls << "\n";
    std::cout << "  - Total Input Polls: " << disp.stats().total_input_polls << "\n";
    std::cout << "  - Total Compositor Frames: " << compositor->stats().total_frames << "\n";
    std::cout << "  - Total Host Frames Presented: " << host_win.total_frames_presented() << "\n";
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 213.23: End-to-End Demonstration      \n";
    std::cout << "====================================================\n";

    test_end_to_end_interactive_flow();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL E2E WORKLOAD STEPS PASSED" : "E2E FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}

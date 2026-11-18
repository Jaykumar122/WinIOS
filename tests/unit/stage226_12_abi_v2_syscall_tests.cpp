#include <iostream>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "kernel/bsd_process.h"
#include "kernel/user_syscall_dispatcher.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "runtime/system_clipboard.h"
#include "winios/window/window.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::graphics;
using namespace winios::runtime;
using namespace winios::memory;

int main() {
    std::cout << "[Test Stage 226.12] Category C ABI v2.0 Syscalls (561-570)...\n" << std::flush;

    CPU cpu;
    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    WindowManager wm;
    dispatcher.set_window_manager(&wm);

    SurfaceResourceManager srm;
    dispatcher.set_surface_resource_manager(&srm);

    SystemClipboard clipboard;
    dispatcher.set_system_clipboard(&clipboard);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("desktop_app", 1);
    TEST_ASSERT(proc != nullptr);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "test_uas");
    proc->set_address_space(&uas);

    // Map a user data page at 0x30000
    const std::uint64_t data_page_pa = ppm.allocate_page();
    uas.map_user_page(0x30000ULL, data_page_pa, PagePermissions::UserReadWrite);

    // 1. Syscall 561: SYS_WINIOS_WINDOW_CREATE_V2
    // Title string at 0x30000: "My Desktop Window"
    const std::string title = "My Desktop Window";
    uas.copyin(0x30000ULL, title.c_str(), title.size() + 1);

    cpu.write_register(16, kSysWiniosWindowCreateV2);
    cpu.write_register(0, 0x30000ULL); // title
    cpu.write_register(1, 100);        // x
    cpu.write_register(2, 80);         // y
    cpu.write_register(3, 400);        // w
    cpu.write_register(4, 300);        // h

    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint64_t win_handle = cpu.read_register(0);
    TEST_ASSERT(win_handle != 0);
    TEST_ASSERT(wm.is_handle_valid(win_handle));

    // 2. Syscall 564: SYS_WINIOS_WINDOW_GET_STATE
    cpu.write_register(16, kSysWiniosWindowGetState);
    cpu.write_register(0, win_handle);
    cpu.write_register(1, 0x30700ULL);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    winios_window_state_data_t st_check{};
    TEST_ASSERT(uas.copyout(reinterpret_cast<void*>(0x30700ULL), &st_check, sizeof(st_check)));
    TEST_ASSERT(st_check.state == static_cast<uint32_t>(WindowState::Normal));

    // 3. Syscall 563: SYS_WINIOS_WINDOW_SET_STATE (Minimize)
    cpu.write_register(16, kSysWiniosWindowSetState);
    cpu.write_register(0, win_handle);
    cpu.write_register(1, static_cast<uint64_t>(WindowState::Minimized));
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));

    auto win = wm.get_window_by_handle(win_handle);
    TEST_ASSERT(win != nullptr);
    TEST_ASSERT(win->state == WindowState::Minimized);

    // 4. Syscall 565: SYS_WINIOS_WINDOW_INVALIDATE_RECT
    winios_rect_t dmg_rect{10, 10, 50, 50};
    uas.copyin(0x30800ULL, &dmg_rect, sizeof(dmg_rect));

    cpu.write_register(16, kSysWiniosWindowInvalidateRect);
    cpu.write_register(0, win_handle);
    cpu.write_register(1, 0x30800ULL);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(win->is_dirty);

    // 5. Syscall 568: SYS_WINIOS_TEXT_INPUT_SUBMIT
    // Post text "Direct Event Text"
    const std::string text = "Direct Event Text";
    uas.copyin(0x30100ULL, text.c_str(), text.size() + 1);

    cpu.write_register(16, kSysWiniosTextInputSubmit);
    cpu.write_register(0, win_handle);
    cpu.write_register(1, 0x30100ULL);
    cpu.write_register(2, text.size());
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));

    // 6. Syscall 566: SYS_WINIOS_EVENT_POLL_V2
    // Struct buffer at 0x30200ULL (sizeof DesktopEvent is 128 bytes)
    cpu.write_register(16, kSysWiniosEventPollV2);
    cpu.write_register(0, win_handle);
    cpu.write_register(1, 0x30200ULL);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 1); // 1 event polled

    winios_desktop_event_t polled_ev{};
    TEST_ASSERT(uas.copyout(reinterpret_cast<void*>(0x30200ULL), &polled_ev, sizeof(polled_ev)));
    TEST_ASSERT(polled_ev.version == 0x00020000);
    TEST_ASSERT(polled_ev.event_type == static_cast<uint32_t>(DesktopEventType::TextInput));
    TEST_ASSERT(std::string(reinterpret_cast<const char*>(polled_ev.payload)) == text);

    // 7. Syscall 570: SYS_WINIOS_CLIPBOARD_SET_V2
    const std::string clip_content = "Copied to Desktop Clipboard";
    uas.copyin(0x30400ULL, clip_content.c_str(), clip_content.size());
    cpu.write_register(16, kSysWiniosClipboardSetV2);
    cpu.write_register(0, 0x30400ULL);
    cpu.write_register(1, clip_content.size());
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));

    // 8. Syscall 569: SYS_WINIOS_CLIPBOARD_GET_V2
    cpu.write_register(16, kSysWiniosClipboardGetV2);
    cpu.write_register(0, 0x30500ULL); // dst buf
    cpu.write_register(1, 256);        // dst cap
    cpu.write_register(2, 0x30600ULL); // out_len
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint64_t returned_len = cpu.read_register(0);
    TEST_ASSERT(returned_len == clip_content.size());

    std::vector<char> retrieved(clip_content.size() + 1, 0);
    TEST_ASSERT(uas.copyout(reinterpret_cast<void*>(0x30500ULL), retrieved.data(), clip_content.size()));
    TEST_ASSERT(std::string(retrieved.data()) == clip_content);

    // 9. Syscall 562: SYS_WINIOS_WINDOW_DESTROY_V2
    cpu.write_register(16, kSysWiniosWindowDestroyV2);
    cpu.write_register(0, win_handle);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(!wm.is_handle_valid(win_handle));

    // Stale handle destroy must fail
    cpu.write_register(16, kSysWiniosWindowDestroyV2);
    cpu.write_register(0, win_handle);
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, *proc, uas, sched));

    std::cout << "[Test Stage 226.12] PASSED successfully.\n" << std::flush;
    return 0;
}

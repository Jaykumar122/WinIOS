#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "winios/ui/toolkit.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "security/capability_manager.h"
#include "runtime/system_clipboard.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::runtime;
using namespace winios::security;

extern "C" {
int32_t winios_surface_fill(winios_wid_t wid, uint32_t x, uint32_t y, uint32_t width, uint32_t height, winios_color_t color) {
    (void)wid; (void)x; (void)y; (void)width; (void)height; (void)color;
    return 0;
}
int32_t winios_surface_update(winios_wid_t wid, const void* pixels, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    (void)wid; (void)pixels; (void)x; (void)y; (void)width; (void)height;
    return 0;
}
}

void test_button_component() {
    std::cout << "[Test 1] UI Button component events and state...\n";
    winios_button_t btn;
    winios_button_init(&btn, 10, 10, 80, 30, "OK");
    assert(btn.bounds.x == 10 && btn.bounds.width == 80);
    assert(btn.is_pressed == 0);
    assert(btn.is_hovered == 0);

    winios_event_v2_t evt{};

    // Mouse move outside button
    evt.type = WINIOS_EVENT_V2_MOUSE_MOVE;
    evt.x = 5;
    evt.y = 5;
    assert(winios_button_handle_event(&btn, &evt) == 0);
    assert(btn.is_hovered == 0);

    // Mouse move inside button
    evt.x = 20;
    evt.y = 20;
    assert(winios_button_handle_event(&btn, &evt) == 0);
    assert(btn.is_hovered == 1);

    // Mouse down inside button
    evt.type = WINIOS_EVENT_V2_MOUSE_DOWN;
    assert(winios_button_handle_event(&btn, &evt) == 0);
    assert(btn.is_pressed == 1);

    // Mouse up inside button -> Click!
    evt.type = WINIOS_EVENT_V2_MOUSE_UP;
    assert(winios_button_handle_event(&btn, &evt) == 1);
    assert(btn.is_pressed == 0);

    // Render test
    std::vector<uint32_t> fb(200 * 200, 0);
    winios_button_render(fb.data(), 200, &btn);
    // Button area should be non-zero
    assert(fb[15 * 200 + 15] != 0);

    std::cout << "[PASS] UI Button verified.\n";
}

void test_textinput_and_caret() {
    std::cout << "[Test 2] UI TextInput component and caret editing...\n";
    char buffer[64] = "Hello";
    winios_textinput_t ti;
    winios_textinput_init(&ti, 10, 10, 150, 24, buffer, sizeof(buffer));
    assert(ti.length == 5);
    assert(ti.cursor_pos == 5);
    assert(ti.is_focused == 0);

    winios_event_v2_t evt{};

    // Click inside to focus
    evt.type = WINIOS_EVENT_V2_MOUSE_DOWN;
    evt.x = 30; // ~character index (30-14)/8 = 2
    evt.y = 15;
    winios_textinput_handle_event(&ti, &evt);
    assert(ti.is_focused == 1);
    assert(ti.cursor_pos == 2);

    // Type character 'X'
    evt.type = WINIOS_EVENT_V2_KEY_CHAR;
    evt.key_code = 'X';
    int changed = winios_textinput_handle_event(&ti, &evt);
    assert(changed == 1);
    assert(std::strcmp(buffer, "HeXllo") == 0);
    assert(ti.cursor_pos == 3);
    assert(ti.length == 6);

    // Backspace
    evt.key_code = 8;
    changed = winios_textinput_handle_event(&ti, &evt);
    assert(changed == 1);
    assert(std::strcmp(buffer, "Hello") == 0);
    assert(ti.cursor_pos == 2);
    assert(ti.length == 5);

    // Left arrow key
    evt.type = WINIOS_EVENT_V2_KEY_DOWN;
    evt.key_code = 0x25;
    winios_textinput_handle_event(&ti, &evt);
    assert(ti.cursor_pos == 1);

    // Right arrow key
    evt.key_code = 0x27;
    winios_textinput_handle_event(&ti, &evt);
    assert(ti.cursor_pos == 2);

    // Render test
    std::vector<uint32_t> fb(200 * 200, 0);
    winios_textinput_render(fb.data(), 200, &ti);
    assert(fb[15 * 200 + 15] != 0);

    // Defocus on click outside
    evt.type = WINIOS_EVENT_V2_MOUSE_DOWN;
    evt.x = 200;
    evt.y = 200;
    winios_textinput_handle_event(&ti, &evt);
    assert(ti.is_focused == 0);

    std::cout << "[PASS] UI TextInput verified.\n";
}

void test_checkbox_and_progressbar() {
    std::cout << "[Test 3] Checkbox and ProgressBar controls...\n";
    winios_checkbox_t cb;
    winios_checkbox_init(&cb, 10, 10, 100, 20, "Enable", 0);
    assert(cb.checked == 0);

    winios_event_v2_t evt{};
    evt.type = WINIOS_EVENT_V2_MOUSE_UP;
    evt.x = 15;
    evt.y = 15;
    int toggled = winios_checkbox_handle_event(&cb, &evt);
    assert(toggled == 1);
    (void)toggled;
    assert(cb.checked == 1);

    std::vector<uint32_t> fb(200 * 200, 0);
    winios_checkbox_render(fb.data(), 200, &cb);
    assert(fb[15 * 200 + 15] != 0);

    winios_progressbar_t pb;
    winios_progressbar_init(&pb, 10, 40, 100, 16, 0, 100, 50);
    assert(pb.current_val == 50);
    winios_progressbar_render(fb.data(), 200, &pb);
    assert(fb[45 * 200 + 15] != 0);

    std::cout << "[PASS] Checkbox and ProgressBar verified.\n";
}

void test_clipboard_syscalls() {
    std::cout << "[Test 4] System clipboard syscalls and permissions...\n";
    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "uas_clip_test");

    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    auto cap_mgr = std::make_shared<CapabilityManager>();
    disp.set_capability_manager(cap_mgr.get());

    SystemClipboard clipboard(cap_mgr);
    disp.set_system_clipboard(&clipboard);

    BsdProcess proc(201, 1, "clip_app");
    proc.set_address_space(&uas);
    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);
    CPU cpu;

    // Grant capabilities
    cap_mgr->grant(201, Capability::ClipboardRead);
    cap_mgr->grant(201, Capability::ClipboardWrite);

    // Setup guest string "WinIOS 222 Clipboard Text"
    const char* sample_text = "WinIOS 222 Clipboard Text";
    const uint64_t text_va = 0x200000;
    const uint64_t text_pa = ppm.allocate_page();
    uas.map_user_page(text_va, text_pa, memory::PagePermissions::UserReadWrite);
    uas.copyin(text_va, sample_text, std::strlen(sample_text) + 1);

    // Call kSysWiniosClipboardSet (535)
    cpu.write_register(16, kSysWiniosClipboardSet);
    cpu.write_register(0, text_va);
    cpu.write_register(1, std::strlen(sample_text));
    assert(disp.handle_syscall(cpu, proc, uas, sched));
    assert(cpu.read_register(0) == 0);

    // Verify host clipboard content
    std::string clip_content;
    assert(clipboard.get_text(201, clip_content));
    assert(clip_content == sample_text);

    // Call kSysWiniosClipboardGet (534)
    const uint64_t read_va = 0x201000;
    const uint64_t read_pa = ppm.allocate_page();
    uas.map_user_page(read_va, read_pa, memory::PagePermissions::UserReadWrite);

    cpu.write_register(16, kSysWiniosClipboardGet);
    cpu.write_register(0, read_va);
    cpu.write_register(1, 128);
    assert(disp.handle_syscall(cpu, proc, uas, sched));
    assert(cpu.read_register(0) == std::strlen(sample_text));

    char read_back[128]{};
    uas.copyout(reinterpret_cast<const void*>(read_va), read_back, sizeof(read_back));
    assert(std::strcmp(read_back, sample_text) == 0);

    // Capability revocation test
    cap_mgr->revoke(201, Capability::ClipboardRead);
    cpu.write_register(16, kSysWiniosClipboardGet);
    cpu.write_register(0, read_va);
    cpu.write_register(1, 128);
    assert(!disp.handle_syscall(cpu, proc, uas, sched));
    assert(cpu.read_register(0) == kErrnoEPERM);

    std::cout << "[PASS] System clipboard syscalls verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.13 & 222.14: UI TOOLKIT & CLIPBOARD TESTS\n";
    std::cout << "========================================\n";

    test_button_component();
    test_textinput_and_caret();
    test_checkbox_and_progressbar();
    test_clipboard_syscalls();

    std::cout << "\nALL STAGE 222.13 & 222.14 TESTS PASSED (100%).\n";
    return 0;
}

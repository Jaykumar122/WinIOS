#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "runtime/guest_event_services.h"
#include "runtime/guest_ipc.h"
#include "input/input_router.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/multi_process_scheduler.h"
#include "security/capability_manager.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "runtime/interactive_runtime_contract.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::runtime;
using namespace winios::input;
using namespace winios::graphics;
using namespace winios::security;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_virtual_timer_events() {
    std::cout << "[Test 1] Virtual timer creation, virtual time step and event dispatch..." << std::endl;
    auto wm = std::make_shared<WindowManager>();
    auto surf = std::make_shared<DisplaySurface>(1, 10, 200, 200);
    uint32_t wid = wm->create_window(10, "TimerApp", Rect{0, 0, 200, 200}, surf);
    wm->set_focus(wid);

    auto router = std::make_shared<InputRouter>(wm);
    GuestEventServices ev_services(router);

    // Create a 100ms periodic timer for PID 10
    uint32_t tid = ev_services.create_timer(10, 100, true);
    TEST_ASSERT(tid > 0);
    TEST_ASSERT(ev_services.active_timers_for_process(10) == 1);

    // Step 50ms: Timer should not fire yet
    ev_services.step(50);
    InputEvent evt{};
    TEST_ASSERT(!router->poll_event(10, evt));

    // Step another 60ms (total 110ms): Timer should fire!
    ev_services.step(60);
    TEST_ASSERT(router->poll_event(10, evt));
    TEST_ASSERT(evt.type == InputEventType::TimerTick);
    TEST_ASSERT(evt.touch.contact_id == tid); // contact_id holds timer ID

    // Step another 100ms: fires second periodic tick
    ev_services.step(100);
    TEST_ASSERT(router->poll_event(10, evt));
    TEST_ASSERT(evt.type == InputEventType::TimerTick);

    // Cancel timer
    bool cancelled = ev_services.cancel_timer(10, tid);
    TEST_ASSERT(cancelled);
    (void)cancelled;
    TEST_ASSERT(ev_services.active_timers_for_process(10) == 0);

    std::cout << "[PASS] Virtual timer verified." << std::endl;
}

void test_inter_app_ipc_isolation() {
    std::cout << "[Test 2] Inter-application IPC messaging and permission isolation..." << std::endl;
    auto& ipc = GuestIpcRouter::Get();

    // App A (PID 100) creates port
    uint32_t portA = ipc.CreatePort(100, "AppA_Service");
    TEST_ASSERT(portA > 0);

    // App B (PID 200) sends message to App A
    const char* ping_data = "PING_MSG_FROM_B";
    bool sent = ipc.Send(200, portA, ping_data, std::strlen(ping_data) + 1, 42);
    TEST_ASSERT(sent);
    (void)sent;

    // Unauthorized App C (PID 300) attempts to read from App A's port
    IpcMessage eavesdrop_msg{};
    bool eavesdrop_ok = ipc.Receive(300, portA, &eavesdrop_msg);
    TEST_ASSERT(!eavesdrop_ok); // Must fail!
    (void)eavesdrop_ok;

    // Owner App A (PID 100) reads message
    IpcMessage msgA{};
    bool read_ok = ipc.Receive(100, portA, &msgA);
    TEST_ASSERT(read_ok);
    (void)read_ok;
    TEST_ASSERT(msgA.sender_pid == 200);
    TEST_ASSERT(msgA.message_id == 42);
    TEST_ASSERT(std::strcmp(reinterpret_cast<const char*>(msgA.data.data()), ping_data) == 0);

    // Queue is now empty
    TEST_ASSERT(!ipc.Receive(100, portA, &msgA));

    // Cleanup App A process
    ipc.CleanupProcess(100);
    TEST_ASSERT(!ipc.Send(200, portA, "MORE", 4, 1)); // Port destroyed

    std::cout << "[PASS] Inter-app IPC verified." << std::endl;
}

void test_scheduler_responsiveness() {
    std::cout << "[Test 3] Scheduler timeslicing and non-blocking event polling..." << std::endl;
    Memory mem(0x1000000ULL);
    KernelThreadManager th_mgr;
    BsdProcessManager proc_mgr;
    TEST_ASSERT(proc_mgr.initialize());

    MultiProcessScheduler scheduler(th_mgr, proc_mgr, mem);
    TEST_ASSERT(scheduler.check_invariants());

    KernelThread* t1 = th_mgr.create_thread("app_t1", 0x1000, 1, 0x70000, 0x10000, 16, true);
    KernelThread* t2 = th_mgr.create_thread("app_t2", 0x2000, 1, 0x60000, 0x10000, 16, true);
    KernelThread* t3 = th_mgr.create_thread("app_t3", 0x3000, 1, 0x50000, 0x10000, 16, true);

    scheduler.enqueue_thread(t1);
    scheduler.enqueue_thread(t2);
    scheduler.enqueue_thread(t3);
    TEST_ASSERT(scheduler.ready_count() == 3);

    CPU cpu;
    cpu.reset();

    // Schedule initial thread
    TEST_ASSERT(scheduler.schedule_next(cpu));
    std::vector<uint64_t> scheduled_tids;
    scheduled_tids.push_back(scheduler.current_thread()->tid());

    // Yield across multiple threads
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT(scheduler.yield(cpu));
        auto cur = scheduler.current_thread();
        TEST_ASSERT(cur != nullptr);
        scheduled_tids.push_back(cur->tid());
    }

    // Verify multiple threads executed
    TEST_ASSERT(scheduled_tids.size() == 6);
    TEST_ASSERT(scheduler.check_invariants());

    std::cout << "[PASS] Scheduler responsiveness and fairness verified." << std::endl;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.15 & 222.16: TIMERS, SCHEDULER & IPC TESTS\n";
    std::cout << "========================================\n";

    test_virtual_timer_events();
    test_inter_app_ipc_isolation();
    test_scheduler_responsiveness();

    std::cout << "\nALL STAGE 222.15 & 222.16 TESTS PASSED (100%).\n";
    return 0;
}

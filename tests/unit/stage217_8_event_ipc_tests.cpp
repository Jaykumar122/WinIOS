#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/guest_event_loop.h"
#include "runtime/guest_ipc.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::runtime;

void test_guest_event_loop_provenance() {
    GuestEventLoop loop(10, 8); // Capacity 8

    winios_event_t ev1{};
    ev1.type = WINIOS_EVENT_TOUCH_BEGIN;
    ev1.x = 100;
    ev1.y = 150;
    TEST_ASSERT(loop.PostEvent(ev1, EventSourceProvenance::kPhysicalWindows));

    winios_event_t ev2{};
    ev2.type = WINIOS_EVENT_TIMER;
    ev2.contact_id = 99;
    TEST_ASSERT(loop.PostEvent(ev2, EventSourceProvenance::kKernelTimer));

    TEST_ASSERT(loop.GetPendingCount() == 2);

    winios_event_t out_ev{};
    EventSourceProvenance prov{};
    TEST_ASSERT(loop.PollEvent(&out_ev, &prov));
    TEST_ASSERT(out_ev.type == WINIOS_EVENT_TOUCH_BEGIN);
    TEST_ASSERT(prov == EventSourceProvenance::kPhysicalWindows);

    TEST_ASSERT(loop.PollEvent(&out_ev, &prov));
    TEST_ASSERT(out_ev.type == WINIOS_EVENT_TIMER);
    TEST_ASSERT(prov == EventSourceProvenance::kKernelTimer);

    // Queue is empty now
    TEST_ASSERT(!loop.PollEvent(&out_ev));

    // Wait with timeout
    TEST_ASSERT(!loop.WaitEvent(&out_ev, 10));

    // Exit request
    loop.RequestExit(42);
    TEST_ASSERT(loop.IsExitRequested());
    TEST_ASSERT(loop.GetExitCode() == 42);

    std::cout << "[PASS] test_guest_event_loop_provenance\n";
}

void test_guest_ipc_bounded_security() {
    auto& ipc = GuestIpcRouter::Get();

    uint32_t port1 = ipc.CreatePort(10, "port_app10");
    uint32_t port2 = ipc.CreatePort(20, "port_app20");
    TEST_ASSERT(port1 != 0 && port2 != 0);

    const std::string msg_data = "Hello from PID 10 to PID 20";
    TEST_ASSERT(ipc.Send(10, port2, msg_data.data(), msg_data.size(), 1));

    // Unauthorized receive (PID 30 trying to read PID 20's port)
    IpcMessage received{};
    TEST_ASSERT(!ipc.Receive(30, port2, &received));

    // Authorized receive (PID 20 reading its port)
    TEST_ASSERT(ipc.Receive(20, port2, &received));
    TEST_ASSERT(received.sender_pid == 10);
    TEST_ASSERT(received.message_id == 1);
    TEST_ASSERT(std::string(reinterpret_cast<char*>(received.data.data()), received.data_size) == msg_data);

    // Empty port receive
    TEST_ASSERT(!ipc.Receive(20, port2, &received));

    // Unauthorized destruction
    TEST_ASSERT(!ipc.DestroyPort(port2, 30));

    // Process cleanup
    ipc.CleanupProcess(10);
    ipc.CleanupProcess(20);

    std::cout << "[PASS] test_guest_ipc_bounded_security\n";
}

int main() {
    test_guest_event_loop_provenance();
    test_guest_ipc_bounded_security();
    std::cout << "All Stage 217.8 & 217.11 event loop and IPC tests passed.\n";
    return 0;
}

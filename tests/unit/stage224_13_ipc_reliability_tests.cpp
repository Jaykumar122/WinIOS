#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/guest_ipc.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::runtime;

void test_generation_handle_and_stale_rejection() {
    std::cout << "[Test 1] 64-bit Generation Handles & Stale Handle Rejection (ABA Defense)...\n";

    auto& ipc = GuestIpcRouter::Get();
    ipc.Reset();

    // Create Port V2
    uint64_t handle1 = ipc.CreatePortV2(100, "service.port");
    TEST_ASSERT(handle1 != 0);

    // Send and receive on valid handle
    const char* ping = "PING";
    TEST_ASSERT(ipc.SendV2(101, handle1, ping, 4, 1));

    IpcMessage msg;
    TEST_ASSERT(ipc.ReceiveV2(100, handle1, &msg));
    TEST_ASSERT(msg.sender_pid == 101);
    TEST_ASSERT(msg.message_id == 1);
    TEST_ASSERT(msg.data_size == 4);

    // Destroy port
    TEST_ASSERT(ipc.DestroyPortV2(handle1, 100));

    // Stale handle operations must now fail!
    TEST_ASSERT(!ipc.SendV2(101, handle1, ping, 4, 1));
    TEST_ASSERT(!ipc.ReceiveV2(100, handle1, &msg));

    std::cout << "[PASS] Generation handle and stale rejection verified.\n";
}

void test_named_port_discovery() {
    std::cout << "[Test 2] Named Port Registry & Dynamic Discovery...\n";

    auto& ipc = GuestIpcRouter::Get();
    ipc.Reset();

    uint64_t handle = ipc.CreatePortV2(200, "com.winios.messenger");
    TEST_ASSERT(handle != 0);

    // Discover port by name
    uint64_t looked_up = ipc.LookupPortName("com.winios.messenger");
    TEST_ASSERT(looked_up == handle);

    // Non-existent name
    TEST_ASSERT(ipc.LookupPortName("com.winios.nonexistent") == 0);

    // Destroy port -> discovery returns 0
    ipc.DestroyPortV2(handle, 200);
    TEST_ASSERT(ipc.LookupPortName("com.winios.messenger") == 0);

    std::cout << "[PASS] Named port discovery verified.\n";
}

void test_queue_depth_and_message_size_limits() {
    std::cout << "[Test 3] Queue Depth (32 max) & Payload (4096 max) Bounds...\n";

    auto& ipc = GuestIpcRouter::Get();
    ipc.Reset();

    uint64_t handle = ipc.CreatePortV2(300);

    // 1. Oversized payload (4097 bytes) -> must fail!
    std::vector<uint8_t> big_data(4097, 0xAA);
    TEST_ASSERT(!ipc.SendV2(301, handle, big_data.data(), big_data.size()));

    // 2. Maximum payload (4096 bytes) -> must succeed
    std::vector<uint8_t> max_data(4096, 0xBB);
    TEST_ASSERT(ipc.SendV2(301, handle, max_data.data(), max_data.size()));

    // Clear queue
    IpcMessage msg;
    ipc.ReceiveV2(300, handle, &msg);

    // 3. Queue depth limits: send 32 messages
    for (int i = 0; i < 32; ++i) {
        TEST_ASSERT(ipc.SendV2(301, handle, "MSG", 3));
    }

    // 33rd message must fail due to queue full!
    TEST_ASSERT(!ipc.SendV2(301, handle, "OVERFLOW", 8));

    std::cout << "[PASS] Queue depth and message size limits verified.\n";
}

void test_process_crash_cleanup() {
    std::cout << "[Test 4] Process Crash & Termination IPC Eviction...\n";

    auto& ipc = GuestIpcRouter::Get();
    ipc.Reset();

    uint64_t h1 = ipc.CreatePortV2(400, "port.app400");
    uint64_t h2 = ipc.CreatePortV2(400);
    uint64_t other = ipc.CreatePortV2(500, "port.app500");

    TEST_ASSERT(h1 != 0);
    TEST_ASSERT(h2 != 0);
    TEST_ASSERT(ipc.GetPortCount() == 3);

    // Process 400 terminates
    ipc.CleanupProcess(400);

    // Ports owned by 400 must be wiped
    TEST_ASSERT(ipc.GetPortCount() == 1);
    TEST_ASSERT(ipc.LookupPortName("port.app400") == 0);
    TEST_ASSERT(ipc.LookupPortName("port.app500") == other); // Other process unharmed!

    std::cout << "[PASS] Process crash cleanup verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 224.13: IPC RELIABILITY TESTS\n";
    std::cout << "========================================\n";

    test_generation_handle_and_stale_rejection();
    test_named_port_discovery();
    test_queue_depth_and_message_size_limits();
    test_process_crash_cleanup();

    std::cout << "\nALL STAGE 224.13 TESTS PASSED (100%).\n";
    return 0;
}

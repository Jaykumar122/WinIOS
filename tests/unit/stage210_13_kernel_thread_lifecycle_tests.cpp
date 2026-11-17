#include <iostream>
#include <vector>
#include <cstdint>

#include "kernel/thread.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_thread_state_machine() {
    std::cout << "[Test 1] Stage 210.13: Thread State Transition Rules & Enforcements...\n";

    KernelThread t(100, "worker", 0x1000ULL, 0, 0x8000ULL, 0x2000ULL, 16);
    TEST_CHECK(t.state() == ThreadState::Ready, "Initial state is Ready");

    // Ready -> Running
    TEST_CHECK(t.transition_state(ThreadState::Running), "Ready -> Running valid");
    TEST_CHECK(t.state() == ThreadState::Running, "State is Running");

    // Running -> Blocked
    TEST_CHECK(t.transition_state(ThreadState::Blocked), "Running -> Blocked valid");
    TEST_CHECK(t.state() == ThreadState::Blocked, "State is Blocked");

    // Blocked -> Ready
    TEST_CHECK(t.transition_state(ThreadState::Ready), "Blocked -> Ready valid");
    TEST_CHECK(t.state() == ThreadState::Ready, "State is Ready");

    // Ready -> Running -> Terminated
    TEST_CHECK(t.transition_state(ThreadState::Running), "Ready -> Running valid");
    t.terminate(42);
    TEST_CHECK(t.state() == ThreadState::Terminated, "State is Terminated");
    TEST_CHECK(t.exit_code() == 42, "Exit code recorded as 42");

    // Invalid transitions from Terminated
    TEST_CHECK(!t.transition_state(ThreadState::Ready), "Terminated -> Ready rejected");
    TEST_CHECK(!t.transition_state(ThreadState::Running), "Terminated -> Running rejected");
    TEST_CHECK(!t.transition_state(ThreadState::Blocked), "Terminated -> Blocked rejected");
}

void test_thread_guest_sync() {
    std::cout << "[Test 2] Stage 210.13: TCB Guest Memory Synchronization...\n";

    Memory mem(0x100000);
    KernelThreadManager mgr;

    auto thread = mgr.create_thread("sync_thread", 0x2000ULL, 0x99ULL, 0x6000ULL, 0x2000ULL, 24);
    TEST_CHECK(thread != nullptr, "Thread created");

    const std::uint64_t tcb_guest_addr = 0x5000ULL;
    TEST_CHECK(thread->write_to_guest_memory(mem, tcb_guest_addr), "Flush TCB to guest memory");

    // Inspect guest memory
    const std::uint64_t magic = mem.read_u64(tcb_guest_addr);
    const std::uint64_t tid = mem.read_u64(tcb_guest_addr + 8);
    const std::uint32_t prio = mem.read_u32(tcb_guest_addr + 0x14);

    TEST_CHECK(magic == kTcbMagic, "Guest memory has kTcbMagic");
    TEST_CHECK(tid == thread->tid(), "Guest memory has correct TID");
    TEST_CHECK(prio == 24, "Guest memory has priority 24");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.13 Kernel Thread Lifecycle Tests ===\n";
    test_thread_state_machine();
    test_thread_guest_sync();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.13: All kernel thread lifecycle tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.13: " << g_failures << " tests failed.\n";
        return 1;
    }
}

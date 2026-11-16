// =============================================================================
// WinIOS — Stage 204.16: Thread Creation & Lifecycle Unit Tests
// =============================================================================

#include "runtime/darwin_thread_runtime.h"
#include "runtime/darwin_bsd_subsystem.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <memory>

using namespace winios;
using namespace winios::runtime;

static int g_failed_tests = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            g_failed_tests++; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Stack Allocation with Guard Pages (PROT_NONE)
// -----------------------------------------------------------------------------
void test_stack_allocation_and_guard_pages() {
    std::cout << "[Stage 204.16 Test 1] Stack Allocation & Guard Pages...\n";

    DarwinBsdSubsystem bsd;
    std::uint32_t pid = bsd.create_process(1, "/usr/bin/threadapp", 501);

    std::uint64_t stack_base = 0;
    std::uint64_t initial_sp = 0;
    constexpr std::size_t kStackSize = 0x80000; // 512 KB
    constexpr std::size_t kGuardSize = 0x4000;  // 16 KB

    bool ok = DarwinThreadRuntime::allocate_thread_stack(bsd, pid, kStackSize, kGuardSize, stack_base, initial_sp);
    TEST_CHECK(ok, "allocate_thread_stack succeeded");
    TEST_CHECK(stack_base > 0, "stack_base is non-zero");
    TEST_CHECK((stack_base & 0x3FFFULL) == 0, "stack_base is 16KB aligned");
    TEST_CHECK((initial_sp & 0xFULL) == 0, "initial_sp is 16-byte aligned");
    TEST_CHECK(initial_sp == stack_base + kGuardSize + kStackSize, "initial_sp points to top of usable stack");

    // Guard page [stack_base, stack_base + kGuardSize) must reject access (PROT_NONE)
    TEST_CHECK(!bsd.check_vm_access(pid, stack_base, kGuardSize, 0x1), "Guard page rejects PROT_READ");
    TEST_CHECK(!bsd.check_vm_access(pid, stack_base, kGuardSize, 0x2), "Guard page rejects PROT_WRITE");

    // Usable stack [stack_base + kGuardSize, initial_sp) must permit R/W access
    TEST_CHECK(bsd.check_vm_access(pid, stack_base + kGuardSize, kStackSize, 0x3), "Usable stack allows PROT_READ | PROT_WRITE");
}

// -----------------------------------------------------------------------------
// Test 2: Thread Creation & TPIDR_EL0 / TPIDRRO_EL0 Binding
// -----------------------------------------------------------------------------
void test_thread_creation_and_tpidr_binding() {
    std::cout << "[Stage 204.16 Test 2] Thread Creation & TPIDR Binding...\n";

    DarwinBsdSubsystem bsd;
    DarwinThreadRuntime runtime;
    std::uint32_t pid = bsd.create_process(1, "/usr/bin/threadapp", 501);
    Memory mem(0x200000); // 2 MB guest RAM
    CPU cpu;

    std::uint64_t entry = 0x10000;
    std::uint64_t arg = 0xCAFE;

    std::uint64_t tid = runtime.create_managed_thread(bsd, mem, pid, entry, arg, 0x80000, 0x4000);
    TEST_CHECK(tid > 0, "create_managed_thread succeeded with valid TID");

    auto t_opt = runtime.get_thread(tid);
    TEST_CHECK(t_opt.has_value(), "get_thread found created thread");
    if (!t_opt) return;

    const auto& t = *t_opt;
    TEST_CHECK(t.state == DarwinThreadState::Running, "Initial thread state is Running");
    TEST_CHECK(t.entry_point == entry, "entry_point matches");
    TEST_CHECK(t.arg == arg, "arg matches");
    TEST_CHECK(t.pthread_va > 0, "pthread_va allocated");
    TEST_CHECK(t.tls_base > t.pthread_va, "tls_base located after pthread header");

    // Verify pthread header contents in guest memory
    DarwinPthreadHeader hdr{};
    TEST_CHECK(DarwinThreadRuntime::read_pthread_header(mem, t.pthread_va, hdr), "read_pthread_header succeeded");
    TEST_CHECK(hdr.magic == kDarwinPthreadMagic, "magic is PTHRD_T");
    TEST_CHECK(hdr.thread_id == tid, "thread_id in header matches TID");
    TEST_CHECK(hdr.tsd_slots[kDarwinTsdThreadSelf] == t.pthread_va, "Slot 0 points to self");
    TEST_CHECK(hdr.tsd_slots[kDarwinTsdErrno] == t.pthread_va + offsetof(DarwinPthreadHeader, errno_val), "Slot 1 points to errno");

    // Bind thread context to CPU
    DarwinThreadRuntime::bind_cpu_thread(cpu, t.pthread_va, t.tls_base);
    TEST_CHECK(cpu.get_tpidrro_el0() == t.pthread_va, "TPIDRRO_EL0 holds pthread_t");
    TEST_CHECK(cpu.get_tpidr_el0() == t.tls_base, "TPIDR_EL0 holds TLS base");

    // Test setting errno via runtime
    TEST_CHECK(DarwinThreadRuntime::set_thread_errno(mem, t.pthread_va, 22), "set_thread_errno to 22 (EINVAL)");
    DarwinPthreadHeader updated_hdr{};
    TEST_CHECK(DarwinThreadRuntime::read_pthread_header(mem, t.pthread_va, updated_hdr), "read updated header");
    TEST_CHECK(updated_hdr.errno_val == 22, "errno_val is 22");
}

// -----------------------------------------------------------------------------
// Test 3: Thread State Transitions (Running, Suspended, Blocked, Terminated)
// -----------------------------------------------------------------------------
void test_thread_state_machine() {
    std::cout << "[Stage 204.16 Test 3] Thread State Machine...\n";

    DarwinBsdSubsystem bsd;
    DarwinThreadRuntime runtime;
    std::uint32_t pid = bsd.create_process(1, "/usr/bin/threadapp", 501);
    Memory mem(0x200000);

    std::uint64_t tid = runtime.create_managed_thread(bsd, mem, pid, 0x10000, 0);
    TEST_CHECK(tid > 0, "thread created");

    // 1. Suspend and Resume
    TEST_CHECK(runtime.suspend_thread(tid), "suspend_thread succeeded");
    TEST_CHECK(runtime.get_thread(tid)->state == DarwinThreadState::Suspended, "state is Suspended");
    TEST_CHECK(!runtime.suspend_thread(tid), "cannot suspend already suspended thread");

    TEST_CHECK(runtime.resume_thread(tid), "resume_thread succeeded");
    TEST_CHECK(runtime.get_thread(tid)->state == DarwinThreadState::Running, "state is Running");
    TEST_CHECK(!runtime.resume_thread(tid), "cannot resume already running thread");

    // 2. Block and Unblock
    TEST_CHECK(runtime.block_thread(tid), "block_thread succeeded");
    TEST_CHECK(runtime.get_thread(tid)->state == DarwinThreadState::Blocked, "state is Blocked");
    TEST_CHECK(!runtime.block_thread(tid), "cannot block already blocked thread");

    TEST_CHECK(runtime.unblock_thread(tid), "unblock_thread succeeded");
    TEST_CHECK(runtime.get_thread(tid)->state == DarwinThreadState::Running, "state is Running");

    // 3. Terminate
    TEST_CHECK(runtime.terminate_thread(tid, 42), "terminate_thread with exit code 42");
    TEST_CHECK(runtime.get_thread(tid)->state == DarwinThreadState::Terminated, "state is Terminated");
    TEST_CHECK(runtime.get_thread(tid)->exit_code == 42, "exit code recorded as 42");

    // 4. Terminated thread cannot be transitioned
    TEST_CHECK(!runtime.suspend_thread(tid), "cannot suspend terminated thread");
    TEST_CHECK(!runtime.resume_thread(tid), "cannot resume terminated thread");
    TEST_CHECK(!runtime.block_thread(tid), "cannot block terminated thread");
    TEST_CHECK(!runtime.terminate_thread(tid), "cannot terminate already terminated thread");
}

// -----------------------------------------------------------------------------
// Test 4: Mach-O Thread Local Variable (TLV) Descriptor Resolution
// -----------------------------------------------------------------------------
void test_tlv_resolution() {
    std::cout << "[Stage 204.16 Test 4] TLV Descriptor Resolution...\n";

    Memory mem(0x100000);
    CPU cpu;

    std::uint64_t pthread_va = 0x50000;
    std::uint64_t tls_base   = 0x60000;
    DarwinThreadRuntime::bind_cpu_thread(cpu, pthread_va, tls_base);

    // 1. Key 0 TLV Descriptor (Relative to TPIDR_EL0)
    std::uint64_t desc0_va = 0x1000;
    DarwinTlvDescriptorRaw desc0{};
    desc0.thunk = 0x20000;
    desc0.key = 0; // TPIDR_EL0
    desc0.offset = 0x128;
    std::memcpy(mem.raw_data() + desc0_va, &desc0, sizeof(desc0));

    std::uint64_t res0 = DarwinThreadRuntime::resolve_tlv_address(cpu, mem, desc0_va);
    TEST_CHECK(res0 == tls_base + 0x128, "Key 0 TLV resolved relative to TPIDR_EL0");

    // 2. Key 1 TLV Descriptor (Relative to TPIDRRO_EL0)
    std::uint64_t desc1_va = 0x1050;
    DarwinTlvDescriptorRaw desc1{};
    desc1.thunk = 0x20000;
    desc1.key = 1; // TPIDRRO_EL0
    desc1.offset = 0x30;
    std::memcpy(mem.raw_data() + desc1_va, &desc1, sizeof(desc1));

    std::uint64_t res1 = DarwinThreadRuntime::resolve_tlv_address(cpu, mem, desc1_va);
    TEST_CHECK(res1 == pthread_va + 0x30, "Key 1 TLV resolved relative to TPIDRRO_EL0");
}

// -----------------------------------------------------------------------------
// Test 5: Multi-thread Accounting & Process Isolation
// -----------------------------------------------------------------------------
void test_multi_thread_accounting() {
    std::cout << "[Stage 204.16 Test 5] Multi-thread Accounting & Process Isolation...\n";

    DarwinBsdSubsystem bsd;
    DarwinThreadRuntime runtime;
    Memory mem(0x800000);

    std::uint32_t pid1 = bsd.create_process(1, "/usr/bin/proc1", 501);
    std::uint32_t pid2 = bsd.create_process(1, "/usr/bin/proc2", 501);

    // Create 3 threads for pid1
    std::uint64_t t1 = runtime.create_managed_thread(bsd, mem, pid1, 0x10000, 1);
    std::uint64_t t2 = runtime.create_managed_thread(bsd, mem, pid1, 0x10000, 2);
    std::uint64_t t3 = runtime.create_managed_thread(bsd, mem, pid1, 0x10000, 3);
    TEST_CHECK(t1 > 0 && t2 > 0 && t3 > 0, "Threads for pid1 created");

    // Create 2 threads for pid2
    std::uint64_t t4 = runtime.create_managed_thread(bsd, mem, pid2, 0x20000, 4);
    std::uint64_t t5 = runtime.create_managed_thread(bsd, mem, pid2, 0x20000, 5);
    TEST_CHECK(t4 > 0 && t5 > 0, "Threads for pid2 created");

    TEST_CHECK(runtime.total_thread_count() == 5, "Total thread count is 5");
    TEST_CHECK(runtime.active_thread_count() == 5, "Active thread count is 5");

    auto p1_threads = runtime.get_threads_for_process(pid1);
    TEST_CHECK(p1_threads.size() == 3, "pid1 has 3 threads");
    auto p2_threads = runtime.get_threads_for_process(pid2);
    TEST_CHECK(p2_threads.size() == 2, "pid2 has 2 threads");

    // Terminate one thread in pid1
    TEST_CHECK(runtime.terminate_thread(t2, 0), "terminate t2");
    TEST_CHECK(runtime.total_thread_count() == 5, "Total thread count remains 5");
    TEST_CHECK(runtime.active_thread_count() == 4, "Active thread count reduced to 4");
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Running Stage 204.16: Thread Creation & Lifecycle Unit Tests\n";
    std::cout << "=================================================================\n";

    test_stack_allocation_and_guard_pages();
    test_thread_creation_and_tpidr_binding();
    test_thread_state_machine();
    test_tlv_resolution();
    test_multi_thread_accounting();

    if (g_failed_tests == 0) {
        std::cout << "=================================================================\n";
        std::cout << "Stage 204.16: ALL TESTS PASSED!\n";
        std::cout << "=================================================================\n";
        return 0;
    } else {
        std::cerr << "=================================================================\n";
        std::cerr << "Stage 204.16: " << g_failed_tests << " TESTS FAILED!\n";
        std::cerr << "=================================================================\n";
        return 1;
    }
}

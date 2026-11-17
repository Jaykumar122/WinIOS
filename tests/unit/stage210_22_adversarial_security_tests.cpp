#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "kernel/ipc.h"
#include "kernel/scheduler.h"
#include "kernel/sync.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "memory/kernel_heap_allocator.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_vm_map.h"
#include "memory/xnu_zone_allocator.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::runtime;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_16_adversarial_security_vectors() {
    std::cout << "[Test 1] Stage 210.22: 16 Adversarial Hostile Security Vectors...\n";

    // Vector 1: Empty kernel image to KernelBootCoordinator
    {
        CPU cpu;
        Memory mem(0x100000);
        KernelBootConfig cfg{};
        cfg.raw_kernel_image.clear();
        const auto res = KernelBootCoordinator::prepare_and_boot(cpu, mem, cfg);
        TEST_CHECK(!res.success, "Vector 1: Empty kernel image rejected");
    }

    // Vector 2: Out-of-bounds heap allocation
    {
        Memory mem(0x100000);
        KernelHeapAllocator heap(mem, 0x10000ULL, 0x10000ULL);
        heap.initialize();
        auto p = heap.kmalloc(0x20000ULL); // Larger than entire heap
        TEST_CHECK(p == 0, "Vector 2: Oversized heap allocation rejected");
    }

    // Vector 3: Heap double-free
    {
        Memory mem(0x100000);
        KernelHeapAllocator heap(mem, 0x10000ULL, 0x10000ULL);
        heap.initialize();
        auto p = heap.kmalloc(64);
        TEST_CHECK(heap.kfree(p), "Initial free succeeded");
        TEST_CHECK(!heap.kfree(p), "Vector 3: Heap double-free detected and rejected");
    }

    // Vector 4: Heap block header corruption detection
    {
        Memory mem(0x100000);
        KernelHeapAllocator heap(mem, 0x10000ULL, 0x10000ULL);
        heap.initialize();
        auto p = heap.kmalloc(64);
        // Corrupt header magic (offset -8 bytes)
        mem.write_u64(p - 8, 0xDEADBEEFCAFEBABEULL);
        std::string err;
        TEST_CHECK(!heap.validate_heap_integrity(&err), "Vector 4: Corrupted heap header detected by audit");
    }

    // Vector 5: Physical page allocator double-free
    {
        XnuPhysicalPageAllocator palloc;
        palloc.initialize(0x0ULL, 0x100000ULL);
        auto p = palloc.allocate_pages(1);
        std::string err;
        TEST_CHECK(palloc.free_pages(*p, 1, &err), "Initial page free succeeded");
        TEST_CHECK(!palloc.free_pages(*p, 1, &err), "Vector 5: Physical page double-free rejected");
    }

    // Vector 6: Out-of-range physical page free
    {
        XnuPhysicalPageAllocator palloc;
        palloc.initialize(0x0ULL, 0x100000ULL);
        std::string err;
        TEST_CHECK(!palloc.free_pages(0x500000ULL, 1, &err), "Vector 6: Out-of-range page free rejected");
    }

    // Vector 7: VM map address overflow (start + size < start)
    {
        XnuVmMap vm_map;
        std::string err;
        TEST_CHECK(!vm_map.vm_map_enter(0xFFFFFFFFFFFFFFFFULL, 0x1000ULL, kVmProtDefault, kVmProtAll, "", &err),
                   "Vector 7: Address arithmetic overflow rejected");
    }

    // Vector 8: VM map collision / overlap rejection
    {
        XnuVmMap vm_map(0x0ULL, 0x100000ULL);
        std::string err;
        vm_map.vm_map_enter(0x10000ULL, 0x10000ULL, kVmProtDefault, kVmProtAll, "base");
        TEST_CHECK(!vm_map.vm_map_enter(0x15000ULL, 0x10000ULL, kVmProtDefault, kVmProtAll, "overlap", &err),
                   "Vector 8: Range collision rejected");
    }

    // Vector 9: VM map permission elevation beyond max_protection
    {
        XnuVmMap vm_map(0x0ULL, 0x100000ULL);
        std::string err;
        vm_map.vm_map_enter(0x10000ULL, 0x10000ULL, kVmProtRead, kVmProtRead, "ro_segment");
        TEST_CHECK(!vm_map.vm_map_protect(0x10000ULL, 0x10000ULL, kVmProtRead | kVmProtWrite, false, &err),
                   "Vector 9: Elevation beyond max_protection rejected");
    }

    // Vector 10: Zone allocator double-free
    {
        XnuZoneAllocator za(0x10000ULL, 0x10000ULL);
        za.create_zone("hostile_zone", 32, 16);
        auto p = za.zalloc("hostile_zone");
        std::string err;
        TEST_CHECK(za.zfree("hostile_zone", p, &err), "Initial zone free succeeded");
        TEST_CHECK(!za.zfree("hostile_zone", p, &err), "Vector 10: Zone double-free rejected");
    }

    // Vector 11: Zone allocator misaligned pointer free
    {
        XnuZoneAllocator za(0x10000ULL, 0x10000ULL);
        za.create_zone("align_zone", 64, 16);
        auto p = za.zalloc("align_zone");
        std::string err;
        TEST_CHECK(!za.zfree("align_zone", p + 5, &err), "Vector 11: Misaligned zone pointer free rejected");
        za.zfree("align_zone", p);
    }

    // Vector 12: Zone allocator out-of-range pointer free
    {
        XnuZoneAllocator za(0x10000ULL, 0x10000ULL);
        za.create_zone("bounds_zone", 32, 16);
        std::string err;
        TEST_CHECK(!za.zfree("bounds_zone", 0x90000ULL, &err), "Vector 12: Out-of-bounds zone free rejected");
    }

    // Vector 13: Illegal thread transition Terminated -> Ready
    {
        KernelThread t(1, "hostile_t", 0x1000ULL, 0, 0x2000ULL, 0x1000ULL);
        t.transition_state(ThreadState::Running);
        t.transition_state(ThreadState::Terminated);
        TEST_CHECK(!t.transition_state(ThreadState::Ready), "Vector 13: Terminated -> Ready transition rejected");
    }

    // Vector 14: Illegal thread transition Terminated -> Running
    {
        KernelThread t(2, "hostile_t2", 0x1000ULL, 0, 0x2000ULL, 0x1000ULL);
        t.transition_state(ThreadState::Running);
        t.transition_state(ThreadState::Terminated);
        TEST_CHECK(!t.transition_state(ThreadState::Running), "Vector 14: Terminated -> Running transition rejected");
    }

    // Vector 15: Recursive spinlock deadlock detection
    {
        Memory mem(0x10000);
        KernelSpinlock lock(mem, 0x1000ULL);
        lock.initialize();
        bool deadlock = false;
        lock.try_acquire_host(42, &deadlock);
        TEST_CHECK(!lock.try_acquire_host(42, &deadlock), "Recursive acquire blocked");
        TEST_CHECK(deadlock, "Vector 15: Deadlock correctly diagnosed");
    }

    // Vector 16: IPC send to non-existent port
    {
        Memory mem(0x100000);
        KernelThreadManager mgr;
        PriorityScheduler sched(mgr, mem);
        sched.initialize();
        IpcManager ipc;
        KernelMessage msg{};
        TEST_CHECK(!ipc.send_from_guest(999, 1, 0x5000ULL, mem, sched),
                   "Vector 16: IPC to non-existent port rejected");
    }
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.22 Adversarial Security Tests ===\n";
    test_16_adversarial_security_vectors();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.22: All 16 adversarial security vectors handled cleanly.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.22: " << g_failures << " tests failed.\n";
        return 1;
    }
}

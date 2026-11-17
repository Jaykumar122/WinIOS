// ============================================================================
// Stage 212.21: Snapshot & Deterministic Replay for Multi-Process State Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bootstrap_registry.h"
#include "kernel/bsd_process.h"
#include "kernel/multi_process_scheduler.h"
#include "kernel/thread.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::kernel;
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

void test_bootstrap_service_registry_snapshot_and_restore() {
    std::cout << "[Test 1] BootstrapServiceRegistry Serialization & Deterministic Restore...\n";

    BootstrapServiceRegistry reg1;
    TEST_CHECK(reg1.check_in("com.apple.syslogd", 101, 1, true) == kBootstrapSuccess, "Check in syslogd");
    TEST_CHECK(reg1.check_in("com.apple.powerd", 102, 11, false) == kBootstrapSuccess, "Check in powerd");
    TEST_CHECK(reg1.check_in("com.apple.launchd", 100, 1, true) == kBootstrapSuccess, "Check in launchd");

    // Lookup to increment lookup counts
    std::uint32_t port = 0;
    TEST_CHECK(reg1.look_up("com.apple.syslogd", port, 20) == kBootstrapSuccess, "Lookup syslogd");
    TEST_CHECK(port == 101, "Port matches 101");

    // Terminate PID 11 to create dead name
    reg1.handle_process_termination(11);
    TEST_CHECK(!reg1.is_service_active("com.apple.powerd"), "Powerd is dead");

    // 1. Serialize
    std::vector<std::uint8_t> blob;
    reg1.serialize(blob);
    TEST_CHECK(!blob.empty(), "Registry serialized to non-empty blob");
    std::cout << "  -> Serialized blob size: " << blob.size() << " bytes\n";

    // 2. Deserialize into clean registry
    BootstrapServiceRegistry reg2;
    const std::uint8_t* ptr = blob.data();
    std::size_t remaining = blob.size();
    TEST_CHECK(reg2.deserialize(ptr, remaining), "Registry deserialized successfully");
    TEST_CHECK(remaining == 0, "All bytes consumed during deserialization");

    // 3. Verify restored state
    TEST_CHECK(reg2.service_count() == 3, "Restored registry has 3 services");
    TEST_CHECK(reg2.is_service_registered("com.apple.syslogd"), "syslogd is registered");
    TEST_CHECK(reg2.is_service_active("com.apple.syslogd"), "syslogd is active");
    TEST_CHECK(reg2.is_service_registered("com.apple.powerd"), "powerd is registered");
    TEST_CHECK(!reg2.is_service_active("com.apple.powerd"), "powerd is inactive (dead)");

    ServiceRecord rec{};
    TEST_CHECK(reg2.get_service_info("com.apple.syslogd", rec), "Retrieved syslogd record");
    TEST_CHECK(rec.port_id == 101, "Restored port ID matches");
    TEST_CHECK(rec.owner_pid == 1, "Restored owner PID matches");
    TEST_CHECK(rec.system_privileged == true, "Restored privileged flag matches");
    TEST_CHECK(rec.lookup_count == 1, "Restored lookup count matches");
}

void test_multi_process_scheduler_snapshot_and_restore() {
    std::cout << "[Test 2] MultiProcessScheduler Serialization & Deterministic Restore...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    auto* t1 = thread_mgr.create_thread("thread1", 0x1000, 1, 0x7fff0000, 0x10000, 16, true);
    auto* t2 = thread_mgr.create_thread("thread2", 0x2000, 2, 0x7fff0000, 0x10000, 16, true);
    auto* t3 = thread_mgr.create_thread("thread3", 0x3000, 3, 0x7fff0000, 0x10000, 16, true);

    MultiProcessScheduler sched1(thread_mgr, proc_mgr, mem);
    sched1.enqueue_thread(t1);
    sched1.enqueue_thread(t2);
    sched1.enqueue_thread(t3);

    CPU cpu;
    TEST_CHECK(sched1.schedule_next(cpu), "Dispatched t1 onto CPU");
    TEST_CHECK(sched1.current_thread() == t1, "Active thread is t1");

    sched1.tick(cpu);
    sched1.tick(cpu);

    // 1. Serialize
    std::vector<std::uint8_t> blob;
    sched1.serialize(blob);
    TEST_CHECK(!blob.empty(), "Scheduler state serialized to non-empty blob");
    std::cout << "  -> Serialized scheduler blob: " << blob.size() << " bytes\n";

    // 2. Restore into clean scheduler
    MultiProcessScheduler sched2(thread_mgr, proc_mgr, mem);
    const std::uint8_t* ptr = blob.data();
    std::size_t remaining = blob.size();
    TEST_CHECK(sched2.deserialize(ptr, remaining), "Scheduler deserialized successfully");
    TEST_CHECK(remaining == 0, "All scheduler bytes consumed");

    // 3. Verify identical state
    TEST_CHECK(sched2.current_thread() == t1, "Restored active thread is t1");
    TEST_CHECK(sched2.ready_count() == 2, "Restored ready queue count matches");
    TEST_CHECK(sched2.stats().total_context_switches == sched1.stats().total_context_switches,
               "Restored context switch count matches");
}

void test_full_system_state_serialization() {
    std::cout << "[Test 3] Full Multi-Process Combined State Serialization Roundtrip...\n";

    Memory mem(0x800000);
    KernelThreadManager thread_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    BootstrapServiceRegistry bootstrap;
    MultiProcessScheduler scheduler(thread_mgr, proc_mgr, mem);

    proc_mgr.create_process("daemon_proc", 1);
    proc_mgr.create_process("client_proc", 1);
    bootstrap.check_in("com.test.service", 55, 2, false);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    // Combine all components into unified snapshot blob
    std::vector<std::uint8_t> snapshot;
    proc_mgr.serialize(snapshot);
    bootstrap.serialize(snapshot);
    scheduler.serialize(snapshot);
    dispatcher.serialize(snapshot);

    TEST_CHECK(snapshot.size() > 50, "Full system snapshot produced (> 50 bytes)");
    std::cout << "  -> Full snapshot size: " << snapshot.size() << " bytes\n";

    // Verify deserialization roundtrip
    const std::uint8_t* ptr = snapshot.data();
    std::size_t remaining = snapshot.size();

    BsdProcessManager restored_proc;
    TEST_CHECK(restored_proc.deserialize_stream(ptr, remaining), "Restored proc mgr");

    BootstrapServiceRegistry restored_boot;
    TEST_CHECK(restored_boot.deserialize(ptr, remaining), "Restored bootstrap registry");

    MultiProcessScheduler restored_sched(thread_mgr, restored_proc, mem);
    TEST_CHECK(restored_sched.deserialize(ptr, remaining), "Restored scheduler");

    VirtualConsole restored_console;
    UserSyscallDispatcher restored_disp(restored_console);
    TEST_CHECK(restored_disp.deserialize(ptr, remaining), "Restored syscall dispatcher");

    TEST_CHECK(remaining == 0, "All snapshot bytes successfully consumed in deterministic order");
    TEST_CHECK(restored_proc.process_count() == 4, "Process count (PID 0,1,2,3) matches 4");
    TEST_CHECK(restored_boot.is_service_registered("com.test.service"), "Registered service intact");
    TEST_CHECK(restored_disp.abi_mode() == SyscallAbi::DarwinStrict, "Restored ABI mode matches");
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Stage 212.21: Snapshot & Deterministic Replay Tests\n";
    std::cout << "=================================================================\n";

    test_bootstrap_service_registry_snapshot_and_restore();
    test_multi_process_scheduler_snapshot_and_restore();
    test_full_system_state_serialization();

    if (g_failures == 0) {
        std::cout << "\n[PASS] All Stage 212.21 Snapshot Replay Tests PASSED cleanly.\n";
        return 0;
    } else {
        std::cerr << "\n[FAIL] " << g_failures << " test(s) FAILED in Stage 212.21.\n";
        return 1;
    }
}

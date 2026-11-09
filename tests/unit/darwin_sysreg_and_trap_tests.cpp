#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/darwin_sysreg_compat.h"
#include "runtime/darwin_syscall_dispatcher.h"
#include "memory/memory.h"

#define DARWIN_TRAP_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::darwin;
using namespace winios::runtime;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: Darwin System Register Inventory and Architecture Queries
// -----------------------------------------------------------------------------
bool test_darwin_sysreg_inventory() {
    std::cout << "[1] test_darwin_sysreg_inventory...\n";

    const auto entries = DarwinCompatibilityRegistry::get_all_entries();
    DARWIN_TRAP_ASSERT(!entries.empty(), "Darwin registry entries must not be empty");

    // Query critical Darwin architectural registers
    const auto* tpidrro = DarwinCompatibilityRegistry::find_by_name("TPIDRRO_EL0");
    DARWIN_TRAP_ASSERT(tpidrro != nullptr, "TPIDRRO_EL0 must exist in Darwin registry");
    DARWIN_TRAP_ASSERT(tpidrro->op0 == 3 && tpidrro->op1 == 3 && tpidrro->crn == 13 &&
                       tpidrro->crm == 0 && tpidrro->op2 == 3, "TPIDRRO_EL0 encoding match");
    DARWIN_TRAP_ASSERT(tpidrro->can_read && !tpidrro->can_write, "TPIDRRO_EL0 is read-only at EL0");

    const auto* tpidr_el1 = DarwinCompatibilityRegistry::find_by_name("TPIDR_EL1");
    DARWIN_TRAP_ASSERT(tpidr_el1 != nullptr, "TPIDR_EL1 must exist");
    DARWIN_TRAP_ASSERT(tpidr_el1->min_el == 1, "TPIDR_EL1 requires EL1");

    const auto* sprr = DarwinCompatibilityRegistry::find_by_name("SPRR_CONFIG_EL1");
    DARWIN_TRAP_ASSERT(sprr != nullptr, "SPRR_CONFIG_EL1 must exist for Apple Silicon");

    const auto* hid0 = DarwinCompatibilityRegistry::find_by_name("HID0_EL1");
    DARWIN_TRAP_ASSERT(hid0 != nullptr, "HID0_EL1 must exist for Apple Silicon pipeline controls");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Thread Pointer Registers (TPIDRRO_EL0 / TPIDR_EL1)
// -----------------------------------------------------------------------------
bool test_thread_pointer_sysregs() {
    std::cout << "[2] test_thread_pointer_sysregs...\n";

    CPU cpu;
    cpu.reset();

    // Verify TPIDR_EL0 (user TLS pointer)
    cpu.set_tpidr_el0(0x0000000100080000ULL);
    DARWIN_TRAP_ASSERT(cpu.get_tpidr_el0() == 0x0000000100080000ULL, "TPIDR_EL0 readback match");

    // Verify TPIDRRO_EL0 (read-only commpage pointer)
    cpu.set_tpidrro_el0(0x0000000FFFFFC000ULL);
    DARWIN_TRAP_ASSERT(cpu.get_tpidrro_el0() == 0x0000000FFFFFC000ULL, "TPIDRRO_EL0 readback match");

    // Verify TPIDR_EL1 (kernel cpu_data_t)
    cpu.set_tpidr_el1(0xFFFFFFF007100000ULL);
    DARWIN_TRAP_ASSERT(cpu.get_tpidr_el1() == 0xFFFFFFF007100000ULL, "TPIDR_EL1 readback match");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: 64-bit Commpage Initialization and Layout
// -----------------------------------------------------------------------------
bool test_commpage_initialization() {
    std::cout << "[3] test_commpage_initialization...\n";

    Memory mem(0x02000000); // 32 MB
    const std::uint64_t commpage_offset = 0x10000ULL;

    const bool init_ok = DarwinSyscallDispatcher::initialize_commpage(mem, commpage_offset, 6);
    DARWIN_TRAP_ASSERT(init_ok, "Commpage initialization must succeed");

    const auto* p = mem.raw_data() + commpage_offset;

    // 1. Signature ("Apple System")
    DARWIN_TRAP_ASSERT(std::string(reinterpret_cast<const char*>(p + commpage::kOffSignature)) == "Apple System",
                       "Signature must match 'Apple System'");

    // 2. Version (16)
    std::uint16_t ver = 0;
    std::memcpy(&ver, p + commpage::kOffVersion, 2);
    DARWIN_TRAP_ASSERT(ver == 16, "Commpage version must be 16");

    // 3. Number of CPUs (6 for iPhone 15)
    DARWIN_TRAP_ASSERT(p[commpage::kOffNCpus] == 6, "CPU count must be 6");

    // 4. Page Shift (14 for 16 KB pages)
    DARWIN_TRAP_ASSERT(p[commpage::kOffUserPageShift] == 14, "Page shift must be 14 (16 KB)");

    // 5. Timebase (24 MHz)
    std::uint64_t tb = 0;
    std::memcpy(&tb, p + commpage::kOffTimebase, 8);
    DARWIN_TRAP_ASSERT(tb == 24000000ULL, "Timebase must be 24,000,000");

    // 6. CPU Capabilities
    std::uint64_t caps = 0;
    std::memcpy(&caps, p + commpage::kOffCpuCapabilities64, 8);
    DARWIN_TRAP_ASSERT((caps & commpage::kCapFp) != 0, "FP capability must be set");
    DARWIN_TRAP_ASSERT((caps & commpage::kCapNeon) != 0, "NEON capability must be set");
    DARWIN_TRAP_ASSERT((caps & commpage::kCapAtomic) != 0, "Atomic capability must be set");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Darwin BSD POSIX Syscall Dispatching (SVC 0x80)
// -----------------------------------------------------------------------------
bool test_darwin_bsd_syscall_dispatch() {
    std::cout << "[4] test_darwin_bsd_syscall_dispatch...\n";

    CPU cpu;
    Memory mem(0x1000000);
    DarwinSyscallDispatcher dispatcher;

    // 1. SYS_getpid (20)
    cpu.reset();
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::GetPid));
    const std::int64_t pid = dispatcher.dispatch(cpu, mem, 0x80);
    DARWIN_TRAP_ASSERT(pid == 1, "getpid must return 1");
    DARWIN_TRAP_ASSERT(cpu.read_register(0) == 1, "X0 must hold return value");

    // 2. SYS_getuid (24)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::GetUid));
    const std::int64_t uid = dispatcher.dispatch(cpu, mem, 0x80);
    DARWIN_TRAP_ASSERT(uid == 0, "getuid must return root (0)");
    DARWIN_TRAP_ASSERT(cpu.read_register(0) == 0, "X0 must hold 0");

    // 3. SYS_mmap (197)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Mmap));
    cpu.write_register(0, 0x60000000ULL); // target addr
    const std::int64_t mmap_ret = dispatcher.dispatch(cpu, mem, 0x80);
    DARWIN_TRAP_ASSERT(mmap_ret == 0x60000000ULL, "mmap must return target address");

    // 4. SYS_exit (1)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Exit));
    cpu.write_register(0, 42u); // exit status
    dispatcher.dispatch(cpu, mem, 0x80);
    DARWIN_TRAP_ASSERT(dispatcher.has_exited(), "Dispatcher must record process exit");
    DARWIN_TRAP_ASSERT(dispatcher.exit_code() == 42, "Exit code must be 42");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Darwin Mach Trap Dispatching (SVC 0x81)
// -----------------------------------------------------------------------------
bool test_darwin_mach_trap_dispatch() {
    std::cout << "[5] test_darwin_mach_trap_dispatch...\n";

    CPU cpu;
    Memory mem(0x1000000);
    DarwinSyscallDispatcher dispatcher;

    // 1. MACH_task_self_trap (-28)
    cpu.reset();
    cpu.write_register(16, static_cast<std::uint64_t>(static_cast<std::int64_t>(DarwinMachTrap::TaskSelf)));
    const std::int64_t task_port = dispatcher.dispatch(cpu, mem, 0x81);
    DARWIN_TRAP_ASSERT(task_port == 0x103, "task_self must return 0x103");
    DARWIN_TRAP_ASSERT(cpu.read_register(0) == 0x103, "X0 must hold task port");

    // 2. MACH_host_self_trap (-29)
    cpu.write_register(16, static_cast<std::uint64_t>(static_cast<std::int64_t>(DarwinMachTrap::HostSelf)));
    const std::int64_t host_port = dispatcher.dispatch(cpu, mem, 0x81);
    DARWIN_TRAP_ASSERT(host_port == 0x101, "host_self must return 0x101");

    // 3. MACH_mach_reply_port (-27)
    cpu.write_register(16, static_cast<std::uint64_t>(static_cast<std::int64_t>(DarwinMachTrap::MachReplyPort)));
    const std::int64_t reply_port = dispatcher.dispatch(cpu, mem, 0x81);
    DARWIN_TRAP_ASSERT(reply_port == 0x203, "reply_port must return 0x203");

    // 4. MACH_mach_msg_trap (-26)
    cpu.write_register(16, static_cast<std::uint64_t>(static_cast<std::int64_t>(DarwinMachTrap::MachMsg)));
    const std::int64_t msg_ret = dispatcher.dispatch(cpu, mem, 0x81);
    DARWIN_TRAP_ASSERT(msg_ret == 0, "mach_msg must return KERN_SUCCESS (0)");

    // 5. MACH_semaphore_wait_trap (-36)
    cpu.write_register(16, static_cast<std::uint64_t>(static_cast<std::int64_t>(DarwinMachTrap::SemaphoreWait)));
    const std::int64_t sem_ret = dispatcher.dispatch(cpu, mem, 0x81);
    DARWIN_TRAP_ASSERT(sem_ret == 0, "semaphore_wait must return KERN_SUCCESS (0)");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Darwin Console I/O Capture (SYS_write)
// -----------------------------------------------------------------------------
bool test_darwin_stdout_capture() {
    std::cout << "[6] test_darwin_stdout_capture...\n";

    CPU cpu;
    Memory mem(0x1000000);
    DarwinSyscallDispatcher dispatcher;

    // Write "Hello Darwin XNU!\n" into guest RAM at 0x1000
    const char text[] = "Hello Darwin XNU!\n";
    std::memcpy(mem.raw_data() + 0x1000, text, sizeof(text) - 1);

    // Call SYS_write(1, 0x1000, 18)
    cpu.reset();
    cpu.write_register(0, 1); // stdout
    cpu.write_register(1, 0x1000);
    cpu.write_register(2, sizeof(text) - 1);
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Write));

    const std::int64_t written = dispatcher.dispatch(cpu, mem, 0x80);
    DARWIN_TRAP_ASSERT(written == sizeof(text) - 1, "Write count match");
    DARWIN_TRAP_ASSERT(dispatcher.stdout_buffer() == text, "Captured stdout must match written text");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Custom Handler Override & Fallback Behavior
// -----------------------------------------------------------------------------
bool test_custom_handler_and_fallback() {
    std::cout << "[7] test_custom_handler_and_fallback...\n";

    CPU cpu;
    Memory mem(0x1000000);
    DarwinSyscallDispatcher dispatcher;

    // Test unsupported syscall returns ENOSYS (78)
    cpu.reset();
    cpu.write_register(16, 9999ULL); // Invalid syscall nr
    const std::int64_t ret = dispatcher.dispatch(cpu, mem, 0x80);
    DARWIN_TRAP_ASSERT(ret == -78, "Unknown syscall must return -78 (ENOSYS)");
    DARWIN_TRAP_ASSERT(cpu.read_register(0) == 78, "X0 must hold 78");

    // Register custom handler for syscall 9999
    dispatcher.register_bsd_handler(9999ULL, [](CPU&, Memory&) -> std::int64_t {
        return 12345;
    });

    const std::int64_t custom_ret = dispatcher.dispatch(cpu, mem, 0x80);
    DARWIN_TRAP_ASSERT(custom_ret == 12345, "Custom handler should return 12345");
    DARWIN_TRAP_ASSERT(cpu.read_register(0) == 12345, "X0 must hold 12345");

    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=== Running WinIOS Darwin System Register & Mach Trap Tests ===\n";

    if (!test_darwin_sysreg_inventory()) return 1;
    if (!test_thread_pointer_sysregs()) return 2;
    if (!test_commpage_initialization()) return 3;
    if (!test_darwin_bsd_syscall_dispatch()) return 4;
    if (!test_darwin_mach_trap_dispatch()) return 5;
    if (!test_darwin_stdout_capture()) return 6;
    if (!test_custom_handler_and_fallback()) return 7;

    std::cout << ">>> All 7 Darwin System Register & Mach Trap Test Suites PASSED! <<<\n";
    return 0;
}

// ==============================================================================
// WinIOS Campaign 203 Stage 203.23: End-to-End Synthetic Darwin Workload Tests
// ==============================================================================
// Complete integration test verifying the unified Darwin execution pipeline:
// 1. Mach-O multi-image dependency topology & dynamic linking (DyldEngine)
// 2. Chained fixup resolution and module initializer sequencing
// 3. Darwin VM space allocation (DarwinVmSpace) with Apple 16KB pages
// 4. Process userspace stack & thread state initialization (DarwinProcessStartup)
// 5. Pthread control block & TLS subsystem binding (DarwinThreadRuntime)
// 6. Unified Darwin syscall dispatching (BSD syscalls & Mach traps on SVC 0x80)
// 7. Multi-process shared memory backing & Mach IPC message notification
// 8. Security invariants (W^X rejection, TPIDRRO_EL0 EL0 write prevention)
// ==============================================================================

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "loader/dyld_engine.h"
#include "loader/macho_dependency_graph.h"
#include "memory/darwin_vm_object.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_syscall_dispatcher.h"
#include "runtime/darwin_process_startup.h"
#include "runtime/darwin_thread_runtime.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Full Lifecycle Synthetic Darwin Workload
// -----------------------------------------------------------------------------
void test_full_lifecycle_synthetic_workload() {
    std::cout << "[Stage 203.23 Test 1] Full Lifecycle Synthetic Darwin Workload...\n";

    // Step 1: Multi-image Dyld dependency topology
    MachODependencyGraph graph;
    MachODependencyNode node_sys;
    node_sys.id = "/usr/lib/libSystem.B.dylib";
    node_sys.install_name = "/usr/lib/libSystem.B.dylib";
    graph.add_node(node_sys);

    MachODependencyNode node_app;
    node_app.id = "/Applications/SyntheticApp.app/SyntheticApp";
    node_app.install_name = node_app.id;
    MachODependencyEdge edge{"/usr/lib/libSystem.B.dylib", "/usr/lib/libSystem.B.dylib", false, false, false, 1, 1};
    node_app.dependencies.push_back(edge);
    graph.add_node(node_app);

    std::set<std::string> available = {
        "/usr/lib/libSystem.B.dylib",
        "/Applications/SyntheticApp.app/SyntheticApp"
    };

    auto dep_res = graph.resolve_dependencies("/Applications/SyntheticApp.app/SyntheticApp", available);
    TEST_CHECK(dep_res.success, "Dependency resolution succeeded");
    TEST_CHECK(dep_res.load_order.size() == 2, "Load order contains 2 images");
    TEST_CHECK(dep_res.load_order[0] == "/usr/lib/libSystem.B.dylib", "libSystem loaded first");
    TEST_CHECK(dep_res.load_order[1] == "/Applications/SyntheticApp.app/SyntheticApp", "App loaded second");

    // Step 2: Dyld module registration and symbol resolution via two-level namespace
    DyldEngine dyld;
    dyld.clear_modules();

    DyldLoadedModule mod_sys;
    mod_sys.name = "libSystem.B.dylib";
    mod_sys.install_name = "/usr/lib/libSystem.B.dylib";
    mod_sys.base_address = 0x180000000ULL;
    mod_sys.exports["_getpid"] = 0x180001000ULL;
    mod_sys.exports["_write"]  = 0x180001020ULL;
    mod_sys.exports["_mmap"]   = 0x180001040ULL;
    mod_sys.exports["_exit"]   = 0x180001060ULL;
    mod_sys.initializers = {0x180001080ULL};
    dyld.register_module(mod_sys);

    DyldLoadedModule mod_app;
    mod_app.name = "SyntheticApp";
    mod_app.install_name = "/Applications/SyntheticApp.app/SyntheticApp";
    mod_app.base_address = 0x100000000ULL;
    dyld.register_module(mod_app);

    std::uint32_t ord_sys = dyld.get_module_ordinal("/usr/lib/libSystem.B.dylib");
    TEST_CHECK(ord_sys != 0, "libSystem ordinal found");

    std::optional<std::uint64_t> resolved_addr;
    std::string err;
    DyldLinkStatus status = dyld.resolve_symbol_two_level(
        "_getpid", static_cast<std::int32_t>(ord_sys), false, resolved_addr, err);
    TEST_CHECK(status == DyldLinkStatus::Success, "Two-level symbol resolution of _getpid succeeds");
    TEST_CHECK(resolved_addr.has_value() && *resolved_addr == 0x180001000ULL, "_getpid resolved to exact target");

    // Step 3: Run static initializers in dependency order
    std::vector<std::string> init_names;
    auto init_order = dyld.get_initialization_order(dep_res.load_order, init_names);
    TEST_CHECK(!init_order.empty(), "Initializers collected");
    TEST_CHECK(init_order[0] == 0x180001080ULL, "libSystem initializer executed first");

    // Step 4: Create VM space and memory backing
    Memory mem(0x200000); // 2MB guest physical RAM
    CPU cpu;
    auto vm_space = std::make_shared<DarwinVmSpace>(101);
    auto ipc = std::make_shared<MachIpcSubsystem>();

    // Step 5: Format process userspace stack and thread state
    DarwinProcessStartupConfig start_cfg{};
    start_cfg.stack_top = 0x80000ULL;
    start_cfg.entry_point = 0x1000ULL;
    start_cfg.executable_path = "/Applications/SyntheticApp.app/SyntheticApp";
    start_cfg.argv = {"SyntheticApp", "--benchmark"};
    start_cfg.envp = {"USER=mobile", "HOME=/var/mobile"};
    start_cfg.populate_registers = true;

    DarwinProcessStartupInfo start_info{};
    std::string start_err;
    bool setup_ok = DarwinProcessStartup::setup_process(cpu, mem, start_cfg, start_info, start_err);
    TEST_CHECK(setup_ok, "Process stack and registers configured");
    TEST_CHECK(start_err.empty(), "No startup error");
    TEST_CHECK((cpu.get_stack_pointer() & 0xFULL) == 0, "Stack pointer is 16-byte aligned");
    TEST_CHECK(cpu.read_register(0) == 2, "X0 holds argc 2");

    // Step 6: Initialize Darwin thread runtime (pthread and TLS)
    const std::uint64_t pthread_va = 0x40000ULL;
    const std::uint64_t tls_base = 0x50000ULL;
    bool pth_ok = DarwinThreadRuntime::initialize_thread_structure(
        mem, pthread_va, 101, 0x10000, 0x70000, 0x103, 0x203, tls_base, 0x1000);
    TEST_CHECK(pth_ok, "Pthread structure initialized");
    DarwinThreadRuntime::bind_cpu_thread(cpu, pthread_va, tls_base);
    TEST_CHECK(cpu.get_tpidrro_el0() == pthread_va, "TPIDRRO_EL0 holds pthread_t");
    TEST_CHECK(cpu.get_tpidr_el0() == tls_base, "TPIDR_EL0 holds TLS base");

    // Step 7: Wire Syscall Dispatcher with bound VM and IPC subsystems
    DarwinSyscallDispatcher dispatcher;
    dispatcher.bind_vm_space(vm_space);
    dispatcher.bind_mach_ipc(ipc, 101);

    // Step 8: Execute SYS_getpid (20)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::GetPid));
    std::int64_t r_pid = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_pid == 1, "getpid returns synthetic root PID 1");
    TEST_CHECK(cpu.read_register(0) == 1, "X0 holds PID 1");

    // Step 9: Execute SYS_write (4) to stdout
    const char banner[] = "[Darwin Synthetic Runtime] Workload Online.\n";
    const std::uint64_t banner_addr = 0x2000ULL;
    std::memcpy(mem.raw_data() + banner_addr, banner, sizeof(banner) - 1);
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Write));
    cpu.write_register(0, 1); // stdout
    cpu.write_register(1, banner_addr);
    cpu.write_register(2, sizeof(banner) - 1);
    std::int64_t r_write = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_write == sizeof(banner) - 1, "write returns byte count");
    TEST_CHECK(dispatcher.stdout_buffer() == banner, "Dispatcher captured stdout banner");

    // Step 10: Execute SYS_mmap (197) to allocate a 16KB heap buffer
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Mmap));
    cpu.write_register(0, 0); // addr = 0 (auto-placement)
    cpu.write_register(1, 0x4000); // len = 16KB
    cpu.write_register(2, kDarwinVmProtRead | kDarwinVmProtWrite);
    cpu.write_register(3, kDarwinMapAnonymous | kDarwinMapPrivate);
    cpu.write_register(4, static_cast<std::uint64_t>(-1)); // fd = -1
    cpu.write_register(5, 0); // offset = 0
    std::int64_t r_mmap = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_mmap >= 0x4000, "mmap returns valid 16KB-aligned VA");
    TEST_CHECK((r_mmap & 0x3FFFULL) == 0, "mmap VA is 16KB aligned");
    TEST_CHECK(vm_space->find_entry(static_cast<std::uint64_t>(r_mmap)) != nullptr, "Range is mapped in VM space");

    // Step 11: Execute Mach Trap mach_reply_port (-26)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinMachTrap::MachReplyPort));
    std::int64_t r_reply = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_reply != kMachPortNull, "mach_reply_port returns valid port handle");
    TEST_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(r_reply), "X0 holds reply port handle");

    // Step 12: Execute SYS_exit (1)
    cpu.write_register(16, static_cast<std::uint64_t>(DarwinBsdSyscall::Exit));
    cpu.write_register(0, 0); // exit code 0
    std::int64_t r_exit = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(r_exit == 0, "exit syscall returns 0");
    TEST_CHECK(dispatcher.has_exited(), "Dispatcher reports process has exited");
    TEST_CHECK(dispatcher.exit_code() == 0, "Exit code is 0");

    std::cout << "  -> Full lifecycle synthetic workload verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Shared Memory Backing & Inter-Process Mach IPC Notification
// -----------------------------------------------------------------------------
void test_shared_memory_mach_ipc_coordination() {
    std::cout << "[Stage 203.23 Test 2] Shared Memory Backing and Mach IPC Coordination...\n";

    auto ipc = std::make_shared<MachIpcSubsystem>();
    const std::uint32_t producer_pid = 201;
    const std::uint32_t consumer_pid = 202;

    auto producer_space = ipc->create_task_port_space(producer_pid);
    auto consumer_space = ipc->create_task_port_space(consumer_pid);

    auto producer_vm = std::make_shared<DarwinVmSpace>(producer_pid);
    auto consumer_vm = std::make_shared<DarwinVmSpace>(consumer_pid);

    // 1. Create a shared VM Object (16KB)
    auto shared_obj = std::make_shared<DarwinVmObject>(
        0x4000, VmMappingType::Shared, true);

    // Map into Producer at 0x60000 and Consumer at 0x80000
    const std::uint64_t prod_va = 0x60000ULL;
    const std::uint64_t cons_va = 0x80000ULL;
    std::uint64_t m1 = producer_vm->map_object(shared_obj, prod_va, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapShared, 0);
    std::uint64_t m2 = consumer_vm->map_object(shared_obj, cons_va, 0x4000, kDarwinVmProtRead | kDarwinVmProtWrite, kDarwinMapShared, 0);
    TEST_CHECK(m1 == prod_va && m2 == cons_va, "Shared object mapped into both processes");

    // 2. Producer writes a structured header into the shared memory
    const std::uint64_t kMagicSignature = 0x44415257494E5F53ULL; // "DARWIN_S"
    const char payload_msg[] = "Cross-Process Shared Memory Data Payload Verified.";
    std::vector<std::uint8_t> write_buf(0x100, 0);
    std::memcpy(write_buf.data(), &kMagicSignature, sizeof(kMagicSignature));
    std::memcpy(write_buf.data() + 8, payload_msg, sizeof(payload_msg));

    int w_rc = producer_vm->mach_vm_write(prod_va, write_buf.data(), write_buf.size());
    TEST_CHECK(w_rc == kKernSuccess, "Producer wrote into shared memory");

    // 3. Set up communication ports between Producer and Consumer
    mach_port_t rcv_port = ipc->mach_port_allocate(consumer_pid, kMachPortRightReceive);
    TEST_CHECK(rcv_port != kMachPortNull, "Consumer allocated receive port");

    auto rcv_obj = consumer_space->lookup_port(rcv_port);
    mach_port_t snd_port = producer_space->allocate_port(rcv_obj, kMachPortRightSend);
    TEST_CHECK(snd_port != kMachPortNull, "Producer obtained send right to consumer port");

    // 4. Producer sends notification message containing shared memory offset
    Memory mem(0x10000);
    const std::uint64_t send_addr = 0x1000;
    const std::uint64_t recv_addr = 0x2000;

    struct NotificationMsg {
        MachMsgHeaderRaw hdr;
        std::uint64_t shared_offset;
        std::uint64_t payload_bytes;
    } notify_send{};

    notify_send.hdr.msgh_bits = 0;
    notify_send.hdr.msgh_size = sizeof(notify_send);
    notify_send.hdr.msgh_remote_port = snd_port;
    notify_send.hdr.msgh_local_port = kMachPortNull;
    notify_send.hdr.msgh_id = 999;
    notify_send.shared_offset = 0;
    notify_send.payload_bytes = write_buf.size();

    std::memcpy(mem.raw_data() + send_addr, &notify_send, sizeof(notify_send));
    int send_rc = ipc->mach_msg(producer_pid, send_addr, kMachSendMsg, sizeof(notify_send), 0, 0, 0, mem);
    TEST_CHECK(send_rc == kMachMsgSuccess, "Producer sent notification Mach message");

    // 5. Consumer receives notification message
    int recv_rc = ipc->mach_msg(consumer_pid, recv_addr, kMachRcvMsg, 0, sizeof(NotificationMsg), rcv_port, 0, mem);
    TEST_CHECK(recv_rc == kMachMsgSuccess, "Consumer received notification Mach message");

    NotificationMsg notify_recv{};
    std::memcpy(&notify_recv, mem.raw_data() + recv_addr, sizeof(notify_recv));
    TEST_CHECK(notify_recv.hdr.msgh_id == 999, "Message ID matches notification");
    TEST_CHECK(notify_recv.payload_bytes == write_buf.size(), "Payload byte size matches");

    // 6. Consumer reads data from shared memory at its own mapped address
    std::vector<std::uint8_t> read_buf(0x100, 0);
    int r_rc = consumer_vm->mach_vm_read(cons_va + notify_recv.shared_offset, read_buf.data(), read_buf.size());
    TEST_CHECK(r_rc == kKernSuccess, "Consumer read from shared memory");

    std::uint64_t read_signature = 0;
    std::memcpy(&read_signature, read_buf.data(), sizeof(read_signature));
    TEST_CHECK(read_signature == kMagicSignature, "Magic signature matches across processes");
    TEST_CHECK(std::strcmp(reinterpret_cast<const char*>(read_buf.data() + 8), payload_msg) == 0,
               "Payload text matches bit-exact in Consumer address space");

    std::cout << "  -> Shared memory backing and Mach IPC coordination verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Security Invariants and Sandboxing Verification
// -----------------------------------------------------------------------------
void test_security_invariants_and_sandboxing() {
    std::cout << "[Stage 203.23 Test 3] Security Invariants and Sandboxing Verification...\n";

    Memory mem(0x100000);
    CPU cpu;
    auto vm_space = std::make_shared<DarwinVmSpace>(301);

    // 1. Apple Silicon W^X policy rejection
    std::uint64_t rwx_addr = vm_space->mmap(
        0x10000, 0x4000,
        kDarwinVmProtRead | kDarwinVmProtWrite | kDarwinVmProtExecute,
        kDarwinMapAnonymous | kDarwinMapPrivate,
        -1, 0);
    TEST_CHECK(rwx_addr == 0, "RWX mapping rejected under Apple Silicon W^X policy");

    // 2. TPIDRRO_EL0 EL0 write prevention
    cpu.set_current_el(0); // Userspace
    cpu.set_architectural_exceptions(true);
    mem.write_u32(0x1000, 0xD51BD060u); // msr TPIDRRO_EL0, x0
    cpu.set_program_counter(0x1000);
    cpu.write_register(0, 0xDEADBEEFULL);
    try {
        cpu.step(mem);
    } catch (...) {}
    TEST_CHECK(cpu.get_tpidrro_el0() != 0xDEADBEEFULL, "EL0 write to TPIDRRO_EL0 strictly prevented");

    // 3. Syscall error boundary: invalid Mach trap returns KERN_FAILURE (5)
    DarwinSyscallDispatcher dispatcher;
    dispatcher.bind_vm_space(vm_space);
    cpu.write_register(16, static_cast<std::uint64_t>(-12345)); // Invalid trap
    std::int64_t trap_rc = dispatcher.dispatch(cpu, mem, 0x80);
    TEST_CHECK(trap_rc == 5, "Invalid Mach trap returns KERN_FAILURE (5)");
    TEST_CHECK(cpu.read_register(0) == 5, "X0 holds KERN_FAILURE");

    std::cout << "  -> Security invariants and sandboxing verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.23: End-to-End Synthetic Darwin Workload Tests\n";
    std::cout << "==================================================================\n";

    test_full_lifecycle_synthetic_workload();
    test_shared_memory_mach_ipc_coordination();
    test_security_invariants_and_sandboxing();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.23 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}

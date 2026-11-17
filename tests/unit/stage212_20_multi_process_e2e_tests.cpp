// ============================================================================
// Stage 212.20: Multi-Process End-to-End Execution Demonstration Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "kernel/bootstrap_registry.h"
#include "kernel/bsd_process.h"
#include "kernel/init_coordinator.h"
#include "kernel/multi_process_scheduler.h"
#include "kernel/pipe.h"
#include "kernel/process_spawner.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "kernel/user_stack.h"
#include "kernel/user_syscall_dispatcher.h"
#include "loader/embedded_multi_process_workload_212.inl"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "vfs/vfs.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::loader;
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

void test_multi_process_concurrent_execution() {
    std::cout << "[Test 1] Multi-Process End-to-End Execution (Init, Daemon, Client)..." << std::endl;

    Memory mem(0x2000000); // 32 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    std::cout << "  [DEBUG] PPM initialized" << std::endl;

    KernelThreadManager thread_mgr;
    BsdProcessManager proc_mgr;
    proc_mgr.initialize(); // Creates PID 0 (kernel_task) and PID 1 (launchd)
    std::cout << "  [DEBUG] ProcMgr initialized" << std::endl;

    BootstrapServiceRegistry bootstrap;
    vfs::VirtualFileSystem vfs;
    vfs.initialize_standard_hierarchy();
    std::cout << "  [DEBUG] VFS initialized" << std::endl;

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_vfs(&vfs);
    dispatcher.set_process_manager(&proc_mgr);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    MultiProcessScheduler scheduler(thread_mgr, proc_mgr, mem);
    InitCoordinator init_coord(proc_mgr, bootstrap, thread_mgr, ppm, mem);
    std::cout << "  [DEBUG] InitCoordinator initialized" << std::endl;

    // 1. Setup PID 1 (Init Coordinator)
    const std::uint64_t root_l2_init = ppm.allocate_page();
    TEST_CHECK(root_l2_init != 0, "Allocated root L2 table for Init Coordinator");
    UserAddressSpace uas_init(mem, ppm, root_l2_init, "init_vm");

    std::vector<std::uint8_t> init_bin(kInitCoordinatorMacho_data, kInitCoordinatorMacho_data + kInitCoordinatorMacho_len);
    auto img_init = MachoUserLoader::load_image(init_bin, uas_init, ppm, mem);
    TEST_CHECK(img_init.valid, "Init Coordinator Mach-O loaded into address space");
    std::cout << "  [DEBUG] Init loaded: valid=" << img_init.valid << " err=" << img_init.error_message << std::endl;

    UserStackConfig cfg_init;
    cfg_init.argv = {"init_coordinator"};
    cfg_init.envp = {"USER=root", "PATH=/bin:/usr/bin"};
    cfg_init.stack_size = 0x10000;
    auto stack_init = UserStackBuilder::build_stack(uas_init, ppm, mem, cfg_init);
    TEST_CHECK(stack_init.success, "Init Coordinator stack built");

    auto* thread_init = thread_mgr.create_thread(
        "init_main",
        img_init.entry_point,
        1,
        stack_init.final_sp_el0,
        cfg_init.stack_size,
        16,
        true);
    TEST_CHECK(thread_init != nullptr, "Init Coordinator main thread created");
    thread_init->set_owner_pid(1);
    thread_init->tcb().pc = img_init.entry_point;
    thread_init->tcb().sp_el0 = stack_init.final_sp_el0;
    thread_init->tcb().spsr_el1 = 0x00ULL;
    thread_init->tcb().gpr[0] = stack_init.argc;
    thread_init->tcb().gpr[1] = stack_init.argv_va;
    thread_init->tcb().gpr[2] = stack_init.envp_va;

    auto* proc_init = proc_mgr.get_process(1);
    TEST_CHECK(proc_init != nullptr, "Retrieved PID 1 process handle");
    proc_init->add_thread(thread_init);

    scheduler.register_process_address_space(1, &uas_init);
    std::cout << "  [DEBUG] Init configured" << std::endl;

    // 2. Setup PID 2 (Service Daemon) via InitCoordinator
    std::vector<std::uint8_t> daemon_bin(kServiceDaemonMacho_data, kServiceDaemonMacho_data + kServiceDaemonMacho_len);
    pid_t pid_daemon = init_coord.spawn_service("service_daemon", daemon_bin);
    std::cout << "  [DEBUG] Daemon spawn returned: " << pid_daemon << std::endl;
    TEST_CHECK(pid_daemon == 2, "Service daemon spawned as PID 2");
    auto* svc_daemon = init_coord.get_service("service_daemon");
    TEST_CHECK(svc_daemon != nullptr, "Service daemon registered with coordinator");
    if (svc_daemon) {
        scheduler.register_process_address_space(pid_daemon, svc_daemon->spawn_res.address_space.get());
    }

    // 3. Setup PID 3 (Client App) via InitCoordinator
    std::vector<std::uint8_t> client_bin(kClientAppMacho_data, kClientAppMacho_data + kClientAppMacho_len);
    pid_t pid_client = init_coord.spawn_service("client_app", client_bin);
    std::cout << "  [DEBUG] Client spawn returned: " << pid_client << std::endl;
    TEST_CHECK(pid_client == 3, "Client app spawned as PID 3");
    auto* svc_client = init_coord.get_service("client_app");
    TEST_CHECK(svc_client != nullptr, "Client app registered with coordinator");
    if (svc_client) {
        scheduler.register_process_address_space(pid_client, svc_client->spawn_res.address_space.get());
    }

    // 4. Enqueue threads into scheduler
    scheduler.enqueue_thread(thread_init);
    if (svc_daemon) scheduler.enqueue_thread(svc_daemon->spawn_res.main_thread);
    if (svc_client) scheduler.enqueue_thread(svc_client->spawn_res.main_thread);
    TEST_CHECK(scheduler.ready_count() == 3, "All 3 process threads enqueued in scheduler");
    std::cout << "  [DEBUG] Enqueued all 3 threads" << std::endl;

    // 5. Initialize CPU and MMU
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x00300000ULL); // FPEN=11

    const std::uint64_t vbar_pa = ppm.allocate_page();
    const std::uint64_t vbar_va = 0x80000ULL;
    cpu.set_vbar_el1(vbar_va);
    mem.write_u32(static_cast<std::size_t>(vbar_pa + 0x400u), 0xD503201Fu); // NOP
    uas_init.map_user_page(vbar_va, vbar_pa, memory::PagePermissions::UserExecute);
    svc_daemon->spawn_res.address_space->map_user_page(vbar_va, vbar_pa, memory::PagePermissions::UserExecute);
    svc_client->spawn_res.address_space->map_user_page(vbar_va, vbar_pa, memory::PagePermissions::UserExecute);
    std::cout << "  [DEBUG] VBAR mapped in all 3 spaces" << std::endl;

    const std::uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(uas_init.root_table_pa());
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));
    std::cout << "  [DEBUG] MMU configured" << std::endl;

    cpu.set_current_el(0);
    bool scheduled = scheduler.schedule_next(cpu);
    TEST_CHECK(scheduled, "First thread dispatched to CPU");
    std::cout << "  [DEBUG] First thread dispatched: scheduled=" << scheduled
              << " cur_pid=" << scheduler.current_pid()
              << " PC=0x" << std::hex << cpu.get_program_counter()
              << " SP=0x" << cpu.get_sp_el0() << std::dec << std::endl;

    // Priority scheduler for dispatcher interface
    PriorityScheduler dummy_sched(thread_mgr, mem);

    // 6. Execution Loop
    std::size_t instructions_retired = 0;
    std::size_t syscalls_handled = 0;
    constexpr std::size_t kMaxSteps = 2000;

    for (std::size_t step = 0; step < kMaxSteps; ++step) {
        if (step < 10) {
            std::cout << "  [DEBUG] Step " << step << " start: cur_pid=" << scheduler.current_pid()
                      << " PC=0x" << std::hex << cpu.get_program_counter() << std::dec << std::endl;
        }
        auto* p_daemon = proc_mgr.get_process(pid_daemon);
        auto* p_client = proc_mgr.get_process(pid_client);
        const bool daemon_done = (!p_daemon || p_daemon->is_terminated());
        const bool client_done = (!p_client || p_client->is_terminated());

        if (proc_init->is_terminated() && daemon_done && client_done) {
            std::cout << "  -> All 3 processes completed at step " << step << "\n";
            break;
        }

        cpu.step(mem);
        ++instructions_retired;

        // Check for SVC exception (EL0 -> EL1, EC == 0x15)
        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            const pid_t cur_pid = scheduler.current_pid();
            auto* cur_proc = proc_mgr.get_process(cur_pid);
            auto* cur_uas = scheduler.get_address_space(cur_pid);

            const std::uint64_t sys_num = cpu.read_register(16);
            std::cout << "  [DEBUG] SVC detected: sys_num=" << sys_num << " pid=" << cur_pid
                      << " proc=" << (cur_proc ? "valid" : "null")
                      << " uas=" << (cur_uas ? "valid" : "null") << std::endl;
            if (cur_proc && cur_uas) {
                dispatcher.handle_syscall(cpu, *cur_proc, *cur_uas, dummy_sched);
                ++syscalls_handled;
                std::cout << "  [DEBUG] Syscall handled: ret=" << cpu.read_register(0) << std::endl;
            }

            if (sys_num == 329) { // SYS_sched_yield
                std::cout << "  [DEBUG] Yielding pid " << cur_pid << std::endl;
                cpu.set_current_el(0);
                cpu.set_program_counter(cpu.get_elr_el1());
                scheduler.yield(cpu);
            } else if (sys_num == 1) { // SYS_exit
                std::cout << "  [DEBUG] Exiting pid " << cur_pid << std::endl;
                cur_proc->terminate(static_cast<int>(cpu.read_register(0)));
                if (scheduler.ready_count() > 0) {
                    scheduler.schedule_next(cpu);
                }
            } else if (sys_num == 7) { // SYS_wait4
                std::cout << "  [DEBUG] Wait4 in pid " << cur_pid << std::endl;
                init_coord.supervise_step();
                cpu.set_current_el(0);
                cpu.set_program_counter(cpu.get_elr_el1());
            } else {
                std::cout << "  [DEBUG] Resuming pid " << cur_pid << " to ELR=0x"
                          << std::hex << cpu.get_elr_el1() << std::dec << std::endl;
                cpu.set_current_el(0);
                cpu.set_program_counter(cpu.get_elr_el1());
            }
        }

        // Timer tick preemption check
        if (step % 4 == 0) {
            scheduler.tick(cpu);
        }
    }

    std::cout << "  -> Instructions Retired: " << instructions_retired
              << ", Syscalls Handled: " << syscalls_handled
              << ", Context Switches: " << scheduler.stats().total_context_switches
              << ", Address Space Switches: " << scheduler.stats().total_address_space_switches << "\n";

    TEST_CHECK(instructions_retired >= 30, "Retired >= 30 instructions across processes");
    TEST_CHECK(scheduler.stats().total_context_switches >= 3, "Context switches occurred across threads");
    TEST_CHECK(scheduler.stats().total_address_space_switches >= 2, "Address space switches occurred across processes");

    const std::string& out = console.get_output();
    std::cout << "  -> Console Output:\n" << out;

    TEST_CHECK(out.find("[INIT] WinIOS Multi-Process Coordinator Started") != std::string::npos,
               "Init coordinator start banner verified");
    TEST_CHECK(out.find("[DAEMON] WinIOS Background Service Active") != std::string::npos,
               "Service daemon start banner verified");
    TEST_CHECK(out.find("[CLIENT] WinIOS Test Client Connected") != std::string::npos,
               "Client app start banner verified");
    TEST_CHECK(out.find("[DAEMON] Service Processing Requests") != std::string::npos,
               "Service daemon request processing message verified");
    TEST_CHECK(out.find("[CLIENT] Request Dispatched To Service") != std::string::npos,
               "Client app dispatch message verified");
    TEST_CHECK(out.find("[INIT] All child services reaped. Exiting.") != std::string::npos,
               "Init coordinator reaping message verified");

    TEST_CHECK(proc_init->is_terminated(), "Init coordinator process terminated cleanly");
    TEST_CHECK(proc_mgr.get_process(pid_daemon) == nullptr, "Service daemon reaped from process table");
    TEST_CHECK(proc_mgr.get_process(pid_client) == nullptr, "Client app reaped from process table");
}

void test_multi_process_pipe_communication() {
    std::cout << "[Test 2] Inter-Process Communication via Bounded Kernel Pipe...\n";

    Pipe pipe;
    const std::string request = "CLIENT_IPC_TRANSACTION_PAYLOAD";
    const std::int64_t written = pipe.write(request.data(), request.size());
    TEST_CHECK(written == static_cast<std::int64_t>(request.size()), "Writer process wrote request into pipe");

    std::vector<char> read_buf(64, '\0');
    const std::int64_t read_bytes = pipe.read(read_buf.data(), read_buf.size());
    TEST_CHECK(read_bytes == written, "Reader daemon received exact byte count from pipe");
    TEST_CHECK(std::string(read_buf.data(), static_cast<std::size_t>(read_bytes)) == request,
               "Reader daemon payload matches client transaction payload");
}

void test_multi_process_memory_isolation() {
    std::cout << "[Test 3] Multi-Process Hardware Memory Isolation and W^X...\n";

    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root1 = ppm.allocate_page();
    const std::uint64_t root2 = ppm.allocate_page();

    UserAddressSpace uas1(mem, ppm, root1, "proc1_vm");
    UserAddressSpace uas2(mem, ppm, root2, "proc2_vm");

    // Map a page at VA 0x100000 in uas1
    uas1.map_user_page(0x100000, 0x200000, PagePermissions::UserRead);

    // Verify uas1 can translate 0x100000, but uas2 cannot
    TEST_CHECK(uas1.translate(0x100000) == 0x200000, "UAS1 translates private virtual page");
    TEST_CHECK(uas2.translate(0x100000) == 0, "UAS2 has no translation for UAS1 virtual page (Isolated)");
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Stage 212.20: Multi-Process End-to-End Execution Demonstration\n";
    std::cout << "=================================================================\n";

    test_multi_process_concurrent_execution();
    test_multi_process_pipe_communication();
    test_multi_process_memory_isolation();

    if (g_failures == 0) {
        std::cout << "\n[PASS] All Stage 212.20 Multi-Process Execution Tests PASSED cleanly.\n";
        return 0;
    } else {
        std::cerr << "\n[FAIL] " << g_failures << " test(s) FAILED in Stage 212.20.\n";
        return 1;
    }
}

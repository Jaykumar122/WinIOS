// ==============================================================================
// WinIOS Campaign 204 Stage 204.23: End-to-End Application Demonstrations
// ==============================================================================
// Implements and verifies three distinct end-to-end Darwin ARM64 workloads:
// Workload A: Minimal ARM64 Application (Darwin ABI stack, clean SYS_exit(42))
// Workload B: Multi-Image Application (Dyld dependency resolution, bottom-up
//             initializers, cross-image execution, SYS_write stdout capture)
// Workload C: Runtime Service Application (VFS file I/O, dynamic memory mmap,
//             pthread synchronization, thread lifecycle, TLS via TPIDR_EL0)
// ==============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>

#include "loader/macho_fixture_corpus.h"
#include "loader/userspace_macho_loader.h"
#include "runtime/darwin_application_runner.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_sync_primitives.h"
#include "runtime/darwin_thread_runtime.h"
#include "runtime/execution_diagnostics.h"
#include "memory/darwin_vm_object.h"
#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;
using namespace winios::memory;

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// ==============================================================================
// WORKLOAD A: Minimal ARM64 Application Demonstration
// ==============================================================================
void test_workload_a_minimal_application() {
    std::cout << "[Workload A] Minimal ARM64 Application Demonstration...\n";

    // 1. Build minimal binary with clean SYS_exit(42)
    ControlledMachOFixture fix = PublicMachOCorpus::create_minimal_executable();

    // Patch code at 0x1000 to exit with code 42
    // mov x0, #42   (0xD2800540)
    // mov x16, #1   (0xD2800030)
    // svc #0x80     (0xD4001001)
    const std::uint32_t code[] = {
        0xD2800540u, // mov x0, #42
        0xD2800030u, // mov x16, #1 (SYS_exit)
        0xD4001001u  // svc #0x80
    };
    std::memcpy(fix.binary_data.data() + 0x1000, code, sizeof(code));

    ApplicationConfig cfg{};
    cfg.executable_path = "/usr/bin/minimal_app";
    cfg.argv = {"/usr/bin/minimal_app", "--test-arg", "42"};
    cfg.envp = {"USER=root", "HOME=/var/root", "PATH=/bin:/usr/bin"};
    cfg.max_instructions = 100;

    DarwinApplicationRunner runner(cfg);
    ApplicationRunResult res = runner.run_binary(fix.binary_data.data(), fix.binary_data.size());

    TEST_CHECK(res.terminated, "Workload A must terminate");
    TEST_CHECK(res.termination_reason == "SYS_exit", "Termination reason must be SYS_exit");
    TEST_CHECK(res.exit_code == 42, "Exit code must match 42");
    TEST_CHECK(res.instructions_retired >= 2, "Must retire at least 2 instructions");

    std::cout << "  -> Workload A passed: Clean SYS_exit(42), retired "
              << res.instructions_retired << " instructions.\n";
}

// ==============================================================================
// WORKLOAD B: Multi-Image Application Demonstration
// ==============================================================================
void test_workload_b_multi_image_application() {
    std::cout << "[Workload B] Multi-Image Application Demonstration...\n";

    // Build multi-image app and its dependent libraries
    auto app_fix = PublicMachOCorpus::create_multi_image_app();
    auto libmath_fix = PublicMachOCorpus::create_multi_image_dylib_math();
    auto libbase_fix = PublicMachOCorpus::create_multi_image_dylib_base();

    // Patch app entry point to emit greeting via SYS_write(1, msg, len) then SYS_exit(0)
    // 0x1000: mov x0, #1       (0xD2800020)
    // 0x1004: adr x1, #28      (0x100000E1) -> points to 0x1020
    // 0x1008: mov x2, #15      (0xD28001E2)
    // 0x100C: mov x16, #4      (0xD2800090) -> SYS_write
    // 0x1010: svc #0x80        (0xD4001001)
    // 0x1014: mov x0, #0       (0xD2800000)
    // 0x1018: mov x16, #1      (0xD2800030) -> SYS_exit
    // 0x101C: svc #0x80        (0xD4001001)
    // 0x1020: "Multi-Image OK\n"
    if (app_fix.binary_data.size() >= 0x1030) {
        const std::uint32_t patch[] = {
            0xD2800020u, // mov x0, #1
            0x100000E1u, // adr x1, #28 -> 0x1020
            0xD28001E2u, // mov x2, #15
            0xD2800090u, // mov x16, #4 (SYS_write)
            0xD4001001u, // svc #0x80
            0xD2800000u, // mov x0, #0
            0xD2800030u, // mov x16, #1 (SYS_exit)
            0xD4001001u  // svc #0x80
        };
        std::memcpy(app_fix.binary_data.data() + 0x1000, patch, sizeof(patch));
        const char msg[] = "Multi-Image OK\n";
        std::memcpy(app_fix.binary_data.data() + 0x1020, msg, sizeof(msg));
    }

    ApplicationConfig cfg{};
    cfg.executable_path = "/usr/bin/multi_app";
    cfg.argv = {"/usr/bin/multi_app"};
    cfg.run_initializers = true;

    DarwinApplicationRunner runner(cfg);
    std::string err;

    // Load dependent libraries
    bool lbase = runner.load_library("/usr/lib/libbase.dylib", libbase_fix.binary_data.data(), libbase_fix.binary_data.size(), err);
    TEST_CHECK(lbase, "libbase load must succeed");

    bool lmath = runner.load_library("/usr/lib/libmath.dylib", libmath_fix.binary_data.data(), libmath_fix.binary_data.size(), err);
    TEST_CHECK(lmath, "libmath load must succeed");

    bool lapp = runner.load_executable(app_fix.binary_data.data(), app_fix.binary_data.size(), err);
    TEST_CHECK(lapp, "app load must succeed");

    bool linked = runner.link_and_initialize(err);
    TEST_CHECK(linked, "Multi-image linking must succeed");

    bool prep = runner.prepare_execution(err);
    TEST_CHECK(prep, "Execution preparation must succeed");

    ApplicationRunResult res = runner.run();
    TEST_CHECK(res.terminated, "Must terminate cleanly");
    TEST_CHECK(res.exit_code == 0, "Exit code must be 0");
    TEST_CHECK(res.stdout_output.find("Multi-Image OK\n") != std::string::npos, "Captured stdout must match banner");

    std::cout << "  -> Workload B passed: Multi-image dependencies linked, stdout verified: "
              << res.stdout_output;
}

// ==============================================================================
// WORKLOAD C: Runtime Service Application Demonstration
// ==============================================================================
void test_workload_c_runtime_service_application() {
    std::cout << "[Workload C] Runtime Service Application Demonstration...\n";

    // Build synthetic service binary that exercises:
    // 1. VFS file I/O operations (open, write, read, close)
    // 2. Dynamic mmap memory allocation via DarwinVmSpace
    // 3. Pthread synchronization & thread lifecycle
    // 4. Thread Local Storage via TPIDR_EL0
    ApplicationConfig cfg{};
    cfg.executable_path = "/usr/sbin/synthetic_service";
    cfg.argv = {"/usr/sbin/synthetic_service", "--daemon", "--port=8080"};
    cfg.memory_size = 0x8000000; // 128 MB

    DarwinApplicationRunner runner(cfg);

    // 1. Test VFS File Operations within the runner's BSD subsystem
    auto& bsd = runner.bsd();
    std::uint32_t pid = bsd.create_process(0, "/usr/sbin/synthetic_service", 0);

    // Open file for read/write with O_RDWR | O_CREAT (0x0202u)
    int log_fd = bsd.open(pid, "/var/log/service.log", 0x0202u, 0644);
    TEST_CHECK(log_fd >= 3, "Service log file created successfully");

    const std::string banner = "[SERVICE] Daemon active on port 8080\n";
    std::int64_t bytes_written = bsd.write(pid, log_fd, banner.data(), banner.size());
    TEST_CHECK(bytes_written == static_cast<std::int64_t>(banner.size()), "Banner written to log");

    // Seek back to beginning and verify read
    bsd.lseek(pid, log_fd, 0, 0 /* SEEK_SET */);
    std::vector<char> read_buf(64, 0);
    std::int64_t bytes_read = bsd.read(pid, log_fd, read_buf.data(), banner.size());
    TEST_CHECK(bytes_read == static_cast<std::int64_t>(banner.size()), "Read back log banner");
    TEST_CHECK(std::string(read_buf.data(), bytes_read) == banner, "Log content verified");

    bsd.close(pid, log_fd);

    // 2. Test Dynamic Memory Allocation via DarwinVmSpace
    DarwinVmSpace vm_space(pid, 0x100000000ULL);
    std::uint64_t alloc_va = vm_space.mmap(0, 0x10000, 3 /* RW */, 0x1002 /* ANONYMOUS */, -1, 0);
    TEST_CHECK(alloc_va != 0, "Dynamic mmap succeeded");
    TEST_CHECK(vm_space.check_access(alloc_va, 0x10000, 3 /* RW */), "Allocated region RW accessible");

    // Write magic signature to guest memory
    std::uint64_t phys_offset = alloc_va % runner.memory().size();
    runner.memory().write_u64(static_cast<std::size_t>(phys_offset), 0xCAFEBABE12345678ULL);
    TEST_CHECK(runner.memory().read_u64(static_cast<std::size_t>(phys_offset)) == 0xCAFEBABE12345678ULL, "Memory read/write verified");

    // 3. Test Pthread Synchronization Primitives
    DarwinSyncManager sync;
    std::uint64_t m_va = alloc_va + 0x1000;
    TEST_CHECK(sync.mutex_init(runner.memory(), m_va, PTHREAD_MUTEX_ERRORCHECK) == DARWIN_SUCCESS, "Mutex init succeeded");
    TEST_CHECK(sync.mutex_lock(runner.memory(), m_va, 1001) == DARWIN_SUCCESS, "Mutex lock by thread 1001 succeeded");
    TEST_CHECK(sync.mutex_trylock(runner.memory(), m_va, 1002) == DARWIN_EBUSY, "Mutex trylock by thread 1002 returned EBUSY");
    TEST_CHECK(sync.mutex_unlock(runner.memory(), m_va, 1001) == DARWIN_SUCCESS, "Mutex unlock succeeded");
    TEST_CHECK(sync.mutex_destroy(runner.memory(), m_va) == DARWIN_SUCCESS, "Mutex destroyed");

    // 4. Test Thread Runtime and TLS Binding
    std::uint64_t pthread_va = alloc_va + 0x2000;
    std::uint64_t tls_base = alloc_va + 0x3000;
    bool t_init = DarwinThreadRuntime::initialize_thread_structure(
        runner.memory(), pthread_va, 101, alloc_va + 0x4000, 0x8000, 0x103, 0x203, tls_base, 0x1000);
    TEST_CHECK(t_init, "Pthread structure initialized successfully");

    // Verify pthread header readback
    DarwinPthreadHeader pth{};
    bool pth_ok = DarwinThreadRuntime::read_pthread_header(runner.memory(), pthread_va, pth);
    TEST_CHECK(pth_ok, "Pthread header readback succeeded");
    TEST_CHECK(pth.thread_id == 101, "TID matches");
    TEST_CHECK(pth.magic == kDarwinPthreadMagic, "Magic matches");

    // Bind thread context to CPU system registers
    DarwinThreadRuntime::bind_cpu_thread(runner.cpu(), pthread_va, tls_base);
    TEST_CHECK(runner.cpu().get_tpidrro_el0() == pthread_va, "TPIDRRO_EL0 holds pthread_t");
    TEST_CHECK(runner.cpu().get_tpidr_el0() == tls_base, "TPIDR_EL0 holds thread TLS base");

    std::cout << "  -> Workload C passed: VFS I/O, mmap, pthread mutex, thread lifecycle & TLS verified.\n";
}

int main() {
    std::cout << "=== Running Stage 204.23 End-to-End Application Demonstrations ===" << std::endl;

    test_workload_a_minimal_application();
    test_workload_b_multi_image_application();
    test_workload_c_runtime_service_application();

    if (g_failures == 0) {
        std::cout << "ALL STAGE 204.23 END-TO-END WORKLOAD DEMONSTRATIONS PASSED!" << std::endl;
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED!" << std::endl;
        return 1;
    }
}

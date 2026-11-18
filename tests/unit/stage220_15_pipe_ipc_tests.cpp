// ============================================================================
// Stage 220.15 Unit Tests: Pipe IPC Backpressure, Ordering & Error Semantics
// ============================================================================

#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <memory>
#include <cstring>

#include "cpu/cpu.h"
#include "kernel/pipe.h"
#include "kernel/thread.h"
#include "kernel/bsd_process.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "memory/user_address_space.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

static void test_pipe_ring_buffer_fifo_and_wrap() {
    std::cout << "[Test 1] 16 KB circular pipe FIFO ordering and wrap-around...\n";
    Pipe pipe(16384); // 16 KB granule
    TEST_ASSERT(pipe.capacity() == 16384);
    TEST_ASSERT(pipe.space_available() == 16384);
    TEST_ASSERT(pipe.bytes_available() == 0);

    std::vector<uint8_t> send_data(10240); // 10 KB
    for (size_t i = 0; i < send_data.size(); ++i) {
        send_data[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
    }

    // Write 10 KB
    int64_t w1 = pipe.write(send_data.data(), send_data.size());
    TEST_ASSERT(w1 == 10240);
    TEST_ASSERT(pipe.bytes_available() == 10240);
    TEST_ASSERT(pipe.space_available() == 16384 - 10240);

    // Read 10 KB
    std::vector<uint8_t> recv_data(10240, 0);
    int64_t r1 = pipe.read(recv_data.data(), recv_data.size());
    TEST_ASSERT(r1 == 10240);
    TEST_ASSERT(send_data == recv_data);
    TEST_ASSERT(pipe.bytes_available() == 0);

    // Write 10 KB again: head wraps around past end of 16 KB buffer
    int64_t w2 = pipe.write(send_data.data(), send_data.size());
    TEST_ASSERT(w2 == 10240);
    TEST_ASSERT(pipe.bytes_available() == 10240);

    std::vector<uint8_t> recv_data2(10240, 0);
    int64_t r2 = pipe.read(recv_data2.data(), recv_data2.size());
    TEST_ASSERT(r2 == 10240);
    TEST_ASSERT(send_data == recv_data2);
    TEST_ASSERT(pipe.bytes_available() == 0);

    std::cout << "  Passed 16 KB ring buffer wrap-around FIFO tests.\n";
}

static void test_pipe_backpressure_and_limits() {
    std::cout << "[Test 2] Pipe backpressure, non-blocking EAGAIN, and capacity limits...\n";
    Pipe pipe(16384);

    std::vector<uint8_t> chunk(16384, 0xAA);
    int64_t w = pipe.write(chunk.data(), chunk.size());
    TEST_ASSERT(w == 16384);
    TEST_ASSERT(pipe.space_available() == 0);

    // When full, non-blocking write returns -35 (-EAGAIN)
    uint8_t extra = 0xFF;
    int64_t w_full_nb = pipe.write(&extra, 1, true);
    TEST_ASSERT(w_full_nb == -35);

    // When full, blocking write returns 0 (backpressure)
    int64_t w_full_b = pipe.write(&extra, 1, false);
    TEST_ASSERT(w_full_b == 0);

    // Drain half (8192 bytes)
    std::vector<uint8_t> drain(8192);
    int64_t r = pipe.read(drain.data(), drain.size());
    TEST_ASSERT(r == 8192);
    TEST_ASSERT(pipe.space_available() == 8192);

    // Now write 8192 bytes succeeds
    std::vector<uint8_t> fill2(8192, 0xBB);
    int64_t w2 = pipe.write(fill2.data(), fill2.size());
    TEST_ASSERT(w2 == 8192);
    TEST_ASSERT(pipe.space_available() == 0);

    std::cout << "  Passed backpressure and capacity limit tests.\n";
}

static void test_pipe_eof_and_broken_pipe_epipe() {
    std::cout << "[Test 3] End-of-file (EOF) and Broken Pipe (EPIPE) semantics...\n";
    Pipe pipe(4096);

    // 1. Read on empty open pipe:
    uint8_t dummy = 0;
    TEST_ASSERT(pipe.read(&dummy, 1, true) == -35); // -EAGAIN
    TEST_ASSERT(pipe.read(&dummy, 1, false) == 0);  // 0 bytes

    // 2. Write data then close write end
    const char msg[] = "Residual Payload";
    pipe.write(msg, sizeof(msg));
    pipe.close_write();
    TEST_ASSERT(pipe.is_write_closed());

    // Read remaining data: succeeds
    char buf[32] = {};
    int64_t r = pipe.read(buf, sizeof(buf));
    TEST_ASSERT(r == sizeof(msg));
    TEST_ASSERT(std::strcmp(buf, msg) == 0);

    // Next read on empty pipe with closed write end returns 0 (EOF)
    int64_t eof = pipe.read(buf, sizeof(buf), false);
    TEST_ASSERT(eof == 0);
    int64_t eof_nb = pipe.read(buf, sizeof(buf), true);
    TEST_ASSERT(eof_nb == 0);

    // 3. Close read end: write must return -32 (-EPIPE)
    Pipe pipe2(4096);
    pipe2.close_read();
    TEST_ASSERT(pipe2.is_read_closed());
    int64_t epipe = pipe2.write("data", 4);
    TEST_ASSERT(epipe == -32); // -EPIPE

    std::cout << "  Passed EOF and EPIPE semantics tests.\n";
}

static void test_pipe_syscall_integration() {
    std::cout << "[Test 4] UserSyscallDispatcher SYS_pipe, SYS_write, and SYS_read integration...\n";
    Memory mem(0x1000000ULL);
    CPU cpu;
    cpu.reset();

    PhysicalPageManager ppm(mem, 0x100000ULL, 0x800000ULL, 0x80000ULL);
    TEST_ASSERT(ppm.initialize());

    const uint64_t root_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_pa, "test_proc");
    uas.set_asid(1);
    uas.set_owner_pid(101);

    const uint64_t fds_va = 0x20000ULL;
    const uint64_t buf_va = 0x30000ULL;
    const uint64_t page_fds = ppm.allocate_page();
    const uint64_t page_buf = ppm.allocate_page();
    TEST_ASSERT(uas.map_user_page(fds_va, page_fds, PagePermissions::UserReadWrite));
    TEST_ASSERT(uas.map_user_page(buf_va, page_buf, PagePermissions::UserReadWrite));

    BsdProcess proc(101, 1, "test_proc");
    KernelThreadManager th_mgr;
    PriorityScheduler dummy_sched(th_mgr, mem);

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    // SYS_pipe: arg0 = fds_va
    cpu.write_register(16, kSysPipe);
    cpu.write_register(0, fds_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));

    int fds[2] = {};
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(fds_va), fds, sizeof(fds)));
    const int rfd = fds[0];
    const int wfd = fds[1];
    TEST_ASSERT(rfd >= 0 && wfd >= 0 && rfd != wfd);

    // Write "WinIOS Pipe Test" into wfd
    const char payload[] = "WinIOS Pipe Test";
    TEST_ASSERT(uas.copyin(buf_va, payload, sizeof(payload)));

    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, wfd);
    cpu.write_register(1, buf_va);
    cpu.write_register(2, sizeof(payload));
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == sizeof(payload));

    // Read back from rfd into buf_va + 0x100
    const uint64_t recv_va = buf_va + 0x100;
    cpu.write_register(16, kSysRead);
    cpu.write_register(0, rfd);
    cpu.write_register(1, recv_va);
    cpu.write_register(2, sizeof(payload));
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == sizeof(payload));

    char recv_str[32] = {};
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(recv_va), recv_str, sizeof(payload)));
    TEST_ASSERT(std::strcmp(recv_str, payload) == 0);

    // Close read end: write should return EPIPE (32)
    cpu.write_register(16, kSysClose);
    cpu.write_register(0, rfd);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));

    cpu.write_register(16, kSysWrite);
    cpu.write_register(0, wfd);
    cpu.write_register(1, buf_va);
    cpu.write_register(2, sizeof(payload));
    TEST_ASSERT(!dispatcher.handle_syscall(cpu, proc, uas, dummy_sched));
    TEST_ASSERT(cpu.read_register(0) == kErrnoEPIPE);

    std::cout << "  Passed pipe syscall integration tests.\n";
}

int main() {
    std::cout << "=== Stage 220.15 Pipe IPC Blocking & Backpressure Tests ===\n";
    test_pipe_ring_buffer_fifo_and_wrap();
    test_pipe_backpressure_and_limits();
    test_pipe_eof_and_broken_pipe_epipe();
    test_pipe_syscall_integration();
    std::cout << "All Stage 220.15 tests passed successfully.\n";
    return 0;
}

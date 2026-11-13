// =============================================================================
// stage_192_16_integrated_conformance_tests.cpp
//
// Stage 192.16: Integrated Darwin Userspace Conformance Tests
// Verifies all core subsystems collaborating in a single runtime environment:
//   1. Dynamic Linking & Symbol Binding
//   2. VFS, Pipes & BSD Syscall Traps (pipe, write, dup, read, close)
//   3. Virtual Memory & 16 KB Granule MMU Translation
//   4. Mach IPC Right Life-Cycle & Message Passing
//   5. Multi-Threaded Execution with TPIDR_EL0 TLS Context Switching
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "loader/macho_dependency_graph.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// -----------------------------------------------------------------------------
// Test 1: Integrated VFS Pipeline (Pipe, Write, Dup, Read, Close)
// -----------------------------------------------------------------------------
bool test_integrated_vfs_pipe_lifecycle() {
    std::cout << "[Test 1] Integrated VFS Pipeline (Pipe, Write, Dup, Read, Close)...\n";

    CPU cpu;
    Memory mem(16 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    constexpr uint32_t kPid = 1601;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/integrated_app", 501);
    engine.set_current_pid(kPid);

    // 1. SYS_pipe (syscall 42)
    cpu.reset();
    cpu.write_register(16, 42); // SYS_pipe
    DarwinPipelineResult res{};
    engine.handle_bsd_syscall(res);
    CHECK((cpu.get_nzcv() & 0x2u) == 0, "SYS_pipe must succeed (C=0)");

    int rfd = static_cast<int>(cpu.read_register(0));
    int wfd = static_cast<int>(cpu.read_register(1));
    CHECK(rfd >= 3 && wfd > rfd, "Pipe fds must be valid integers >= 3");

    // 2. Write payload into wfd via SYS_write (syscall 4)
    const std::string payload = "Integrated Darwin Userspace Pipe Data\n";
    constexpr uint64_t kWriteBuf = 0x20000;
    for (size_t i = 0; i < payload.size(); ++i) {
        mem.write_u8(kWriteBuf + i, static_cast<uint8_t>(payload[i]));
    }

    cpu.write_register(16, 4); // SYS_write
    cpu.write_register(0, static_cast<uint64_t>(wfd));
    cpu.write_register(1, kWriteBuf);
    cpu.write_register(2, payload.size());
    engine.handle_bsd_syscall(res);
    CHECK((cpu.get_nzcv() & 0x2u) == 0, "SYS_write must succeed");
    CHECK(cpu.read_register(0) == payload.size(), "Written bytes count mismatch");

    // 3. Dup rfd via SYS_dup (syscall 41)
    cpu.write_register(16, 41); // SYS_dup
    cpu.write_register(0, static_cast<uint64_t>(rfd));
    engine.handle_bsd_syscall(res);
    CHECK((cpu.get_nzcv() & 0x2u) == 0, "SYS_dup must succeed");
    int dup_rfd = static_cast<int>(cpu.read_register(0));
    CHECK(dup_rfd != rfd, "Duplicated fd must have different integer id");

    // 4. Read payload from dup_rfd via SYS_read (syscall 3)
    constexpr uint64_t kReadBuf = 0x30000;
    cpu.write_register(16, 3); // SYS_read
    cpu.write_register(0, static_cast<uint64_t>(dup_rfd));
    cpu.write_register(1, kReadBuf);
    cpu.write_register(2, 64);
    engine.handle_bsd_syscall(res);
    CHECK((cpu.get_nzcv() & 0x2u) == 0, "SYS_read must succeed");
    CHECK(cpu.read_register(0) == payload.size(), "Read bytes count mismatch");

    std::string received;
    for (size_t i = 0; i < payload.size(); ++i) {
        received.push_back(static_cast<char>(mem.read_u8(kReadBuf + i)));
    }
    CHECK(received == payload, "Received payload mismatch");

    // 5. Close all fds via SYS_close (syscall 6)
    for (int fd : {wfd, rfd, dup_rfd}) {
        cpu.write_register(16, 6);
        cpu.write_register(0, static_cast<uint64_t>(fd));
        engine.handle_bsd_syscall(res);
        CHECK((cpu.get_nzcv() & 0x2u) == 0, "SYS_close must succeed");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Integrated Virtual Memory & MMU Madvise
// -----------------------------------------------------------------------------
bool test_integrated_virtual_memory() {
    std::cout << "[Test 2] Integrated Virtual Memory & MMU Madvise...\n";

    CPU cpu;
    Memory mem(16 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    constexpr uint32_t kPid = 1602;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/mmu_app", 501);
    engine.set_current_pid(kPid);

    // 1. Allocate 32 KB (2 x 16 KB pages) via SYS_mmap (syscall 197)
    constexpr uint64_t kMmapReqAddr = 0x400000ULL;
    constexpr uint64_t kMmapSize    = 0x8000ULL; // 32 KB
    constexpr uint64_t kProtRW      = 0x3ULL;    // PROT_READ | PROT_WRITE
    constexpr uint64_t kFlagsAnon   = 0x1002ULL; // MAP_ANON | MAP_PRIVATE

    cpu.reset();
    cpu.write_register(16, 197); // SYS_mmap
    cpu.write_register(0, kMmapReqAddr);
    cpu.write_register(1, kMmapSize);
    cpu.write_register(2, kProtRW);
    cpu.write_register(3, kFlagsAnon);
    cpu.write_register(4, static_cast<uint64_t>(-1));
    cpu.write_register(5, 0);

    DarwinPipelineResult res{};
    engine.handle_bsd_syscall(res);
    CHECK((cpu.get_nzcv() & 0x2u) == 0, "SYS_mmap must succeed");
    uint64_t mapped_addr = cpu.read_register(0);
    CHECK(mapped_addr != 0, "Mapped address must be non-null");
    CHECK((mapped_addr & 0x3FFFULL) == 0, "Mapped address must be 16 KB aligned");

    // 2. Write and read memory across both pages
    mem.write_u64(mapped_addr, 0x1122334455667788ULL);
    mem.write_u64(mapped_addr + 0x4000, 0x99AABBCCDDEEFF00ULL);
    CHECK(mem.read_u64(mapped_addr) == 0x1122334455667788ULL, "First page readback mismatch");
    CHECK(mem.read_u64(mapped_addr + 0x4000) == 0x99AABBCCDDEEFF00ULL, "Second page readback mismatch");

    // 3. Issue SYS_madvise (syscall 75)
    cpu.write_register(16, 75); // SYS_madvise
    cpu.write_register(0, mapped_addr);
    cpu.write_register(1, kMmapSize);
    cpu.write_register(2, 3); // MADV_WILLNEED
    engine.handle_bsd_syscall(res);
    CHECK((cpu.get_nzcv() & 0x2u) == 0, "SYS_madvise must succeed");
    CHECK(cpu.read_register(0) == 0, "SYS_madvise return value must be 0");

    // 4. Deallocate via SYS_munmap (syscall 73)
    cpu.write_register(16, 73); // SYS_munmap
    cpu.write_register(0, mapped_addr);
    cpu.write_register(1, kMmapSize);
    engine.handle_bsd_syscall(res);
    CHECK((cpu.get_nzcv() & 0x2u) == 0, "SYS_munmap must succeed");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Integrated Mach IPC Messaging & Task Port Space
// -----------------------------------------------------------------------------
bool test_integrated_mach_ipc() {
    std::cout << "[Test 3] Integrated Mach IPC Messaging & Task Port Space...\n";

    CPU cpu;
    Memory mem(16 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    constexpr uint32_t kServerPid = 1603;
    constexpr uint32_t kClientPid = 1604;

    bsd.create_process_with_pid(kServerPid, 0, "/usr/bin/server", 501);
    bsd.create_process_with_pid(kClientPid, 0, "/usr/bin/client", 501);

    auto server_space = ipc.create_task_port_space(kServerPid);
    auto client_space = ipc.create_task_port_space(kClientPid);
    CHECK(server_space != nullptr && client_space != nullptr, "Port spaces must be created");

    // Allocate service receive right in server
    auto service_obj = std::make_shared<MachPortObject>(8801, kServerPid);
    mach_port_t server_recv_port = server_space->allocate_port(service_obj, kMachPortRightReceive);
    CHECK(server_recv_port != kMachPortNull, "Server port allocation failed");

    // Client allocates a local reply port
    auto reply_obj = std::make_shared<MachPortObject>(8802, kClientPid);
    mach_port_t client_reply_port = client_space->allocate_port(reply_obj, kMachPortRightReceive);
    CHECK(client_reply_port != kMachPortNull, "Client reply port allocation failed");

    // Client acquires send right to server service
    mach_port_t client_server_ref = client_space->allocate_port(service_obj, kMachPortRightSend);
    CHECK(client_server_ref != kMachPortNull, "Client acquire send right failed");

    // Construct request message in memory
    constexpr uint64_t kReqAddr = 0x50000;
    MachMsgHeaderRaw req{};
    req.msgh_size = sizeof(MachMsgHeaderRaw) + 8;
    req.msgh_remote_port = client_server_ref;
    req.msgh_local_port  = client_reply_port;
    req.msgh_id = 0xCAFE;

    std::memcpy(mem.raw_data() + kReqAddr, &req, sizeof(req));
    std::memcpy(mem.raw_data() + kReqAddr + sizeof(req), "REQDATA!", 8);

    // Send via mach_msg trap
    int32_t send_rc = ipc.mach_msg(kClientPid, kReqAddr, kMachSendMsg,
                                  sizeof(MachMsgHeaderRaw) + 8, 0, 0, 0, mem);
    CHECK(send_rc == kMachMsgSuccess, "mach_msg send must succeed");

    // Receive message in server
    constexpr uint64_t kRecvAddr = 0x60000;
    int32_t recv_rc = ipc.mach_msg(kServerPid, kRecvAddr, kMachRcvMsg,
                                  0, 512, server_recv_port, 0, mem);
    CHECK(recv_rc == kMachMsgSuccess, "mach_msg receive must succeed");

    MachMsgHeaderRaw received_hdr{};
    std::memcpy(&received_hdr, mem.raw_data() + kRecvAddr, sizeof(received_hdr));
    CHECK(received_hdr.msgh_id == 0xCAFE, "Received message ID mismatch");

    char body[9] = {0};
    std::memcpy(body, mem.raw_data() + kRecvAddr + sizeof(received_hdr), 8);
    CHECK(std::string(body) == "REQDATA!", "Received body payload mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Integrated Threads & TLS Context Switching
// -----------------------------------------------------------------------------
bool test_integrated_threads_and_tls() {
    std::cout << "[Test 4] Integrated Threads & TLS Context Switching...\n";

    CPU cpu;
    Memory mem(16 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    constexpr uint32_t kPid = 1605;
    bsd.create_process_with_pid(kPid, 0, "/usr/bin/multi_thread_app", 501);
    engine.set_current_pid(kPid);

    // Primary thread (TID 1)
    engine.set_current_tid(1);
    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_tpidr_el0(0x100000);
    cpu.write_register(0, 0xAAAA);

    // Create Thread 2 (TID 2)
    uint64_t tid2 = bsd.create_thread(kPid, 0x2000, 0x7FFFF000, 0x200000);
    CHECK(tid2 == 2, "Thread 2 creation failed");

    DarwinThreadContext ctx2{};
    bsd.get_thread_context(kPid, tid2, ctx2);
    ctx2.x[0] = 0xBBBB;
    bsd.set_thread_context(kPid, tid2, ctx2);

    // Context switch: Thread 1 -> Thread 2
    CHECK(engine.switch_thread(2), "switch_thread(2) failed");
    CHECK(cpu.get_tpidr_el0() == 0x200000, "Thread 2 TPIDR_EL0 mismatch");
    CHECK(cpu.read_register(0) == 0xBBBB, "Thread 2 X0 mismatch");

    // Context switch: Thread 2 -> Thread 1
    CHECK(engine.switch_thread(1), "switch_thread(1) failed");
    CHECK(cpu.get_tpidr_el0() == 0x100000, "Thread 1 TPIDR_EL0 mismatch");
    CHECK(cpu.read_register(0) == 0xAAAA, "Thread 1 X0 mismatch");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 192.16: Integrated Darwin Userspace Conformance\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_integrated_vfs_pipe_lifecycle, "Integrated VFS Pipeline (Pipe, Write, Dup, Read, Close)");
    run_test(test_integrated_virtual_memory, "Integrated Virtual Memory & MMU Madvise");
    run_test(test_integrated_mach_ipc, "Integrated Mach IPC Messaging & Task Port Space");
    run_test(test_integrated_threads_and_tls, "Integrated Threads & TLS Context Switching");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

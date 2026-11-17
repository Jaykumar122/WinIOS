#include <iostream>
#include <vector>
#include <cstdint>

#include "kernel/bsd_process.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_process_manager_snapshot_serialization() {
    std::cout << "[Test 1] Stage 211.23: BsdProcessManager Snapshot Serialization & Deserialization...\n";

    BsdProcessManager src_mgr;
    src_mgr.initialize();
    auto* p1 = src_mgr.create_process("init_proc", 0);
    p1->fd_table().install_fd(3, FileType::Regular, true, true, "/var/log/init.log");

    auto* p2 = src_mgr.create_process("worker_app", 1);
    p2->terminate(123);

    // Serialize
    std::vector<std::uint8_t> blob;
    src_mgr.serialize(blob);
    TEST_CHECK(!blob.empty(), "Serialized blob generated");

    // Deserialize
    BsdProcessManager dst_mgr;
    TEST_CHECK(dst_mgr.deserialize(blob.data(), blob.size()), "Deserialize into target manager");
    TEST_CHECK(dst_mgr.process_count() == 3, "Restored 3 processes (PID 0, 1, 2)");

    const auto* dp0 = dst_mgr.get_process(0);
    TEST_CHECK(dp0 != nullptr && dp0->name() == "kernel_task", "Restored PID 0");

    const auto* dp1 = dst_mgr.get_process(1);
    TEST_CHECK(dp1 != nullptr && dp1->name() == "init_proc", "Restored PID 1");
    TEST_CHECK(dp1->fd_table().is_valid_fd(3), "Restored custom FD 3 on PID 1");
    TEST_CHECK(dp1->fd_table().get_fd(3)->path == "/var/log/init.log", "Restored FD 3 path");

    const auto* dp2 = dst_mgr.get_process(2);
    TEST_CHECK(dp2 != nullptr && dp2->name() == "worker_app", "Restored PID 2");
    TEST_CHECK(dp2->is_terminated(), "Restored PID 2 is terminated");
    TEST_CHECK(dp2->exit_code() == 123, "Restored exit code 123");
}

void test_user_address_space_and_syscall_dispatcher_snapshot() {
    std::cout << "[Test 2] Stage 211.23: UserAddressSpace & Syscall Dispatcher State Restoration...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace src_uas(mem, ppm, root_l2_pa, "snap_uas");

    // Map a page
    const std::uint64_t data_pa = ppm.allocate_page();
    src_uas.map_user_page(0x20000ULL, data_pa, PagePermissions::UserReadWrite);

    // Serialize UAS
    std::vector<std::uint8_t> uas_blob;
    src_uas.serialize(uas_blob);
    TEST_CHECK(!uas_blob.empty(), "Serialized UserAddressSpace blob generated");

    // Deserialize UAS
    const std::uint8_t* ptr = uas_blob.data();
    std::size_t rem = uas_blob.size();
    auto dst_uas = UserAddressSpace::deserialize(mem, ppm, ptr, rem);
    TEST_CHECK(dst_uas != nullptr, "Deserialized UserAddressSpace successfully");
    TEST_CHECK(dst_uas->root_table_pa() == root_l2_pa, "Root table PA matches");
    TEST_CHECK(dst_uas->name() == "snap_uas", "UAS name matches");

    // Syscall dispatcher serialization
    VirtualConsole src_console;
    src_console.write_bytes("Replay Console Test\n", 20);
    UserSyscallDispatcher src_disp(src_console);

    std::vector<std::uint8_t> disp_blob;
    src_disp.serialize(disp_blob);
    TEST_CHECK(!disp_blob.empty(), "Serialized UserSyscallDispatcher blob generated");

    VirtualConsole dst_console;
    UserSyscallDispatcher dst_disp(dst_console);
    const std::uint8_t* d_ptr = disp_blob.data();
    std::size_t d_rem = disp_blob.size();
    TEST_CHECK(dst_disp.deserialize(d_ptr, d_rem), "Deserialized UserSyscallDispatcher");
    TEST_CHECK(dst_console.get_output() == "Replay Console Test\n", "Console output restored bit-exact");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.23 Snapshot Replay Tests ===\n";
    test_process_manager_snapshot_serialization();
    test_user_address_space_and_syscall_dispatcher_snapshot();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.23: All snapshot replay tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.23: " << g_failures << " tests failed.\n";
        return 1;
    }
}

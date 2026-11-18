#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>

#include "winios_sdk/winios_sdk.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "runtime/guest_allocator.h"
#include "runtime/guest_storage_api.h"
#include "runtime/guest_ipc.h"
#include "runtime/app_preferences.h"
#include "runtime/execution_boundary.h"
#include "storage/isolated_storage.h"
#include "vfs/vfs.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::runtime;
using namespace winios::storage;

void test_abi_v12_syscall_dispatch() {
    std::cout << "[Test 1] Stage 218.14: ABI v1.2 Syscall Dispatch, Trapping & Isolation (513..521)...\n";

    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "syscall_test_vm");

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("test_app", 0);
    TEST_ASSERT(proc != nullptr);

    // Map a data page for user buffers at 0x100000000
    uint64_t data_va = 0x100000000ULL;
    uint64_t data_pa = ppm.allocate_page();
    uas.map_user_page(data_va, data_pa, memory::PagePermissions::UserReadWrite);

    // Setup Allocator
    GuestAllocator allocator(0x200000000ULL, 1024 * 1024, 64 * 1024, proc->pid());
    dispatcher.set_allocator(&allocator);

    // Setup Storage
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    auto iso_storage = std::make_shared<IsolatedStorage>(vfs, "com.winios.testapp");
    GuestStorageApi storage_api(iso_storage, proc->pid());
    dispatcher.set_storage_api(&storage_api);

    // Setup Preferences
    auto prefs = std::make_shared<AppPreferences>(iso_storage, "com.winios.testapp");
    dispatcher.set_preferences(prefs.get());

    CPU cpu;
    cpu.reset();

    // 1. Test SYS_winios_alloc_heap (513)
    cpu.write_register(16, kSysWiniosAllocHeap);
    cpu.write_register(0, 512); // size
    cpu.write_register(1, 16);  // alignment
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint64_t heap_ptr = cpu.read_register(0);
    TEST_ASSERT(heap_ptr >= 0x200000000ULL);

    // 2. Test SYS_winios_get_runtime_info (515)
    uint64_t info_va = data_va;
    cpu.write_register(16, kSysWiniosGetRuntimeInfo);
    cpu.write_register(0, info_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0); // success

    winios_runtime_info_t info{};
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(info_va), &info, sizeof(info)));
    TEST_ASSERT(info.process_id == static_cast<uint32_t>(proc->pid()));
    TEST_ASSERT(info.heap_used_bytes >= 512);

    // 3. Test SYS_winios_free_heap (514)
    cpu.write_register(16, kSysWiniosFreeHeap);
    cpu.write_register(0, heap_ptr);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    // Double free must fail!
    cpu.write_register(16, kSysWiniosFreeHeap);
    cpu.write_register(0, heap_ptr);
    dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(cpu.read_register(0) == static_cast<uint64_t>(kErrnoEINVAL));

    // 4. Test SYS_winios_storage_write (517) & SYS_winios_storage_read (516)
    // Setup path string at data_va + 256
    uint64_t path_va = data_va + 256;
    const char* test_path = "notes.txt";
    uas.copyin(path_va, test_path, std::strlen(test_path) + 1);

    // Setup payload string at data_va + 512
    uint64_t payload_va = data_va + 512;
    const char* test_payload = "Genuine ARM64 Storage Content";
    size_t payload_len = std::strlen(test_payload);
    uas.copyin(payload_va, test_payload, payload_len);

    // Setup request struct at data_va + 128
    uint64_t req_va = data_va + 128;
    winios_storage_req_t req{};
    req.path_ptr = path_va;
    req.path_len = static_cast<uint32_t>(std::strlen(test_path));
    req.flags = 0;
    uas.copyin(req_va, &req, sizeof(req));

    // Write file
    cpu.write_register(16, kSysWiniosStorageWrite);
    cpu.write_register(0, req_va);
    cpu.write_register(1, payload_va);
    cpu.write_register(2, payload_len);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == payload_len);

    // Read file back into data_va + 768
    uint64_t read_va = data_va + 768;
    cpu.write_register(16, kSysWiniosStorageRead);
    cpu.write_register(0, req_va);
    cpu.write_register(1, read_va);
    cpu.write_register(2, 64);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == payload_len);

    char read_buf[64]{0};
    uas.copyout(reinterpret_cast<const void*>(read_va), read_buf, payload_len);
    TEST_ASSERT(std::memcmp(read_buf, test_payload, payload_len) == 0);

    // Path traversal attack: attempt writing to ../evil.txt
    const char* bad_path = "../evil.txt";
    uas.copyin(path_va, bad_path, std::strlen(bad_path) + 1);
    req.path_len = static_cast<uint32_t>(std::strlen(bad_path));
    uas.copyin(req_va, &req, sizeof(req));
    cpu.write_register(16, kSysWiniosStorageWrite);
    cpu.write_register(0, req_va);
    cpu.write_register(1, payload_va);
    cpu.write_register(2, payload_len);
    dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(cpu.read_register(0) == static_cast<uint64_t>(kErrnoEPERM));

    // 5. Test SYS_winios_ipc_create_port (518), send (519), recv (520)
    uint64_t port_name_va = data_va + 1024;
    uas.copyin(port_name_va, "test_ipc_port", 14);

    cpu.write_register(16, kSysWiniosIpcCreatePort);
    cpu.write_register(0, port_name_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint32_t my_port = static_cast<uint32_t>(cpu.read_register(0));
    TEST_ASSERT(my_port != 0);

    // Send IPC message to my_port
    uint64_t desc_va = data_va + 1100;
    winios_ipc_msg_desc_t msg_desc{};
    msg_desc.sender_pid = proc->pid();
    msg_desc.target_port = my_port;
    msg_desc.data_ptr = payload_va;
    msg_desc.data_size = static_cast<uint32_t>(payload_len);
    msg_desc.message_id = 999;
    uas.copyin(desc_va, &msg_desc, sizeof(msg_desc));

    cpu.write_register(16, kSysWiniosIpcSend);
    cpu.write_register(0, desc_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    // Receive IPC message
    uint64_t recv_desc_va = data_va + 1200;
    uint64_t recv_data_va = data_va + 1300;
    cpu.write_register(16, kSysWiniosIpcRecv);
    cpu.write_register(0, my_port);
    cpu.write_register(1, recv_desc_va);
    cpu.write_register(2, recv_data_va);
    cpu.write_register(3, 64);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    winios_ipc_msg_desc_t out_desc{};
    uas.copyout(reinterpret_cast<const void*>(recv_desc_va), &out_desc, sizeof(out_desc));
    TEST_ASSERT(out_desc.sender_pid == static_cast<uint32_t>(proc->pid()));
    TEST_ASSERT(out_desc.message_id == 999);
    TEST_ASSERT(out_desc.data_size == payload_len);

    char recv_buf[64]{0};
    uas.copyout(reinterpret_cast<const void*>(recv_data_va), recv_buf, payload_len);
    TEST_ASSERT(std::memcmp(recv_buf, test_payload, payload_len) == 0);

    // 6. Test SYS_winios_pref_get_set (521)
    uint64_t pref_key_va = data_va + 1400;
    uint64_t pref_val_va = data_va + 1500;
    uas.copyin(pref_key_va, "dark_mode", 10);
    int int_val = 1;
    uas.copyin(pref_val_va, &int_val, sizeof(int));

    uint64_t pref_va = data_va + 1600;
    winios_pref_entry_t pref_entry{};
    pref_entry.key_ptr = pref_key_va;
    pref_entry.key_len = 9;
    pref_entry.val_ptr = pref_val_va;
    pref_entry.val_size = sizeof(int);
    pref_entry.val_type = 2; // Int
    uas.copyin(pref_va, &pref_entry, sizeof(pref_entry));

    // Set preference (op = 1)
    cpu.write_register(16, kSysWiniosPrefGetSet);
    cpu.write_register(0, 1); // Set
    cpu.write_register(1, pref_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);
    TEST_ASSERT(prefs->GetInt("dark_mode") == 1);

    // Get preference (op = 0) into fresh buffer
    int fresh_val = 0;
    uas.copyin(pref_val_va, &fresh_val, sizeof(int));
    cpu.write_register(16, kSysWiniosPrefGetSet);
    cpu.write_register(0, 0); // Get
    cpu.write_register(1, pref_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    uas.copyout(reinterpret_cast<const void*>(pref_val_va), &fresh_val, sizeof(int));
    TEST_ASSERT(fresh_val == 1);

    std::cout << "[PASS] test_abi_v12_syscall_dispatch\n";
}

int main() {
    std::cout << "=== WinIOS Stage 218.14 Syscall Dispatch & Isolation Tests ===\n";
    test_abi_v12_syscall_dispatch();
    std::cout << "[ALL PASS] Stage 218.14 Syscall Dispatch & Isolation tests succeeded.\n";
    return 0;
}

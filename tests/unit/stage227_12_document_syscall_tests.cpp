#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "vfs/vfs.h"
#include "documents/document_registry.h"
#include "documents/document_stream_manager.h"
#include "documents/file_picker_service.h"
#include "documents/recent_document_manager.h"
#include "security/capability_manager.h"
#include "winios_sdk/winios_sdk.h"

#include <cassert>
#include <cstring>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::documents;

int main() {
    std::cout << "[Test Stage 227.12] Category C ABI v2.1 Document Syscalls (571-580)...\n" << std::flush;

    CPU cpu;
    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/Documents");

    auto cm = std::make_shared<security::CapabilityManager>();
    dispatcher.set_capability_manager(cm.get());

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("doc_app", 1);
    TEST_ASSERT(proc != nullptr);

    cm->bind_identity(proc->pid(), "com.winios.app");
    cm->grant_mask(proc->pid(), security::Capability::DocumentCreate | security::Capability::DocumentRead | security::Capability::DocumentWrite | security::Capability::DocumentShare);

    auto doc_reg = std::make_shared<DocumentRegistry>(cm);
    auto stream_mgr = std::make_shared<DocumentStreamManager>(doc_reg, vfs);
    auto picker_svc = std::make_shared<FilePickerService>(doc_reg, vfs);
    auto recent_mgr = std::make_shared<RecentDocumentManager>(vfs);

    dispatcher.set_document_registry(doc_reg.get());
    dispatcher.set_document_stream_manager(stream_mgr.get());
    dispatcher.set_file_picker_service(picker_svc.get());
    dispatcher.set_recent_document_manager(recent_mgr.get());

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "test_uas");
    proc->set_address_space(&uas);

    // Map user data page at 0x30000
    const std::uint64_t data_page_pa = ppm.allocate_page();
    uas.map_user_page(0x30000ULL, data_page_pa, PagePermissions::UserReadWrite);

    // Register a test document
    DocumentHandle doc_h = doc_reg->register_document(proc->pid(), "com.winios.app", "notes.txt", "/Documents/notes.txt", "text/plain", 128);
    TEST_ASSERT(doc_h != kInvalidDocumentHandle);

    // 1. Syscall 573: SYS_WINIOS_DOC_GET_METADATA
    cpu.write_register(16, kSysWiniosDocGetMetadata);
    cpu.write_register(0, doc_h);
    cpu.write_register(1, 0x30100ULL); // out_meta ptr
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    winios_doc_metadata_t meta_read{};
    uas.copyout(reinterpret_cast<const void*>(0x30100ULL), &meta_read, sizeof(meta_read));
    TEST_ASSERT(meta_read.doc_handle == doc_h);
    TEST_ASSERT(std::string(meta_read.display_name) == "notes.txt");
    TEST_ASSERT(std::string(meta_read.canonical_path) == "/Documents/notes.txt");

    // 2. Syscall 574: SYS_WINIOS_DOC_STREAM_OPEN
    cpu.write_register(16, kSysWiniosDocStreamOpen);
    cpu.write_register(0, doc_h);
    cpu.write_register(1, static_cast<uint64_t>(DocumentAccessMode::ReadWrite));
    cpu.write_register(2, 0x30300ULL); // out_stream_h ptr
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint64_t stream_h = 0;
    uas.copyout(reinterpret_cast<const void*>(0x30300ULL), &stream_h, sizeof(stream_h));
    TEST_ASSERT(stream_h != 0);

    // 3. Syscall 576: SYS_WINIOS_DOC_STREAM_WRITE
    const std::string text = "Hello WinIOS ABI v2.1\n";
    uas.copyin(0x30400ULL, text.data(), text.size());
    cpu.write_register(16, kSysWiniosDocStreamWrite);
    cpu.write_register(0, stream_h);
    cpu.write_register(1, 0x30400ULL); // src buffer
    cpu.write_register(2, text.size());
    cpu.write_register(3, 0x30500ULL); // out_written ptr
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint64_t written = 0;
    uas.copyout(reinterpret_cast<const void*>(0x30500ULL), &written, sizeof(written));
    TEST_ASSERT(written == text.size());

    // 4. Syscall 577: SYS_WINIOS_DOC_STREAM_SEEK
    cpu.write_register(16, kSysWiniosDocStreamSeek);
    cpu.write_register(0, stream_h);
    cpu.write_register(1, 0); // seek to offset 0
    cpu.write_register(2, 0x30550ULL); // out_cursor ptr
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint64_t cursor = 999;
    uas.copyout(reinterpret_cast<const void*>(0x30550ULL), &cursor, sizeof(cursor));
    TEST_ASSERT(cursor == 0);

    // 5. Syscall 575: SYS_WINIOS_DOC_STREAM_READ
    cpu.write_register(16, kSysWiniosDocStreamRead);
    cpu.write_register(0, stream_h);
    cpu.write_register(1, 0x30600ULL); // dst buffer
    cpu.write_register(2, text.size());
    cpu.write_register(3, 0x30700ULL); // out_read ptr
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint64_t bytes_read = 0;
    uas.copyout(reinterpret_cast<const void*>(0x30700ULL), &bytes_read, sizeof(bytes_read));
    TEST_ASSERT(bytes_read == text.size());
    std::vector<char> read_back(bytes_read + 1, 0);
    uas.copyout(reinterpret_cast<const void*>(0x30600ULL), read_back.data(), bytes_read);
    TEST_ASSERT(std::string(read_back.data()) == text);

    // 6. Syscall 578: SYS_WINIOS_DOC_STREAM_CLOSE
    cpu.write_register(16, kSysWiniosDocStreamClose);
    cpu.write_register(0, stream_h);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));

    // 7. Syscall 571: SYS_WINIOS_DOC_OPEN_PICKER
    const std::string ext = ".txt";
    uas.copyin(0x30800ULL, ext.c_str(), ext.size() + 1);
    cpu.write_register(16, kSysWiniosDocOpenPicker);
    cpu.write_register(0, 0x100000001ULL); // caller window
    cpu.write_register(1, 0x30800ULL);      // filter ext
    cpu.write_register(2, 0x30850ULL);      // out_req_id
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint32_t req_id = 0;
    uas.copyout(reinterpret_cast<const void*>(0x30850ULL), &req_id, sizeof(req_id));
    TEST_ASSERT(req_id > 0);

    // 8. Syscall 579: SYS_WINIOS_DOC_GRANT_ACCESS
    cpu.write_register(16, kSysWiniosDocGrantAccess);
    cpu.write_register(0, doc_h);
    cpu.write_register(1, 202); // grantee PID
    cpu.write_register(2, static_cast<uint64_t>(DocumentAccessMode::Read));
    cpu.write_register(3, 0x30900ULL); // out_token ptr
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint64_t grant_tok = 0;
    uas.copyout(reinterpret_cast<const void*>(0x30900ULL), &grant_tok, sizeof(grant_tok));
    TEST_ASSERT(grant_tok > 0);

    // 9. Syscall 580: SYS_WINIOS_DOC_GET_RECENT
    recent_mgr->add_recent("com.winios.app", doc_h, "notes.txt", "/Documents/notes.txt");
    cpu.write_register(16, kSysWiniosDocGetRecent);
    cpu.write_register(0, 0x30A00ULL); // dst array ptr
    cpu.write_register(1, 10);         // max count
    cpu.write_register(2, 0x30B00ULL); // out_count ptr
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint32_t rec_count = 0;
    uas.copyout(reinterpret_cast<const void*>(0x30B00ULL), &rec_count, sizeof(rec_count));
    TEST_ASSERT(rec_count == 1);
    winios_recent_doc_t rec_item{};
    uas.copyout(reinterpret_cast<const void*>(0x30A00ULL), &rec_item, sizeof(rec_item));
    TEST_ASSERT(std::string(rec_item.display_name) == "notes.txt");
    TEST_ASSERT(std::string(rec_item.canonical_path) == "/Documents/notes.txt");

    std::cout << "stage227_12_document_syscall_tests: ALL PASSED" << std::endl;
    return 0;
}

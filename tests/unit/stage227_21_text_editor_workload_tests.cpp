#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign227_workloads.inl"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "memory/user_address_space.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "runtime/guest_app_contract.h"
#include "runtime/guest_storage_api.h"
#include "runtime/system_clipboard.h"
#include "security/capability_manager.h"
#include "vfs/vfs.h"
#include "runtime/execution_boundary.h"
#include "documents/document_registry.h"
#include "documents/document_stream_manager.h"
#include "documents/file_picker_service.h"
#include "documents/recent_document_manager.h"
#include "documents/desktop_session_manager.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::runtime;
using namespace winios::graphics;
using namespace winios::security;
using namespace winios::vfs;
using namespace winios::documents;

static void setup_cpu(CPU& cpu, uint64_t root_l2_pa) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(3ULL << 20u);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL | (1ULL << 14u));
    cpu.set_current_el(0);
}

int main() {
    std::cout << "[Test Stage 227.21] Native ARM64 Text Editor Guest Workload...\n" << std::flush;
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    WindowManager wm;
    dispatcher.set_window_manager(&wm);

    SurfaceResourceManager srm;
    dispatcher.set_surface_resource_manager(&srm);

    SystemClipboard clipboard;
    dispatcher.set_system_clipboard(&clipboard);

    auto vfs = std::make_shared<VirtualFileSystem>();
    dispatcher.set_vfs(vfs.get());

    auto cm = std::make_shared<CapabilityManager>();
    dispatcher.set_capability_manager(cm.get());

    auto doc_reg = std::make_shared<DocumentRegistry>(cm);
    auto stream_mgr = std::make_shared<DocumentStreamManager>(doc_reg, vfs);
    auto picker_svc = std::make_shared<FilePickerService>(doc_reg, vfs);
    auto recent_mgr = std::make_shared<RecentDocumentManager>(vfs);

    dispatcher.set_document_registry(doc_reg.get());
    dispatcher.set_document_stream_manager(stream_mgr.get());
    dispatcher.set_file_picker_service(picker_svc.get());
    dispatcher.set_recent_document_manager(recent_mgr.get());

    // Seed target document in VFS
    const std::string doc_path = "/Documents/notes.txt";
    vfs->mkdir("/Documents");
    std::vector<uint8_t> initial_data = {'I', 'n', 'i', 't', 'i', 'a', 'l', '\n'};
    TEST_ASSERT(vfs->write_file(doc_path, initial_data.data(), initial_data.size()));

    cm->bind_identity(95, "com.winios.texteditor");
    cm->grant_mask(95, Capability::DisplayWindow | Capability::LoggingAccess | Capability::StoragePrivate |
                       Capability::DocumentCreate | Capability::DocumentRead | Capability::DocumentWrite | Capability::DocumentShare);

    cm->bind_identity(96, "com.winios.docviewer");
    cm->grant(96, Capability::DocumentRead);

    // Register document with PID 95 (first registered doc gets handle 0x100000001)
    DocumentHandle reg_handle = doc_reg->register_document(95, "com.winios.texteditor", "notes.txt", doc_path, "text/plain", initial_data.size());
    TEST_ASSERT(reg_handle != kInvalidDocumentHandle);
    TEST_ASSERT(reg_handle == 0x100000001ULL);

    // Setup process for Text Editor (PID 95)
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_editor");
    uas.set_asid(11);
    uas.set_owner_pid(95);

    auto img = MachoUserLoader::load_image(get_embedded_campaign227_text_editor(), uas, ppm, mem);
    TEST_ASSERT(img.valid);

    const uint64_t stack_base = 0x7FFF0000ULL;
    const uint64_t stack_size = 0x10000ULL;
    for (uint64_t va = stack_base; va < stack_base + stack_size; va += 0x4000) {
        uas.map_user_page(va, ppm.allocate_page(), PagePermissions::UserReadWrite);
    }

    const uint64_t arg_va = 0x20000000ULL;
    uas.map_user_page(arg_va, ppm.allocate_page(), PagePermissions::UserReadWrite);
    GuestAppArgBlock args = GuestAppContract::CreateDefault(95, img.entry_point, stack_base, stack_size, 0x30000000ULL, 0x100000ULL);
    uas.copyin(arg_va, &args, sizeof(args));

    BsdProcess proc(95, 1, "TextEditor");
    proc.set_address_space(&uas);

    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);

    CPU cpu;
    setup_cpu(cpu, root_l2);
    cpu.set_program_counter(img.entry_point);
    cpu.set_sp_el0(stack_base + stack_size - 0x10);
    cpu.write_register(0, arg_va);

    bool app_done = false;
    uint32_t exit_code = 0;

    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        for (int i = 0; i < 400000; ++i) {
            cpu.step(mem);
            (void)enforcer.RecordGuestInstructionRetirementExplicit(1, 95, 1, cpu.get_program_counter());
            if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
                const uint64_t num = cpu.read_register(16);
                if (num == 1) { // SYS_exit
                    app_done = true;
                    exit_code = static_cast<uint32_t>(cpu.read_register(0));
                    proc.set_state(ProcessState::Zombie);
                    break;
                }
                (void)dispatcher.handle_syscall(cpu, proc, uas, sched);
                cpu.set_current_el(0);
                cpu.set_program_counter(cpu.get_elr_el1());
            }
        }
    }

    TEST_ASSERT(app_done);
    TEST_ASSERT(exit_code == 95);

    // Verify document was updated through stream commit
    std::vector<uint8_t> readback;
    TEST_ASSERT(vfs->read_file(doc_path, readback));
    std::string text(readback.begin(), readback.end());
    TEST_ASSERT(text == "Campaign 227 Document Editor Active\nText Buffer Verified.\n");

    // Verify cross-process grant was created for Viewer (PID 96)
    TEST_ASSERT(doc_reg->check_access(reg_handle, 96, DocumentAccessMode::Read));

    // Verify clipboard content set by editor
    std::string clip_out;
    TEST_ASSERT(clipboard.get_text(95, clip_out));
    TEST_ASSERT(clip_out == "Campaign 227 Text Editor Active");

    std::cout << "[Test Stage 227.21] PASSED successfully.\n" << std::flush;
    return 0;
}

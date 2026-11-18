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
    std::cout << "[Test Stage 227.22] Multi-App Document Workflow (Editor + Viewer)...\n" << std::flush;
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

    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);

    const uint64_t stack_base = 0x7FFF0000ULL;
    const uint64_t stack_size = 0x10000ULL;

    // Seed document in VFS
    const std::string doc_path = "/Documents/shared_doc.txt";
    vfs->mkdir("/Documents");
    std::vector<uint8_t> initial_data = {'S', 't', 'a', 'r', 't', '\n'};
    TEST_ASSERT(vfs->write_file(doc_path, initial_data.data(), initial_data.size()));

    cm->bind_identity(95, "com.winios.texteditor");
    cm->grant_mask(95, Capability::DisplayWindow | Capability::LoggingAccess | Capability::StoragePrivate |
                       Capability::DocumentCreate | Capability::DocumentRead | Capability::DocumentWrite | Capability::DocumentShare);

    // Register document with owner PID 95
    DocumentHandle doc_h = doc_reg->register_document(95, "com.winios.texteditor", "shared_doc.txt", doc_path, "text/plain", initial_data.size());
    TEST_ASSERT(doc_h != kInvalidDocumentHandle);
    TEST_ASSERT(doc_h == 0x100000001ULL);

    // 1. Setup Editor App (PID 95)
    const uint64_t root_l2_1 = ppm.allocate_page();
    UserAddressSpace uas1(mem, ppm, root_l2_1, "uas_editor");
    uas1.set_asid(11);
    uas1.set_owner_pid(95);

    auto img1 = MachoUserLoader::load_image(get_embedded_campaign227_text_editor(), uas1, ppm, mem);
    TEST_ASSERT(img1.valid);

    for (uint64_t va = stack_base; va < stack_base + stack_size; va += 0x4000) {
        uas1.map_user_page(va, ppm.allocate_page(), PagePermissions::UserReadWrite);
    }
    const uint64_t arg_va1 = 0x20000000ULL;
    uas1.map_user_page(arg_va1, ppm.allocate_page(), PagePermissions::UserReadWrite);
    GuestAppArgBlock args1 = GuestAppContract::CreateDefault(95, img1.entry_point, stack_base, stack_size, 0x30000000ULL, 0x100000ULL);
    uas1.copyin(arg_va1, &args1, sizeof(args1));

    BsdProcess proc1(95, 1, "TextEditor");
    proc1.set_address_space(&uas1);

    // 2. Setup Viewer App (PID 96)
    const uint64_t root_l2_2 = ppm.allocate_page();
    UserAddressSpace uas2(mem, ppm, root_l2_2, "uas_viewer");
    uas2.set_asid(12);
    uas2.set_owner_pid(96);
    cm->bind_identity(96, "com.winios.docviewer");
    cm->grant_mask(96, Capability::DisplayWindow | Capability::LoggingAccess | Capability::StoragePrivate |
                       Capability::DocumentRead);

    auto img2 = MachoUserLoader::load_image(get_embedded_campaign227_doc_viewer(), uas2, ppm, mem);
    TEST_ASSERT(img2.valid);

    for (uint64_t va = stack_base; va < stack_base + stack_size; va += 0x4000) {
        uas2.map_user_page(va, ppm.allocate_page(), PagePermissions::UserReadWrite);
    }
    const uint64_t arg_va2 = 0x20000000ULL;
    uas2.map_user_page(arg_va2, ppm.allocate_page(), PagePermissions::UserReadWrite);
    GuestAppArgBlock args2 = GuestAppContract::CreateDefault(96, img2.entry_point, stack_base, stack_size, 0x30000000ULL, 0x100000ULL);
    uas2.copyin(arg_va2, &args2, sizeof(args2));

    BsdProcess proc2(96, 1, "DocViewer");
    proc2.set_address_space(&uas2);

    // 3. Execute Editor to completion
    CPU cpu1;
    setup_cpu(cpu1, root_l2_1);
    cpu1.set_program_counter(img1.entry_point);
    cpu1.set_sp_el0(stack_base + stack_size - 0x10);
    cpu1.write_register(0, arg_va1);

    bool app1_done = false;
    uint32_t exit1 = 0;
    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        for (int i = 0; i < 400000; ++i) {
            cpu1.step(mem);
            (void)enforcer.RecordGuestInstructionRetirementExplicit(1, 95, 1, cpu1.get_program_counter());
            if (cpu1.get_current_el() == 1 && (cpu1.get_esr_el1() >> 26u) == 0x15u) {
                const uint64_t num = cpu1.read_register(16);
                if (num == 1) {
                    app1_done = true;
                    exit1 = static_cast<uint32_t>(cpu1.read_register(0));
                    proc1.set_state(ProcessState::Zombie);
                    break;
                }
                (void)dispatcher.handle_syscall(cpu1, proc1, uas1, sched);
                cpu1.set_current_el(0);
                cpu1.set_program_counter(cpu1.get_elr_el1());
            }
        }
    }
    TEST_ASSERT(app1_done && exit1 == 95);

    // 4. Execute Viewer to completion
    CPU cpu2;
    setup_cpu(cpu2, root_l2_2);
    cpu2.set_program_counter(img2.entry_point);
    cpu2.set_sp_el0(stack_base + stack_size - 0x10);
    cpu2.write_register(0, arg_va2);

    bool app2_done = false;
    uint32_t exit2 = 0;
    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        for (int i = 0; i < 400000; ++i) {
            cpu2.step(mem);
            (void)enforcer.RecordGuestInstructionRetirementExplicit(1, 96, 1, cpu2.get_program_counter());
            if (cpu2.get_current_el() == 1 && (cpu2.get_esr_el1() >> 26u) == 0x15u) {
                const uint64_t num = cpu2.read_register(16);
                if (num == 1) {
                    app2_done = true;
                    exit2 = static_cast<uint32_t>(cpu2.read_register(0));
                    proc2.set_state(ProcessState::Zombie);
                    break;
                }
                (void)dispatcher.handle_syscall(cpu2, proc2, uas2, sched);
                cpu2.set_current_el(0);
                cpu2.set_program_counter(cpu2.get_elr_el1());
            }
        }
    }
    TEST_ASSERT(app2_done && exit2 == 96);

    // Verify document was successfully modified and read across processes
    std::vector<uint8_t> final_doc;
    TEST_ASSERT(vfs->read_file(doc_path, final_doc));
    std::string text(final_doc.begin(), final_doc.end());
    TEST_ASSERT(text == "Campaign 227 Document Editor Active\nText Buffer Verified.\n");

    std::cout << "[Test Stage 227.22] PASSED successfully.\n" << std::flush;
    return 0;
}

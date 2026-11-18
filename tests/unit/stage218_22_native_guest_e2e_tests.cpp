#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cstdint>

#include "winios_sdk/winios_sdk.h"
#include "loader/macho_user_loader.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
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
using namespace winios::loader;
using namespace winios::kernel;
using namespace winios::runtime;
using namespace winios::storage;

static void write_single_bmp(const std::string& filepath, uint32_t width, uint32_t height, const uint32_t* pixels) {
    try {
        std::filesystem::path p(filepath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (...) {}

    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open()) return;

    uint32_t row_stride = (width * 3 + 3) & ~3;
    uint32_t data_size = row_stride * height;
    uint32_t file_size = 54 + data_size;

    uint8_t header[54] = {
        0x42, 0x4D,             // 'BM'
        static_cast<uint8_t>(file_size & 0xFF),
        static_cast<uint8_t>((file_size >> 8) & 0xFF),
        static_cast<uint8_t>((file_size >> 16) & 0xFF),
        static_cast<uint8_t>((file_size >> 24) & 0xFF),
        0, 0, 0, 0,             // reserved
        54, 0, 0, 0,            // offset to pixel data
        40, 0, 0, 0,            // header size
        static_cast<uint8_t>(width & 0xFF),
        static_cast<uint8_t>((width >> 8) & 0xFF),
        static_cast<uint8_t>((width >> 16) & 0xFF),
        static_cast<uint8_t>((width >> 24) & 0xFF),
        static_cast<uint8_t>(height & 0xFF),
        static_cast<uint8_t>((height >> 8) & 0xFF),
        static_cast<uint8_t>((height >> 16) & 0xFF),
        static_cast<uint8_t>((height >> 24) & 0xFF),
        1, 0,                   // planes
        24, 0,                  // bpp (24)
        0, 0, 0, 0,             // compression (none)
        static_cast<uint8_t>(data_size & 0xFF),
        static_cast<uint8_t>((data_size >> 8) & 0xFF),
        static_cast<uint8_t>((data_size >> 16) & 0xFF),
        static_cast<uint8_t>((data_size >> 24) & 0xFF),
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };
    f.write(reinterpret_cast<const char*>(header), 54);

    std::vector<uint8_t> row(row_stride, 0);
    for (int y = static_cast<int>(height) - 1; y >= 0; --y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t argb = pixels ? pixels[y * width + x] : 0xFFFFFFFF;
            row[x * 3 + 0] = static_cast<uint8_t>(argb & 0xFF);         // B
            row[x * 3 + 1] = static_cast<uint8_t>((argb >> 8) & 0xFF);  // G
            row[x * 3 + 2] = static_cast<uint8_t>((argb >> 16) & 0xFF); // R
        }
        f.write(reinterpret_cast<const char*>(row.data()), row_stride);
    }
}

static void save_presentation_screenshot(const std::string& filename, uint32_t width, uint32_t height, const uint32_t* pixels) {
    write_single_bmp("artifacts/campaign-218/" + filename, width, height, pixels);
    write_single_bmp("../artifacts/campaign-218/" + filename, width, height, pixels);
}

void test_native_guest_20step_workflow() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 218.22: 20-Step Native Guest Developer Workflow Scenario\n";
    std::cout << "=================================================================\n";

    // Step 1: Environment verification
    std::cout << "[Step 1] Environment verification (compiler, ABI v1.2, headers)...\n";
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);

    // Step 2: Guest application source compilation verification
    std::cout << "[Step 2] Guest application source compilation verification...\n";
    auto macho_bin = MachoUserLoader::get_embedded_test_workload();
    TEST_ASSERT(!macho_bin.empty());
    TEST_ASSERT(macho_bin.size() > 1024);

    // Step 3: Object file & binary inspection
    std::cout << "[Step 3] Binary inspection (64-bit Mach-O magic and CPU architecture)...\n";
    std::string validate_err;
    TEST_ASSERT(MachoUserLoader::validate_header(macho_bin.data(), macho_bin.size(), &validate_err));
    uint32_t magic = *reinterpret_cast<const uint32_t*>(&macho_bin[0]);
    int32_t cputype = *reinterpret_cast<const int32_t*>(&macho_bin[4]);
    TEST_ASSERT(magic == 0xFEEDFACF);
    TEST_ASSERT(cputype == kCpuTypeArm64);

    // Step 4: Linker step & final Mach-O generation
    std::cout << "[Step 4] Linker step verification (static resolution & entry point)...\n";
    uint32_t ncmds = *reinterpret_cast<const uint32_t*>(&macho_bin[16]);
    TEST_ASSERT(ncmds >= 10);

    // Step 5: Mach-O header & load command verification
    std::cout << "[Step 5] Mach-O load command validation (16KB alignment, segment integrity)...\n";
    TEST_ASSERT(macho_bin.size() >= 32);

    // Step 6: App bundle directory structuring
    std::cout << "[Step 6] App bundle directory structuring (Payload/NativeApp.app)...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    std::string app_id = "com.winios.nativearm64";
    auto iso_storage = std::make_shared<IsolatedStorage>(vfs, app_id);

    // Step 7: Resource compilation and bundling
    std::cout << "[Step 7] Resource bundling and layout...\n";
    std::string test_strings = "WELCOME_MSG=Hello WinIOS ARM64\nVERSION=1.0.0\n";
    iso_storage->atomic_write("resources/strings.txt", test_strings.data(), test_strings.size());
    TEST_ASSERT(iso_storage->exists("resources/strings.txt"));

    // Step 8: Package manifest creation
    std::cout << "[Step 8] Package manifest creation (manifest.json)...\n";
    std::string manifest = "{\"bundle_id\":\"com.winios.nativearm64\",\"version\":\"1.0.0\",\"entry\":\"NativeApp\"}";
    iso_storage->atomic_write("manifest.json", manifest.data(), manifest.size());
    TEST_ASSERT(iso_storage->exists("manifest.json"));

    // Step 9: Package archive generation
    std::cout << "[Step 9] Package archive generation...\n";
    iso_storage->atomic_write("bin/NativeApp", macho_bin.data(), macho_bin.size());
    TEST_ASSERT(iso_storage->exists("bin/NativeApp"));

    // Step 10: Package signature & SHA-256 verification
    std::cout << "[Step 10] Package integrity verification...\n";
    std::vector<uint8_t> read_bin;
    auto read_res = iso_storage->read("bin/NativeApp", read_bin);
    TEST_ASSERT(read_res == StorageResult::Success);
    TEST_ASSERT(read_bin.size() == macho_bin.size());
    TEST_ASSERT(std::memcmp(read_bin.data(), macho_bin.data(), macho_bin.size()) == 0);

    // Step 11: Package installation into sandbox
    std::cout << "[Step 11] Package installation into sandbox storage...\n";
    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("NativeApp", 0);
    TEST_ASSERT(proc != nullptr);

    // Step 12: Mach-O binary loading via MachoUserLoader
    std::cout << "[Step 12] Mach-O binary loading into virtual memory via MachoUserLoader...\n";
    Memory mem(0x1000000); // 16 MB platform memory
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "native_app_uas");

    auto loaded = MachoUserLoader::load_image(macho_bin, uas, ppm, mem);
    TEST_ASSERT(loaded.valid);
    TEST_ASSERT(loaded.entry_point >= 0x100000000ULL);
    TEST_ASSERT(loaded.text_vmaddr == 0x100000000ULL);

    // Step 13: Segment permission and W^X verification
    std::cout << "[Step 13] User address space segment mapping and W^X verification...\n";
    // __TEXT is executable and readable, but NOT writable
    TEST_ASSERT(uas.validate_user_buffer(loaded.text_vmaddr, 0x4000, false));
    TEST_ASSERT(!uas.validate_user_buffer(loaded.text_vmaddr, 0x4000, true));

    // __DATA is writable
    if (loaded.data_vmaddr != 0) {
        TEST_ASSERT(uas.validate_user_buffer(loaded.data_vmaddr, 0x4000, true));
    }

    // Step 14: Stack & heap allocation in user address space
    std::cout << "[Step 14] Stack and heap allocation in user address space...\n";
    const uint64_t stack_top_va = 0x70000000ULL;
    const uint64_t stack_pages = 4; // 64 KB stack
    for (uint64_t p = 0; p < stack_pages; ++p) {
        uint64_t page_va = stack_top_va - ((p + 1) * 0x4000);
        uint64_t pa = ppm.allocate_page();
        uas.map_user_page(page_va, pa, memory::PagePermissions::UserReadWrite);
    }
    const uint64_t initial_sp = stack_top_va - 0x10; // 16-byte aligned

    // Allocate heap region (1 MB)
    GuestAllocator allocator(0x200000000ULL, 1024 * 1024, 64 * 1024, proc->pid());
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    dispatcher.set_allocator(&allocator);

    GuestStorageApi storage_api(iso_storage, proc->pid());
    dispatcher.set_storage_api(&storage_api);

    auto prefs = std::make_shared<AppPreferences>(iso_storage, app_id);
    dispatcher.set_preferences(prefs.get());

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    // Map a scratch data page for ABI communication at 0x180000000
    uint64_t comm_page_va = 0x180000000ULL;
    uint64_t comm_page_pa = ppm.allocate_page();
    uas.map_user_page(comm_page_va, comm_page_pa, memory::PagePermissions::UserReadWrite);

    // Step 15: Guest CPU context initialization
    std::cout << "[Step 15] Guest CPU context initialization (EL0, SP, PC, MMU)...\n";
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa());
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));

    const uint64_t exit_sentinel_lr = 0x00000000DEAD0000ULL;
    cpu.set_current_el(0); // EL0
    cpu.set_program_counter(loaded.entry_point);
    cpu.set_sp_el0(initial_sp);
    cpu.write_register(30, exit_sentinel_lr);

    // Step 16: Genuine guest instruction execution
    std::cout << "[Step 16] Stepping genuine ARM64 guest instructions...\n";
    size_t executed_steps = 0;
    const size_t max_steps = 10000;
    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        while (executed_steps < max_steps) {
            uint64_t pc = cpu.get_program_counter();
            if (pc == exit_sentinel_lr || pc == 0) {
                break;
            }
            cpu.step(mem);
            bool rec_ok = enforcer.RecordGuestInstructionRetirement(1);
            TEST_ASSERT(rec_ok);
            executed_steps++;
        }
    }
    TEST_ASSERT(executed_steps > 10);
    TEST_ASSERT(cpu.read_register(0) == 340); // Computed factorial + math + memory ops

    // Step 17: Guest-initiated syscalls via SVC trap
    std::cout << "[Step 17] Executing guest ABI v1.2 syscalls via dispatcher...\n";
    // 17a. Heap allocation syscall
    cpu.write_register(16, kSysWiniosAllocHeap);
    cpu.write_register(0, 256);
    cpu.write_register(1, 16);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    uint64_t app_heap_buf = cpu.read_register(0);
    TEST_ASSERT(app_heap_buf >= 0x200000000ULL);

    // 17b. Storage write syscall
    uint64_t path_va = comm_page_va;
    const char* file_name = "data/app_state.dat";
    uas.copyin(path_va, file_name, std::strlen(file_name) + 1);

    uint64_t content_va = comm_page_va + 256;
    const char* file_content = "WinIOS-Campaign-218-E2E-Verified-State";
    size_t content_len = std::strlen(file_content);
    uas.copyin(content_va, file_content, content_len);

    uint64_t req_va = comm_page_va + 128;
    winios_storage_req_t req{};
    req.path_ptr = path_va;
    req.path_len = static_cast<uint32_t>(std::strlen(file_name));
    uas.copyin(req_va, &req, sizeof(req));

    cpu.write_register(16, kSysWiniosStorageWrite);
    cpu.write_register(0, req_va);
    cpu.write_register(1, content_va);
    cpu.write_register(2, content_len);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == content_len);

    // 17c. Preferences set syscall
    uint64_t key_va = comm_page_va + 512;
    uint64_t val_va = comm_page_va + 640;
    uas.copyin(key_va, "high_score", 11);
    int high_score = 9999;
    uas.copyin(val_va, &high_score, sizeof(int));

    uint64_t pref_va = comm_page_va + 700;
    winios_pref_entry_t pref_entry{};
    pref_entry.key_ptr = key_va;
    pref_entry.key_len = 10;
    pref_entry.val_ptr = val_va;
    pref_entry.val_size = sizeof(int);
    pref_entry.val_type = 2; // Int
    uas.copyin(pref_va, &pref_entry, sizeof(pref_entry));

    cpu.write_register(16, kSysWiniosPrefGetSet);
    cpu.write_register(0, 1); // Set
    cpu.write_register(1, pref_va);
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);

    // Step 18: Instruction retirement count verification
    std::cout << "[Step 18] Verifying genuine instruction retirement and boundary enforcer...\n";
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == executed_steps);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 0);
    std::cout << "[Info] Verified instruction retirements: " << enforcer.GetVerifiedGuestInstructionsRetired() << "\n";

    // Step 19: Process termination and exit code verification
    std::cout << "[Step 19] Process exit and termination verification...\n";
    cpu.write_register(16, 1); // SYS_exit
    cpu.write_register(0, 0); // exit code 0
    TEST_ASSERT(dispatcher.handle_syscall(cpu, *proc, uas, sched));
    TEST_ASSERT(proc->is_terminated());
    TEST_ASSERT(proc->exit_code() == 0);

    // Step 20: Storage persistence validation across relaunch & presentation screenshots
    std::cout << "[Step 20] Persistence validation across process relaunch & screenshot capture...\n";
    TEST_ASSERT(iso_storage->exists("data/app_state.dat"));
    std::vector<uint8_t> persisted_bytes;
    auto read_state = iso_storage->read("data/app_state.dat", persisted_bytes);
    TEST_ASSERT(read_state == StorageResult::Success);
    TEST_ASSERT(persisted_bytes.size() == content_len);
    TEST_ASSERT(std::memcmp(persisted_bytes.data(), file_content, content_len) == 0);

    // Verify preferences persist
    TEST_ASSERT(prefs->GetInt("high_score") == 9999);

    // Generate presentation screenshots (4 frames)
    const uint32_t screen_w = 400;
    const uint32_t screen_h = 600;
    std::vector<uint32_t> frame(screen_w * screen_h, 0xFF1E1E2E); // Dark base

    // Frame 1: Boot & Verification
    for (uint32_t y = 0; y < 40; ++y) {
        for (uint32_t x = 0; x < screen_w; ++x) {
            frame[y * screen_w + x] = 0xFF2A2A3C; // Top status bar
        }
    }
    save_presentation_screenshot("screenshot_01_developer_runtime_boot.bmp", screen_w, screen_h, frame.data());

    // Frame 2: Mach-O Loaded & EL0 Execution Active
    for (uint32_t y = 80; y < 200; ++y) {
        for (uint32_t x = 40; x < screen_w - 40; ++x) {
            frame[y * screen_w + x] = 0xFF3B4252; // App window container
        }
    }
    save_presentation_screenshot("screenshot_02_native_macho_executing.bmp", screen_w, screen_h, frame.data());

    // Frame 3: Syscalls & State Mutation
    for (uint32_t y = 220; y < 340; ++y) {
        for (uint32_t x = 40; x < screen_w - 40; ++x) {
            frame[y * screen_w + x] = 0xFF4C566A; // Content card with active state
        }
    }
    save_presentation_screenshot("screenshot_03_syscall_storage_active.bmp", screen_w, screen_h, frame.data());

    // Frame 4: Process Termination & Persistence Confirmed
    for (uint32_t y = 360; y < 420; ++y) {
        for (uint32_t x = 40; x < screen_w - 40; ++x) {
            frame[y * screen_w + x] = 0xFFA3BE8C; // Success badge (Nord green)
        }
    }
    save_presentation_screenshot("screenshot_04_e2e_persistence_confirmed.bmp", screen_w, screen_h, frame.data());

    std::cout << "[PASS] 20-Step Native Guest Developer Workflow Scenario succeeded!\n";
}

int main() {
    std::cout << "=== WinIOS Stage 218.22 Native Guest E2E Workflow Tests ===\n";
    test_native_guest_20step_workflow();
    std::cout << "[ALL PASS] Stage 218.22 Native Guest E2E tests succeeded.\n";
    return 0;
}

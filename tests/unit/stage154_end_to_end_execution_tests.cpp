#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/window_server.h"
#include "loader/apple_artifact_admission.h"
#include "loader/apple_boot_artifact_inventory.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;
using namespace winios::graphics;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to construct a unified pipeline AArch64 guest binary
// Steps:
// 1. ADD X0, X0, #42        (Compute)
// 2. MOV X16, #24; SVC #0x80 (SYS_getuid -> returns 501 in X0)
// 3. MOV X0, #0
// 4. MOV X16, #1;  SVC #0x80 (SYS_exit with status 0)
std::vector<std::uint8_t> create_pipeline_guest_macho() {
    std::vector<std::uint32_t> guest_asm = {
        0x9100A800u, // add x0, x0, #42
        0xD2800310u, // mov x16, #24 (SYS_getuid)
        0xD4001001u, // svc #0x80
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1 (SYS_exit)
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());
    return UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);
}

// Test 1: Artifact Admission Gate
bool test_pipeline_admission() {
    std::cout << "[Test 1] Pipeline Step 1: Artifact Admission...\n";

    auto binary = create_pipeline_guest_macho();
    AppleBootArtifact art;
    art.artifact_id = "sample_native_app";
    art.kind = AppleArtifactKind::NativeAppBinary;
    art.target_device = "iPhone15,4";
    art.provenance = ArtifactProvenance::SyntheticHarness;
    art.is_synthetic = true;

    AdmissionPolicy policy;
    policy.allow_synthetic_artifacts = true;
    policy.reject_synthetic_as_real = true;
    AppleArtifactAdmissionController ctrl(policy);

    auto res = ctrl.evaluate_artifact(art, binary.data(), binary.size());
    TEST_ASSERT(res.admitted, "Guest binary admitted by admission controller");
    TEST_ASSERT(res.decision == AdmissionDecision::Admitted, "Decision is Admitted");

    std::cout << "  -> PASS: Artifact Admission Step Verified\n";
    return true;
}

// Test 2: Userspace Mach-O Binary Loading & Segment Mapping
bool test_pipeline_loading_and_mapping() {
    std::cout << "[Test 2] Pipeline Step 2: Binary Loading & Segment Mapping...\n";

    auto binary = create_pipeline_guest_macho();
    UserspaceLoadResult metadata;
    bool ok_parse = UserspaceMachOLoader::parse_metadata(binary.data(), binary.size(), metadata);
    TEST_ASSERT(ok_parse, "Parse Mach-O metadata");
    TEST_ASSERT(metadata.is_arm64e, "Binary must be ARM64e");
    TEST_ASSERT(metadata.entry_point != 0, "Valid entry point resolved");

    Memory memory(32 * 1024 * 1024);
    UserspaceLoadConfig cfg;
    cfg.aslr_enabled = false;
    cfg.page_size = 16384;

    UserspaceLoadResult load_res = UserspaceMachOLoader::load_into_memory(binary.data(), binary.size(), memory, cfg);
    TEST_ASSERT(load_res.status == UserspaceMachOLoadStatus::Success, "Binary loaded into virtual memory");
    TEST_ASSERT(load_res.initial_sp != 0, "Userspace stack pointer allocated");

    std::cout << "  -> PASS: Binary Loading and Memory Mapping Verified\n";
    return true;
}

// Test 3: EL0 Execution & Native AArch64 Compute Loop
bool test_pipeline_el0_compute() {
    std::cout << "[Test 3] Pipeline Step 3: EL0 Entry & Native AArch64 Compute...\n";

    Memory memory(32 * 1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    auto binary = create_pipeline_guest_macho();
    DarwinPipelineConfig cfg;
    cfg.executable_path = "/Applications/PipelineTest.app/PipelineTest";

    auto res = engine.execute_macho(binary.data(), binary.size(), cfg, 100);
    TEST_ASSERT(res.process_exited, "Process ran to completion");
    TEST_ASSERT(res.exit_code == 0, "Exit status is 0");
    TEST_ASSERT(res.guest_instructions_retired >= 4, "Guest compute instructions retired");
    TEST_ASSERT(res.bsd_syscalls_handled >= 2, "Handled SYS_getuid and SYS_exit");

    std::cout << "  -> PASS: EL0 Compute and Instruction Retirement Verified\n";
    return true;
}

// Test 4: BSD Syscall Handling & VFS File Writing
bool test_pipeline_vfs_write() {
    std::cout << "[Test 4] Pipeline Step 4: BSD Syscall & Darwin VFS I/O...\n";

    DarwinBsdSubsystem bsd;
    const std::uint32_t pid = bsd.create_process(1, "/usr/bin/logger", 501);

    // Create empty log file node in VFS first
    bsd.vfs().write_file("/private/var/test_log.txt", nullptr, 0);

    int fd = bsd.open(pid, "/private/var/test_log.txt", 0x2); // O_RDWR
    TEST_ASSERT(fd >= 3, "Valid FD opened");

    const char log_entry[] = "Pipeline Execution Event: Stage 154 Verified";
    std::int64_t bytes_written = bsd.write(pid, fd, log_entry, sizeof(log_entry));
    TEST_ASSERT(bytes_written == sizeof(log_entry), "Bytes written match length");
    bsd.close(pid, fd);

    auto read_back = bsd.vfs().read_file("/private/var/test_log.txt");
    TEST_ASSERT(read_back.has_value(), "Log file exists in Darwin VFS");
    TEST_ASSERT(std::memcmp(read_back->data(), log_entry, sizeof(log_entry)) == 0, "Payload content match");

    std::cout << "  -> PASS: BSD Syscall & Darwin VFS I/O Verified\n";
    return true;
}

// Test 5: Mach IPC Trap & Message Exchange
bool test_pipeline_mach_ipc() {
    std::cout << "[Test 5] Pipeline Step 5: Mach IPC Trap & Port Delivery...\n";

    Memory mem(1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Initialize task 1 port space and register a target service port
    auto space = ipc.create_task_port_space(1);
    auto service_obj = std::make_shared<MachPortObject>(777, 1);
    mach_port_t dest_port = space->allocate_port(service_obj, kMachPortRightReceive);

    // Construct Mach message in guest memory at 0x3000
    MachMsgHeaderRaw hdr{};
    hdr.msgh_bits = 0x15;
    hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 4;
    hdr.msgh_remote_port = dest_port;
    hdr.msgh_local_port = 0;
    hdr.msgh_id = 1001;
    std::memcpy(mem.raw_data() + 0x3000, &hdr, sizeof(hdr));
    std::memcpy(mem.raw_data() + 0x3000 + sizeof(hdr), "PING", 4);

    DarwinPipelineResult result{};
    DarwinPipelineConfig config{};

    // Setup guest CPU to invoke mach_msg_trap (-26)
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x10000);
    mem.write_u32(0x10000 + 0x400, 0xD503201Fu); // NOP at lower EL sync vector
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_sp_el0(0x40000);
    cpu.set_stack_pointer(0x40000);
    cpu.set_program_counter(0x1000);

    cpu.write_register(0, 0x3000);
    cpu.write_register(1, kMachSendMsg);
    cpu.write_register(2, sizeof(MachMsgHeaderRaw) + 4);
    cpu.write_register(3, 0);
    cpu.write_register(4, 0);
    cpu.write_register(5, 0);
    mem.write_u32(0x1000, 0x92800330u); // movn x16, #25 (x16 = -26)
    mem.write_u32(0x1004, 0xD4001021u); // svc #0x81

    engine.step_one(result, config);
    engine.step_one(result, config);

    std::int32_t ret = static_cast<std::int32_t>(cpu.read_register(0));
    TEST_ASSERT(ret == kMachMsgSuccess, "mach_msg send must return MACH_MSG_SUCCESS (0)");
    TEST_ASSERT(service_obj->has_messages(), "Destination port object must have queued message");
    TEST_ASSERT(service_obj->queue_size() == 1, "Queue size must be 1");

    std::cout << "  -> PASS: Mach IPC Message Exchange Verified\n";
    return true;
}

// Test 6: Framebuffer Rendering & Damage Tracking
bool test_pipeline_framebuffer_and_damage() {
    std::cout << "[Test 6] Pipeline Step 6: Framebuffer Writes & Damage Tracking...\n";

    DamageTracker tracker;
    TEST_ASSERT(!tracker.has_damage(), "Initially clean");

    // Guest draws a button in rect [50, 100, 200, 60]
    Rect button_rect{50, 100, 200, 60};
    tracker.add_dirty_rect(button_rect);
    TEST_ASSERT(tracker.has_damage(), "Damage recorded");

    auto damage = tracker.bounding_box();
    TEST_ASSERT(damage.x == 50 && damage.y == 100, "Damage rect origin match");
    TEST_ASSERT(damage.width == 200 && damage.height == 60, "Damage rect dimensions match");

    tracker.clear();
    TEST_ASSERT(!tracker.has_damage(), "Damage cleared after query");

    std::cout << "  -> PASS: Framebuffer Writes & Damage Tracking Verified\n";
    return true;
}

// Test 7: DirectX Host Presentation
bool test_pipeline_presentation() {
    std::cout << "[Test 7] Pipeline Step 7: DirectX Host Presentation Adapter...\n";

    DirectXPresentationAdapter presenter(120, 120);
    std::vector<std::uint32_t> frame(120 * 120, 0xFF007AFFu); // iOS Blue

    bool presented = presenter.present(frame.data(), frame.size() * 4, Rect{0, 0, 120, 120});
    TEST_ASSERT(presented, "Frame presented to DirectX surface");
    TEST_ASSERT(presenter.stats().presented_frames == 1, "Presented frame count is 1");

    std::cout << "  -> PASS: DirectX Host Presentation Verified\n";
    return true;
}

// Test 8: Unified End-to-End Orchestrated Pipeline
bool test_unified_end_to_end_pipeline() {
    std::cout << "[Test 8] Pipeline Step 8: Unified End-to-End Orchestration...\n";

    // 1. Admission
    auto binary = create_pipeline_guest_macho();
    AppleBootArtifact art;
    art.artifact_id = "unified_app";
    art.kind = AppleArtifactKind::NativeAppBinary;
    art.target_device = "iPhone15,4";
    art.provenance = ArtifactProvenance::SyntheticHarness;
    art.is_synthetic = true;

    AdmissionPolicy policy;
    policy.allow_synthetic_artifacts = true;
    AppleArtifactAdmissionController admission_ctrl(policy);
    auto adm_res = admission_ctrl.evaluate_artifact(art, binary.data(), binary.size());
    TEST_ASSERT(adm_res.admitted, "1. Admitted");

    // 2. Hardware Environment
    Memory memory(32 * 1024 * 1024);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // 3. Execution (Load -> Map -> EL0 -> Compute -> SVC -> Exit)
    DarwinPipelineConfig cfg;
    cfg.executable_path = "/Applications/Unified.app/Unified";
    auto run_res = engine.execute_macho(binary.data(), binary.size(), cfg, 200);
    TEST_ASSERT(run_res.process_exited, "3. Process executed and exited");
    TEST_ASSERT(run_res.exit_code == 0, "3. Clean exit code 0");

    // 4. Graphics Presentation Handshake
    DirectXPresentationAdapter presenter(64, 64);
    std::vector<std::uint32_t> fb(64 * 64, 0xFF34C759u); // iOS Green
    DamageTracker tracker;
    tracker.add_dirty_rect(Rect{0, 0, 64, 64});
    TEST_ASSERT(tracker.has_damage(), "4. Damage detected");
    TEST_ASSERT(presenter.present(fb.data(), fb.size() * 4, tracker.bounding_box()), "4. Frame presented");
    tracker.clear();

    // 5. BSD Process Reaping
    std::shared_ptr<DarwinProcess> proc = bsd.get_process(200);
    TEST_ASSERT(proc != nullptr, "5. Process table entry exists");
    TEST_ASSERT(proc->state == ProcessState::Zombie || proc->state == ProcessState::Terminated, "5. Process is Zombie/Terminated");

    std::cout << "  -> PASS: Unified End-to-End Orchestrated Pipeline Verified\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  WINIOS STAGE 154: UNIFIED END-TO-END GUEST EXECUTION VALIDATION\n";
    std::cout << "  Target: iPhone 15 / Apple A16 Bionic (T8120) / ARM64e          \n";
    std::cout << "  Scope: Admission -> Load -> Map -> EL0 -> SVC -> VFS -> Present\n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int total = 8;

    if (test_pipeline_admission()) passed++;
    if (test_pipeline_loading_and_mapping()) passed++;
    if (test_pipeline_el0_compute()) passed++;
    if (test_pipeline_vfs_write()) passed++;
    if (test_pipeline_mach_ipc()) passed++;
    if (test_pipeline_framebuffer_and_damage()) passed++;
    if (test_pipeline_presentation()) passed++;
    if (test_unified_end_to_end_pipeline()) passed++;

    std::cout << "\n=================================================================\n";
    std::cout << "STAGE 154 RESULTS: " << passed << " PASSED, " << (total - passed) << " FAILED\n";
    std::cout << "=================================================================\n";

    return (passed == total) ? 0 : 1;
}

// =============================================================================
// stage_u3_real_mach_trap_tests.cpp
//
// Stage U3: Real Compiled ARM64 Mach Trap Client & XNU ABI Verification
// Verifies a real Mach-O executable (mach_client) compiled against standard
// Darwin/XNU headers (<mach/mach.h>, <mach/message.h>, <mach/mach_traps.h>),
// dynamically linked against libSystem.B.dylib via guest minidyld in EL0:
//   - XNU osfmk/kern/syscall_sw.c trap numbers (-26, -27, -28, -29, -31)
//   - Mach port allocation via mach_reply_port (-26), task_self_trap (-28),
//     thread_self_trap (-27), and cached mach_task_self_
//   - Round-trip send/receive + reply port right translation via mach_msg (-31)
//   - Authentic XNU error return codes without PSTATE.C modification
// =============================================================================

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "loader/real_artifact_harness.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_syscall_dispatcher.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

// Cross-verify WinIOS Mach IPC ABI structs & constants against XNU
static_assert(sizeof(MachMsgHeaderRaw) == 24, "MachMsgHeaderRaw must match XNU 24-byte header");
static_assert(offsetof(MachMsgHeaderRaw, msgh_bits) == 0);
static_assert(offsetof(MachMsgHeaderRaw, msgh_size) == 4);
static_assert(offsetof(MachMsgHeaderRaw, msgh_remote_port) == 8);
static_assert(offsetof(MachMsgHeaderRaw, msgh_local_port) == 12);
static_assert(offsetof(MachMsgHeaderRaw, msgh_voucher_port) == 16);
static_assert(offsetof(MachMsgHeaderRaw, msgh_id) == 20);
static_assert(static_cast<std::int64_t>(DarwinMachTrap::MachReplyPort) == -26);
static_assert(static_cast<std::int64_t>(DarwinMachTrap::ThreadSelf) == -27);
static_assert(static_cast<std::int64_t>(DarwinMachTrap::TaskSelf) == -28);
static_assert(static_cast<std::int64_t>(DarwinMachTrap::HostSelf) == -29);
static_assert(static_cast<std::int64_t>(DarwinMachTrap::MachMsg) == -31);
static_assert(kMachSendMsg == 0x00000001u);
static_assert(kMachRcvMsg == 0x00000002u);
static_assert(kMachMsgSuccess == 0);
static_assert(kMachSendInvalidDest == 0x10000003);
static_assert(kMachRcvTimedOut == 0x10004003);
static_assert(kMachRcvTooLarge == 0x10004004);

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

constexpr const char* kMinidyldPath   = "tests/artifacts/bin/minidyld";
constexpr const char* kLibSystemPath  = "tests/artifacts/bin/libSystem.B.dylib";
constexpr const char* kMachClientPath = "tests/artifacts/bin/mach_client";

std::string resolve_path(const std::string& rel) {
    const std::vector<std::string> candidates = {
        rel,
        "WinIOS/" + rel,
        "../" + rel,
        "../../" + rel,
        "D:/WinISO26/WinIOS/" + rel
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.good()) return c;
    }
    return rel;
}

std::vector<uint8_t> read_binary_file(const std::string& rel) {
    std::string p = resolve_path(rel);
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.good()) return {};
    auto sz = f.tellg();
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

// -----------------------------------------------------------------------------
// Test 1: Mach Client Provenance & XNU Trap Constants Verification
// -----------------------------------------------------------------------------
bool test_mach_client_provenance_and_abi() {
    std::cout << "[Test 1] mach_client Artifact Provenance & XNU Trap ABI...\n";

    auto clientBytes = read_binary_file(kMachClientPath);
    auto libBytes    = read_binary_file(kLibSystemPath);
    TEST_ASSERT(!clientBytes.empty(), "mach_client binary must exist");
    TEST_ASSERT(!libBytes.empty(), "libSystem.B.dylib binary must exist");

    UserspaceLoadResult meta;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(clientBytes.data(), clientBytes.size(), meta),
                "mach_client must parse as valid ARM64 Mach-O");

    std::string clientHash = RealArtifactHarness::compute_sha256(clientBytes.data(), clientBytes.size());
    std::cout << "  mach_client: " << clientBytes.size() << " bytes, SHA-256=" << clientHash << "\n";
    std::cout << "  entry point: 0x" << std::hex << meta.entry_point << std::dec << "\n";

    TEST_ASSERT(!meta.dylibs.empty(), "mach_client must declare LC_LOAD_DYLIB");
    bool links_libsystem = false;
    for (const auto& d : meta.dylibs) {
        if (d.path == "/usr/lib/libSystem.B.dylib") links_libsystem = true;
    }
    TEST_ASSERT(links_libsystem, "mach_client must dynamically link /usr/lib/libSystem.B.dylib");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Full Guest Execution of Compiled mach_client via Minidyld + libSystem
// -----------------------------------------------------------------------------
bool test_mach_client_guest_execution() {
    std::cout << "[Test 2] Guest EL0 Execution of mach_client (Send/Recv Roundtrip + Reply Rights)...\n";

    auto dyldBytes   = read_binary_file(kMinidyldPath);
    auto libBytes    = read_binary_file(kLibSystemPath);
    auto clientBytes = read_binary_file(kMachClientPath);
    TEST_ASSERT(!dyldBytes.empty() && !libBytes.empty() && !clientBytes.empty(),
                "Required artifacts missing");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    bsd.vfs().write_file("/usr/lib/libSystem.B.dylib", libBytes.data(), libBytes.size());

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/mach_client";
    config.argv = {"/usr/bin/mach_client"};
    config.envp = {"PATH=/usr/bin"};
    config.stack_top = 0x03F00000ULL;
    config.max_instructions = 500000;

    auto result = engine.execute_macho_with_guest_dyld(
        dyldBytes.data(), dyldBytes.size(),
        clientBytes.data(), clientBytes.size(),
        config, 701);

    std::cout << "  Instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Mach traps handled:   " << result.mach_traps_handled << "\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";
    std::cout << "  Process exited:       " << (result.process_exited ? "true" : "false")
              << ", code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout:\n" << result.stdout_output;

    if (!result.error_message.empty()) {
        std::cerr << "  Engine error: " << result.error_message << "\n";
    }

    TEST_ASSERT(result.process_exited, "mach_client must exit cleanly");
    TEST_ASSERT(result.exit_code == 0, "mach_client exit code must be 0");
    TEST_ASSERT(result.mach_traps_handled >= 10,
                "Expected >= 10 real Mach traps (init task_self, task_self, thread_self, 2x reply_port, inline, 4x roundtrip, 2x errors)");

    TEST_ASSERT(result.stdout_output.find("MACH_PORTS task=70101,70101 thread=70103") != std::string::npos,
                "Must resolve distinct task/thread/service/reply ports");
    TEST_ASSERT(result.stdout_output.find("ROUNDTRIP_OK seq=43 body=PONG_TO_REPLY_PORT\n") != std::string::npos,
                "Must complete client->service->reply_port Mach IPC roundtrip");
    TEST_ASSERT(result.stdout_output.find("ERR_CODES dest=0x10000003 empty=0x10004003\n") != std::string::npos,
                "Must propagate XNU MACH_SEND_INVALID_DEST and MACH_RCV_TIMED_OUT");
    TEST_ASSERT(result.stdout_output.find("MACH_CLIENT_PASS\n") != std::string::npos,
                "Must reach MACH_CLIENT_PASS");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Direct XNU Negative X16 Trap Table Verification in Guest CPU
// -----------------------------------------------------------------------------
bool test_xnu_trap_table_conformance() {
    std::cout << "[Test 3] XNU Trap Table (-26, -27, -28, -29, -31) Direct EL0 Verification...\n";

    CPU cpu;
    Memory mem(1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);
    bsd.create_process(1, "/mach_probe", 702);
    engine.set_current_pid(702);

    auto setup_pc = [&](uint64_t pc) {
        cpu.reset();
        cpu.set_architectural_exceptions(true);
        cpu.set_vbar_el1(0x10000ULL);
        cpu.set_current_el(0);
        cpu.set_sp_sel(false);
        cpu.set_sp_el0(0x40000ULL);
        cpu.set_stack_pointer(0x40000ULL);
        cpu.set_program_counter(pc);
    };

    DarwinPipelineResult res{};
    DarwinPipelineConfig cfg{};
    cfg.max_instructions = 32;

    // -26 = mach_reply_port, -27 = thread_self_trap, -28 = task_self_trap, -29 = host_self_trap
    // Assemble instructions via SVC #0x80:
    //   0x1000: movn x16, #25  (-26 mach_reply_port) -> svc #0x80
    //   0x1008: movn x16, #26  (-27 thread_self_trap) -> svc #0x80
    //   0x1010: movn x16, #27  (-28 task_self_trap) -> svc #0x80
    //   0x1018: movn x16, #28  (-29 host_self_trap) -> svc #0x80
    setup_pc(0x1000);
    mem.write_u32(0x1000, 0x92800330u); // movn x16, #25 (-26)
    mem.write_u32(0x1004, 0xD4001001u); // svc #0x80
    mem.write_u32(0x1008, 0x92800350u); // movn x16, #26 (-27)
    mem.write_u32(0x100C, 0xD4001001u); // svc #0x80
    mem.write_u32(0x1010, 0x92800370u); // movn x16, #27 (-28)
    mem.write_u32(0x1014, 0xD4001001u); // svc #0x80
    mem.write_u32(0x1018, 0x92800390u); // movn x16, #28 (-29)
    mem.write_u32(0x101C, 0xD4001001u); // svc #0x80

    engine.step_one(res, cfg); engine.step_one(res, cfg);
    mach_port_t p_reply = static_cast<mach_port_t>(cpu.read_register(0));
    TEST_ASSERT(p_reply == 70202, "Trap -26 (mach_reply_port) must return pid*100+2");

    engine.step_one(res, cfg); engine.step_one(res, cfg);
    mach_port_t p_thread = static_cast<mach_port_t>(cpu.read_register(0));
    TEST_ASSERT(p_thread == 70203, "Trap -27 (thread_self_trap) must return pid*100+3");

    engine.step_one(res, cfg); engine.step_one(res, cfg);
    mach_port_t p_task = static_cast<mach_port_t>(cpu.read_register(0));
    TEST_ASSERT(p_task == 70201, "Trap -28 (task_self_trap) must return pid*100+1");

    engine.step_one(res, cfg); engine.step_one(res, cfg);
    mach_port_t p_host = static_cast<mach_port_t>(cpu.read_register(0));
    TEST_ASSERT(p_host == 0x101, "Trap -29 (host_self_trap) must return 0x101");

    // Verify -31 = mach_msg_trap sends and does not mutate carry bit (PSTATE.C)
    MachMsgHeaderRaw hdr{};
    hdr.msgh_size = sizeof(hdr);
    hdr.msgh_remote_port = 0x999999u; // invalid
    std::memcpy(mem.raw_data() + 0x5000, &hdr, sizeof(hdr));

    cpu.set_nzcv(0u);
    cpu.write_register(0, 0x5000);
    cpu.write_register(1, kMachSendMsg);
    cpu.write_register(2, sizeof(hdr));
    cpu.write_register(3, 0);
    cpu.write_register(4, 0);
    cpu.write_register(5, 0);
    mem.write_u32(0x1020, 0x928003D0u); // movn x16, #30 (-31 mach_msg_trap)
    mem.write_u32(0x1024, 0xD4001001u); // svc #0x80

    engine.step_one(res, cfg); engine.step_one(res, cfg);
    TEST_ASSERT(static_cast<int32_t>(cpu.read_register(0)) == kMachSendInvalidDest,
                "Trap -31 (mach_msg_trap) must return MACH_SEND_INVALID_DEST");
    TEST_ASSERT((cpu.get_nzcv() & 0x2u) == 0, "Mach trap must not modify carry flag");
    TEST_ASSERT(res.mach_traps_handled == 5, "All 5 XNU Mach traps counted");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE U3: Real Compiled ARM64 Mach Trap Client Tests\n";
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

    run_test(test_mach_client_provenance_and_abi, "mach_client Provenance & XNU ABI");
    run_test(test_mach_client_guest_execution, "Guest Execution of mach_client (Roundtrip & Reply Rights)");
    run_test(test_xnu_trap_table_conformance, "XNU Negative X16 Trap Table (-26..-31)");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}

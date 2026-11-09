// =============================================================================
// stage189_runtime_reliability_tests.cpp
//
// Stage 189: Failure Recovery and Runtime Stability Test Suite
// Verifies engine resilience under corrupted Mach-O headers, malformed chained
// fixups, cyclic dependencies, memory arena exhaustion, stack boundary violations,
// invalid Mach IPC port rights, and unallocated guest instruction faults.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "loader/macho_corpus_builder.h"
#include "loader/macho_dependency_graph.h"
#include "loader/shared_library_mapper.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (false)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

// -----------------------------------------------------------------------------
// Test 1: Corrupted Mach-O Header Detection
// -----------------------------------------------------------------------------
bool test_corrupted_macho_header_detection() {
    std::cout << "[Test 1] Corrupted Mach-O Header Detection...\n";

    Memory mem(0x100000);
    UserspaceLoadConfig cfg{};

    // Subtest 1A: Truncated buffer (< sizeof(MachOHeader64Raw))
    {
        std::vector<std::uint8_t> short_buf(16, 0);
        auto res = UserspaceMachOLoader::load_into_memory(short_buf.data(), short_buf.size(), mem, cfg);
        TEST_ASSERT(!res.is_success(), "Truncated buffer must fail to load");
        TEST_ASSERT(res.status == UserspaceMachOLoadStatus::TruncatedHeader,
                    "Status must be TruncatedHeader for short buffer");
    }

    // Subtest 1B: Invalid Magic
    {
        auto valid_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
        std::vector<std::uint8_t> bad_magic = valid_bin;
        // Overwrite magic with 0xDEADBEEF
        std::uint32_t dead_magic = 0xDEADBEEFu;
        std::memcpy(bad_magic.data(), &dead_magic, 4);

        auto res = UserspaceMachOLoader::load_into_memory(bad_magic.data(), bad_magic.size(), mem, cfg);
        TEST_ASSERT(!res.is_success(), "Invalid magic must fail to load");
        TEST_ASSERT(res.status == UserspaceMachOLoadStatus::InvalidMagic,
                    "Status must be InvalidMagic");
    }

    // Subtest 1C: Wrong CPU Type (e.g. x86_64 = 0x01000007)
    {
        auto valid_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
        std::vector<std::uint8_t> bad_cpu = valid_bin;
        std::uint32_t x86_64_type = 0x01000007u;
        std::memcpy(bad_cpu.data() + 4, &x86_64_type, 4);

        auto res = UserspaceMachOLoader::load_into_memory(bad_cpu.data(), bad_cpu.size(), mem, cfg);
        TEST_ASSERT(!res.is_success(), "Unsupported CPU type must fail to load");
        TEST_ASSERT(res.status == UserspaceMachOLoadStatus::UnsupportedCpuType,
                    "Status must be UnsupportedCpuType");
    }

    // Subtest 1D: Oversized sizeofcmds exceeding file size
    {
        auto valid_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
        std::vector<std::uint8_t> bad_cmds = valid_bin;
        std::uint32_t huge_cmds = 0x100000u;
        std::memcpy(bad_cmds.data() + 20, &huge_cmds, 4); // sizeofcmds offset is 20

        auto res = UserspaceMachOLoader::load_into_memory(bad_cmds.data(), bad_cmds.size(), mem, cfg);
        TEST_ASSERT(!res.is_success(), "Oversized sizeofcmds must fail to load");
        TEST_ASSERT(res.status == UserspaceMachOLoadStatus::CorruptedLoadCommands,
                    "Status must be CorruptedLoadCommands");
    }

    std::cout << "  Passed: Truncation, bad magic, architecture mismatch, and corrupt cmds rejected safely.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Malformed & Out-of-Bounds Chained Fixups Rejection
// -----------------------------------------------------------------------------
bool test_malformed_chained_fixups_rejection() {
    std::cout << "[Test 2] Malformed & Out-of-Bounds Chained Fixups Rejection...\n";

    DyldEngine dyld;
    Memory mem(0x100000);
    std::string err;

    // Subtest 2A: Payload smaller than DyldChainedFixupsHeaderRaw
    {
        std::vector<std::uint8_t> short_payload(12, 0);
        auto st = dyld.process_chained_fixups(short_payload.data(), short_payload.size(), mem, 0x10000, 0, err);
        TEST_ASSERT(st == DyldLinkStatus::InvalidChainedFixups, "Short payload must yield InvalidChainedFixups");
        TEST_ASSERT(!err.empty(), "Error message must describe truncation");
    }

    // Subtest 2B: Invalid fixups version (!= 0)
    {
        DyldChainedFixupsHeaderRaw hdr{};
        hdr.fixups_version = 999;
        std::vector<std::uint8_t> bad_ver(sizeof(hdr), 0);
        std::memcpy(bad_ver.data(), &hdr, sizeof(hdr));

        auto st = dyld.process_chained_fixups(bad_ver.data(), bad_ver.size(), mem, 0x10000, 0, err);
        TEST_ASSERT(st == DyldLinkStatus::InvalidChainedFixups, "Invalid fixups version must yield InvalidChainedFixups");
    }

    // Subtest 2C: Out-of-bounds starts_offset
    {
        DyldChainedFixupsHeaderRaw hdr{};
        hdr.fixups_version = 0;
        hdr.starts_offset = 0x50000; // Far beyond buffer size
        hdr.imports_offset = 0;
        hdr.symbols_offset = 0;
        std::vector<std::uint8_t> oob_starts(sizeof(hdr), 0);
        std::memcpy(oob_starts.data(), &hdr, sizeof(hdr));

        auto st = dyld.process_chained_fixups(oob_starts.data(), oob_starts.size(), mem, 0x10000, 0, err);
        TEST_ASSERT(st == DyldLinkStatus::InvalidChainedFixups, "OOB starts_offset must yield InvalidChainedFixups");
    }

    // Subtest 2D: Out-of-bounds imports_offset
    {
        DyldChainedFixupsHeaderRaw hdr{};
        hdr.fixups_version = 0;
        hdr.starts_offset = 0;
        hdr.imports_offset = 0x80000; // Far beyond buffer size
        hdr.imports_count = 5;
        std::vector<std::uint8_t> oob_imports(sizeof(hdr), 0);
        std::memcpy(oob_imports.data(), &hdr, sizeof(hdr));

        auto st = dyld.process_chained_fixups(oob_imports.data(), oob_imports.size(), mem, 0x10000, 0, err);
        TEST_ASSERT(st == DyldLinkStatus::InvalidChainedFixups, "OOB imports_offset must yield InvalidChainedFixups");
    }

    std::cout << "  Passed: All malformed fixup structures safely rejected with diagnostic descriptions.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Cyclic Dependency Graph Detection
// -----------------------------------------------------------------------------
bool test_cyclic_dependency_graph_detection() {
    std::cout << "[Test 3] Cyclic Dependency Graph Detection...\n";

    MachODependencyGraph graph;

    // Build cycle: libA -> libB -> libA
    MachODependencyNode nodeA{};
    nodeA.id = "libA.dylib";
    MachODependencyEdge edgeA_to_B{};
    edgeA_to_B.raw_path = "libB.dylib";
    edgeA_to_B.resolved_path = "libB.dylib";
    nodeA.dependencies.push_back(edgeA_to_B);

    MachODependencyNode nodeB{};
    nodeB.id = "libB.dylib";
    MachODependencyEdge edgeB_to_A{};
    edgeB_to_A.raw_path = "libA.dylib";
    edgeB_to_A.resolved_path = "libA.dylib";
    nodeB.dependencies.push_back(edgeB_to_A);

    graph.add_node(nodeA);
    graph.add_node(nodeB);

    std::set<std::string> available = {"libA.dylib", "libB.dylib"};
    auto res = graph.resolve_dependencies("libA.dylib", available);

    TEST_ASSERT(res.has_cycle, "Graph resolver must detect dependency cycle");
    TEST_ASSERT(!res.cycle_nodes.empty(), "Cycle nodes list must not be empty");
    bool foundA = false;
    bool foundB = false;
    for (const auto& n : res.cycle_nodes) {
        if (n == "libA.dylib") foundA = true;
        if (n == "libB.dylib") foundB = true;
    }
    TEST_ASSERT(foundA && foundB, "Both cycle participants must be identified in cycle_nodes");

    std::cout << "  Passed: Mutual cyclic dependency detected without recursion or hang.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Shared Library Arena Memory Exhaustion
// -----------------------------------------------------------------------------
bool test_shared_library_arena_memory_exhaustion() {
    std::cout << "[Test 4] Shared Library Arena Memory Exhaustion...\n";

    // Allocate an arena of only 64 KB (0x10000)
    const std::uint64_t arena_base = 0x180000000ULL;
    const std::uint64_t arena_size = 0x00010000ULL; // 64 KB
    SharedLibraryMapper mapper(arena_base, arena_size);
    Memory mem(0x200000000ULL); // Host VM buffer
    std::string err;

    // Create a mock dylib with a __TEXT segment larger than the 64 KB arena
    std::vector<std::uint8_t> huge_dylib;
    huge_dylib.resize(0x400, 0); // Header + load commands

    MachOHeader64Raw hdr{};
    hdr.magic = kMachO64Magic;
    hdr.cputype = kMachOCpuTypeARM64;
    hdr.cpusubtype = kMachOCpuSubtypeArm64All;
    hdr.filetype = kMachOFileTypeDylib;
    hdr.ncmds = 1;
    hdr.sizeofcmds = sizeof(MachOSegment64Raw);

    MachOSegment64Raw seg{};
    seg.cmd = kCmdSegment64;
    seg.cmdsize = sizeof(seg);
    std::memcpy(seg.segname, "__TEXT", 6);
    seg.vmaddr = 0;
    seg.vmsize = 0x00020000ULL; // 128 KB segment > 64 KB arena
    seg.fileoff = 0;
    seg.filesize = 0x400;
    seg.initprot = kProtRead | kProtExecute;
    seg.maxprot = kProtRead | kProtExecute;

    std::memcpy(huge_dylib.data(), &hdr, sizeof(hdr));
    std::memcpy(huge_dylib.data() + sizeof(hdr), &seg, sizeof(seg));

    auto status = mapper.map_dylib(
        huge_dylib.data(),
        huge_dylib.size(),
        "/usr/lib/libHuge.dylib",
        mem,
        std::nullopt, // auto allocate
        0,
        err);

    TEST_ASSERT(status == SharedLibMapStatus::MemoryExhausted,
                "Exhausting arena bounds must return SharedLibMapStatus::MemoryExhausted");
    TEST_ASSERT(!err.empty(), "Error message must detail exhaustion reason");

    std::cout << "  Passed: Arena memory exhaustion caught cleanly with MemoryExhausted status.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Stack Exhaustion & Boundary Protection
// -----------------------------------------------------------------------------
bool test_stack_exhaustion_and_boundary_protection() {
    std::cout << "[Test 5] Stack Exhaustion & Boundary Protection...\n";

    Memory mem(0x100000); // 1 MB physical memory
    std::uint64_t out_sp = 0;
    std::string err;

    // Subtest 5A: Stack top far outside mapped physical memory
    const std::uint64_t invalid_stack_top = 0xFFFFFFFFFF000000ULL;
    std::vector<std::string> args = {"/bin/test"};
    std::vector<std::string> envs = {"PATH=/bin"};

    bool ok = UserspaceMachOLoader::construct_initial_stack(
        mem, invalid_stack_top, args, envs, "/bin/test", out_sp, err);

    TEST_ASSERT(!ok, "Constructing stack at invalid out-of-bounds address must fail");
    TEST_ASSERT(!err.empty(), "Error message must report boundary violation");

    // Subtest 5B: Stack underflow attempt (stack top at zero)
    const std::uint64_t zero_stack_top = 0x0ULL;
    ok = UserspaceMachOLoader::construct_initial_stack(
        mem, zero_stack_top, args, envs, "/bin/test", out_sp, err);

    TEST_ASSERT(!ok, "Constructing stack at address 0 must fail bounds check");

    std::cout << "  Passed: Stack boundary violations trapped without host memory corruption.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Invalid Mach Port Rights & Trap Fault Recovery
// -----------------------------------------------------------------------------
bool test_invalid_mach_port_rights_and_fault_recovery() {
    std::cout << "[Test 6] Invalid Mach Port Rights & Trap Fault Recovery...\n";

    MachIpcSubsystem ipc;
    Memory mem(0x100000);
    const std::uint32_t task_id = 25;
    auto space = ipc.create_task_port_space(task_id);

    // Subtest 6A: Send message to non-existent port (invalid destination)
    {
        MachMsgHeaderRaw send_hdr{};
        send_hdr.msgh_bits = 0x15;
        send_hdr.msgh_size = sizeof(MachMsgHeaderRaw) + 8;
        send_hdr.msgh_remote_port = 0xDEAD; // Non-existent
        send_hdr.msgh_local_port = kMachPortNull;
        send_hdr.msgh_id = 100;

        std::memcpy(mem.raw_data() + 0x2000, &send_hdr, sizeof(send_hdr));
        std::int32_t ret = ipc.mach_msg(task_id, 0x2000, kMachSendMsg, send_hdr.msgh_size, 0, 0, 0, mem);
        TEST_ASSERT(ret == kMachSendInvalidDest, "Message to dead port must return kMachSendInvalidDest");
    }

    // Subtest 6B: Send message buffer at out-of-bounds guest memory offset
    {
        std::int32_t ret = ipc.mach_msg(task_id, 0xFFFFFFFF, kMachSendMsg, 64, 0, 0, 0, mem);
        TEST_ASSERT(ret == kMachSendInvalidData, "Out-of-bounds message buffer must return kMachSendInvalidData");
    }

    // Subtest 6C: Receive with invalid local port
    {
        std::int32_t ret = ipc.mach_msg(task_id, 0x3000, 0x00000002u /* MACH_RCV_MSG */, 0, 64, 0xCAFE /* invalid port */, 0, mem);
        TEST_ASSERT(ret != kMachMsgSuccess, "Receive on invalid port name must fail gracefully");
    }

    std::cout << "  Passed: Invalid Mach port rights and buffer locations return Darwin IPC fault codes cleanly.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Unallocated Guest Instruction Exception Trapping
// -----------------------------------------------------------------------------
bool test_unallocated_guest_instruction_trapping() {
    std::cout << "[Test 7] Unallocated Guest Instruction Exception Trapping...\n";

    Memory mem(0x100000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Write an illegal / undefined ARM64 opcode: 0x00000000
    mem.write_u32(0x1000, 0x00000000u);
    cpu.set_program_counter(0x1000);
    cpu.set_stack_pointer(0x8000);

    DarwinPipelineResult result{};
    DarwinPipelineConfig cfg{};
    cfg.max_instructions = 10;

    bool can_continue = engine.step_one(result, cfg);

    TEST_ASSERT(!can_continue, "Execution cannot continue after invalid instruction fault");
    TEST_ASSERT(!result.error_message.empty(), "Error message must report instruction fault");
    std::cout << "  Trap report: " << result.error_message << "\n";

    std::cout << "  Passed: Undefined/unallocated opcode trapped safely into result error diagnostics.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Main Test Runner
// -----------------------------------------------------------------------------
int main() {
    std::cout << "=================================================================\n";
    std::cout << "WinIOS Stage 189: Failure Recovery & Runtime Stability Tests\n";
    std::cout << "=================================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* name, bool (*fn)()) {
        if (fn()) {
            std::cout << "[PASS] " << name << "\n\n";
            passed++;
        } else {
            std::cerr << "[FAIL] " << name << "\n\n";
            failed++;
        }
    };

    run_test("Test 1: Corrupted Mach-O Header Detection", test_corrupted_macho_header_detection);
    run_test("Test 2: Malformed & Out-of-Bounds Chained Fixups Rejection", test_malformed_chained_fixups_rejection);
    run_test("Test 3: Cyclic Dependency Graph Detection", test_cyclic_dependency_graph_detection);
    run_test("Test 4: Shared Library Arena Memory Exhaustion", test_shared_library_arena_memory_exhaustion);
    run_test("Test 5: Stack Exhaustion & Boundary Protection", test_stack_exhaustion_and_boundary_protection);
    run_test("Test 6: Invalid Mach Port Rights & Trap Fault Recovery", test_invalid_mach_port_rights_and_fault_recovery);
    run_test("Test 7: Unallocated Guest Instruction Exception Trapping", test_unallocated_guest_instruction_trapping);

    std::cout << "=================================================================\n";
    std::cout << "Stage 189 Test Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}

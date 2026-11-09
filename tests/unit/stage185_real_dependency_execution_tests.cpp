// =============================================================================
// stage185_real_dependency_execution_tests.cpp
//
// Stage 185: Real Dependency and Relocation Test
// Verifies dynamic dependency resolution, shared library arena mapping,
// Darwin runtime library stub generation, dyld chained fixups memory patching,
// two-level namespace symbol binding, and cross-image BLR execution.
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
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_runtime_library.h"
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
// Test 1: Dependency Graph Analysis for Real Mach-O
// -----------------------------------------------------------------------------
bool test_dependency_graph_analysis() {
    std::cout << "[Test 1] Dependency Graph Analysis for Real Mach-O...\n";

    auto bin = MachOCorpusRegistry::create_dynamic_chained_fixup();
    MachODependencyGraph graph;
    std::string err;
    TEST_ASSERT(graph.add_binary(bin.binary_data.data(), bin.binary_data.size(), "main", err),
                "add_binary failed: " + err);

    const auto* node = graph.get_node("main");
    TEST_ASSERT(node != nullptr, "get_node('main') returned null");
    TEST_ASSERT(node->dylinker == "/usr/lib/dyld", "Dylinker mismatch");
    TEST_ASSERT(!node->dependencies.empty(), "Dependencies must not be empty");
    TEST_ASSERT(node->dependencies[0].raw_path == "/usr/lib/libSystem.B.dylib", "Dylib mismatch");

    auto res = graph.resolve_dependencies("main", {"/usr/lib/libSystem.B.dylib", "/usr/lib/dyld"});
    TEST_ASSERT(res.success, "resolve_dependencies failed");
    TEST_ASSERT(!res.load_order.empty(), "Load order must not be empty");

    std::cout << "  Passed (Parsed and ordered dependencies from real Mach-O)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Dynamic Library Arena Allocation & Segment Mapping
// -----------------------------------------------------------------------------
bool test_shared_library_arena_mapping() {
    std::cout << "[Test 2] Shared Library Arena Mapping...\n";

    SharedLibraryMapper mapper(0x0000000180000000ULL, 0x0000000040000000ULL);
    Memory mem(0x200000);

    auto dylib_entry = MachOCorpusRegistry::create_hello_world();
    std::string err;
    auto status = mapper.map_dylib(dylib_entry.binary_data.data(), dylib_entry.binary_data.size(),
                                   "/usr/lib/libSystem.B.dylib", mem, std::nullopt, 0, err);

    TEST_ASSERT(status == SharedLibMapStatus::Success, ("map_dylib failed: " + err).c_str());
    const auto* img = mapper.find_image("/usr/lib/libSystem.B.dylib");
    TEST_ASSERT(img != nullptr, "find_image must locate mapped image");
    TEST_ASSERT(img->base_vaddr >= 0x0000000180000000ULL, "Base address must be in arena");
    TEST_ASSERT(img->install_name == "/usr/lib/libSystem.B.dylib", "Install name mismatch");

    std::cout << "  Passed (Mapped libSystem.B.dylib at 0x" << std::hex << img->base_vaddr
              << std::dec << " in shared arena)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Runtime Library Stub Generation & Registration
// -----------------------------------------------------------------------------
bool test_runtime_library_stub_registration() {
    std::cout << "[Test 3] Runtime Library Stub Generation & Registration...\n";

    DarwinRuntimeLibrary runtime_lib(0x0000000180000000ULL);
    DyldEngine dyld;
    runtime_lib.register_with_dyld(dyld);

    std::vector<std::string> symbols = {
        "_exit", "_write", "_read", "_getpid", "_mach_msg_trap", "_mach_reply_port"
    };

    for (const auto& sym : symbols) {
        auto stub = runtime_lib.find_stub(sym);
        TEST_ASSERT(stub != nullptr, "Missing stub in runtime library: " + sym);
        auto resolved = dyld.resolve_symbol(sym);
        TEST_ASSERT(resolved.has_value() && *resolved == stub->address,
                    "Dyld resolution failed for symbol: " + sym);
    }

    std::cout << "  Passed (Generated and registered 6 critical Darwin runtime stubs)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Real dyld Chained Fixup Memory Patching
// -----------------------------------------------------------------------------
bool test_chained_fixup_memory_patching() {
    std::cout << "[Test 4] Real dyld Chained Fixup Memory Patching...\n";

    Memory mem(0x100000);
    DyldEngine dyld;
    dyld.register_builtin_system_libraries();

    // Setup chained fixup segment pointing to 0x5000 in memory
    std::uint32_t seg_offset = 0x5000;
    DyldFixtureBuilder::SyntheticSegmentFixups seg;
    seg.segment_offset = seg_offset;
    seg.page_size = 0x1000;
    seg.pointer_format = DYLD_CHAINED_PTR_64;
    seg.page_starts = {0x00}; // Starts at offset 0x5000

    auto fixups_blob = DyldFixtureBuilder::create_synthetic_chained_fixups_with_segments(
        {"_exit"}, {seg});

    // Write terminal bind pointer: ordinal 0 (_exit) with next stride 0
    std::uint64_t bind_ptr = 0x8000000000000000ULL;
    mem.write_u64(seg_offset, bind_ptr);

    std::string err;
    auto status = dyld.process_chained_fixups(fixups_blob.data(), fixups_blob.size(),
                                             mem, 0x100000000ULL, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, ("process_chained_fixups failed: " + err).c_str());

    std::uint64_t patched = mem.read_u64(seg_offset);
    TEST_ASSERT(patched != 0 && patched != bind_ptr, "Pointer must be patched with resolved symbol");

    std::cout << "  Passed (Patched chained bind pointer to 0x" << std::hex << patched << std::dec << ")\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Cross-Image Execution via BLR to libSystem.B.dylib Stub
// -----------------------------------------------------------------------------
bool test_cross_image_blr_execution() {
    std::cout << "[Test 5] Cross-Image Execution via BLR to libSystem.B.dylib Stub...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // 1. Stage DarwinRuntimeLibrary stubs into memory
    DarwinRuntimeLibrary runtime_lib(0x0000000180000000ULL);
    std::string stage_err;
    TEST_ASSERT(runtime_lib.stage_into_memory(mem, stage_err), "stage_into_memory failed");

    auto exit_stub = runtime_lib.find_stub("_exit");
    TEST_ASSERT(exit_stub != nullptr, "_exit stub must exist");

    std::uint64_t exit_vaddr = exit_stub->address;
    std::uint64_t target_exit = (exit_vaddr < mem.size()) ? exit_vaddr : (exit_vaddr % mem.size());

    // 2. Stage main binary code at 0x1000 that calls BLR X16:
    // mov x0, #42
    // ldr x16, [pc, #8]
    // blr x16
    // <64-bit target_exit>
    mem.write_u32(0x1000, 0xD2800540u); // mov x0, #42
    mem.write_u32(0x1004, 0x58000050u); // ldr x16, pc+8 (at 0x100C)
    mem.write_u32(0x1008, 0xD63F0200u); // blr x16
    mem.write_u64(0x100C, target_exit); // Target exit stub address

    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);
    cpu.set_stack_pointer(0x80000);

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 10;
    DarwinPipelineResult result;

    // Step 1: mov x0, #42
    TEST_ASSERT(engine.step_one(result, cfg), "Step 1 failed");
    TEST_ASSERT(cpu.read_register(0) == 42, "X0 must be 42");

    // Step 2: ldr x16, [pc, #8]
    TEST_ASSERT(engine.step_one(result, cfg), "Step 2 failed");
    TEST_ASSERT(cpu.read_register(16) == target_exit, "X16 must hold target stub address");

    // Step 3: blr x16 -> jumps to target_exit (the _exit stub)
    TEST_ASSERT(engine.step_one(result, cfg), "Step 3 (blr x16) failed");
    TEST_ASSERT(cpu.get_program_counter() == target_exit, "PC must be at target exit stub");

    // Step 4: First instruction of _exit stub (mov x16, #1)
    TEST_ASSERT(engine.step_one(result, cfg), "Step 4 failed");
    TEST_ASSERT(cpu.read_register(16) == 1, "X16 must be 1 (SYS_exit)");

    // Step 5: Second instruction of _exit stub (svc #0x80)
    TEST_ASSERT(!engine.step_one(result, cfg), "Step 5 (svc #0x80 SYS_exit) must terminate process");
    TEST_ASSERT(result.process_exited, "Process must exit");
    TEST_ASSERT(result.exit_code == 42, "Exit code must be 42");

    std::cout << "  Passed (Executed cross-image BLR jump into libSystem.B.dylib _exit stub)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Two-Level Namespace Symbol Resolution & Weak Fallback
// -----------------------------------------------------------------------------
bool test_two_level_namespace_resolution() {
    std::cout << "[Test 6] Two-Level Namespace Symbol Resolution & Weak Fallback...\n";

    DyldEngine dyld;
    dyld.register_builtin_system_libraries();

    // 1. Resolve _write in library ordinal 1 (/usr/lib/libSystem.B.dylib)
    std::optional<std::uint64_t> out_addr;
    std::string err;
    auto status = dyld.resolve_symbol_two_level("_write", 1, false, out_addr, err);
    TEST_ASSERT(status == DyldLinkStatus::Success && out_addr.has_value() && *out_addr != 0,
                "Two-level resolve _write failed");

    // 2. Weak import fallback for nonexistent symbol (must succeed with address 0)
    out_addr.reset();
    status = dyld.resolve_symbol_two_level("_nonexistent_weak_symbol", 1, true, out_addr, err);
    TEST_ASSERT(status == DyldLinkStatus::Success && out_addr.has_value() && *out_addr == 0,
                "Weak import must resolve to 0");

    // 3. Strong import of nonexistent symbol must report UndefinedSymbol
    out_addr.reset();
    status = dyld.resolve_symbol_two_level("_nonexistent_strong_symbol", 1, false, out_addr, err);
    TEST_ASSERT(status == DyldLinkStatus::UndefinedSymbol && !out_addr.has_value(),
                "Strong missing symbol must report UndefinedSymbol");

    std::cout << "  Passed (Validated two-level ordinals and weak import fallback)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Unmapping and Dynamic Dependency Lifecycle
// -----------------------------------------------------------------------------
bool test_dependency_unmapping_lifecycle() {
    std::cout << "[Test 7] Unmapping and Dynamic Dependency Lifecycle...\n";

    SharedLibraryMapper mapper(0x0000000180000000ULL, 0x0000000040000000ULL);
    Memory mem(0x200000);

    auto dylib_entry = MachOCorpusRegistry::create_hello_world();
    std::string err;
    auto status = mapper.map_dylib(dylib_entry.binary_data.data(), dylib_entry.binary_data.size(),
                                   "/usr/lib/libAux.dylib", mem, std::nullopt, 0, err);
    TEST_ASSERT(status == SharedLibMapStatus::Success, "Initial map failed");
    TEST_ASSERT(mapper.find_image("/usr/lib/libAux.dylib") != nullptr, "Library must be found");

    // Unmap
    auto unmap_st = mapper.unmap_image("/usr/lib/libAux.dylib", mem, err);
    TEST_ASSERT(unmap_st == SharedLibMapStatus::Success, "unmap_image failed: " + err);
    TEST_ASSERT(mapper.find_image("/usr/lib/libAux.dylib") == nullptr, "Library must be cleared from table");

    std::cout << "  Passed (Verified dynamic library unmapping and resource reclamation)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 185: REAL DEPENDENCY AND RELOCATION TEST SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_dependency_graph_analysis()) passed++; else failed++;
    if (test_shared_library_arena_mapping()) passed++; else failed++;
    if (test_runtime_library_stub_registration()) passed++; else failed++;
    if (test_chained_fixup_memory_patching()) passed++; else failed++;
    if (test_cross_image_blr_execution()) passed++; else failed++;
    if (test_two_level_namespace_resolution()) passed++; else failed++;
    if (test_dependency_unmapping_lifecycle()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 185 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}
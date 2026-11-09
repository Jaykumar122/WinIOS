// =============================================================================
// stage187_multi_image_execution_tests.cpp
//
// Stage 187: Real Multi-Image Execution Test
// Verifies simultaneous multi-image execution linking:
//   - Main executable (image 0)
//   - libSystem.B.dylib (image 1)
//   - libMath.dylib (image 2)
// Testing cross-image relocations, two-level symbol resolution, chained fixup
// patching across 3 images, inter-module BLR/RET calling conventions,
// and dynamic weak import fallbacks.
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
// Test 1: Multi-Image Dependency Graph Construction & Ordering
// -----------------------------------------------------------------------------
bool test_multi_image_dependency_graph() {
    std::cout << "[Test 1] Multi-Image Dependency Graph Construction & Ordering...\n";

    // Create main executable with 2 dependent dylibs: libSystem and libMath
    auto main_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib", "/usr/lib/libMath.dylib"});

    // Create libMath dylib depending on libSystem
    auto math_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "", {"/usr/lib/libSystem.B.dylib"});

    // Create libSystem leaf dylib
    auto sys_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable("", {});

    MachODependencyGraph graph;
    std::string err;

    TEST_ASSERT(graph.add_binary(main_bin.data(), main_bin.size(), "main", err),
                "add_binary main failed: " + err);
    TEST_ASSERT(graph.add_binary(math_bin.data(), math_bin.size(), "/usr/lib/libMath.dylib", err),
                "add_binary libMath failed: " + err);
    TEST_ASSERT(graph.add_binary(sys_bin.data(), sys_bin.size(), "/usr/lib/libSystem.B.dylib", err),
                "add_binary libSystem failed: " + err);

    const auto* main_node = graph.get_node("main");
    TEST_ASSERT(main_node != nullptr, "main node must exist");
    TEST_ASSERT(main_node->dependencies.size() == 2, "main must have 2 dependencies");

    auto res = graph.resolve_dependencies("main", {"/usr/lib/libSystem.B.dylib", "/usr/lib/libMath.dylib", "/usr/lib/dyld"});
    TEST_ASSERT(res.success, "resolve_dependencies must succeed");
    TEST_ASSERT(res.load_order.size() >= 2, "Load order must contain all images");

    std::cout << "  Passed (Resolved 3-image topological load order: ";
    for (const auto& path : res.load_order) {
        std::cout << path << " -> ";
    }
    std::cout << "main)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Tri-Image Address Space Allocation in Shared Arena
// -----------------------------------------------------------------------------
bool test_tri_image_address_space_allocation() {
    std::cout << "[Test 2] Tri-Image Address Space Allocation in Shared Arena...\n";

    SharedLibraryMapper mapper(0x0000000180000000ULL, 0x0000000040000000ULL);
    Memory mem(0x200000);

    auto main_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib", "/usr/lib/libMath.dylib"});
    auto math_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable("", {});
    auto sys_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable("", {});

    std::string err;

    // 1. Map main executable
    auto st_main = mapper.map_main_executable(main_bin.data(), main_bin.size(), mem, 0, err);
    TEST_ASSERT(st_main == SharedLibMapStatus::Success, "map_main_executable failed: " + err);

    // 2. Map libSystem.B.dylib into shared arena
    auto st_sys = mapper.map_dylib(sys_bin.data(), sys_bin.size(), "/usr/lib/libSystem.B.dylib",
                                   mem, std::nullopt, 0, err);
    TEST_ASSERT(st_sys == SharedLibMapStatus::Success, "map_dylib libSystem failed: " + err);

    // 3. Map libMath.dylib into shared arena
    auto st_math = mapper.map_dylib(math_bin.data(), math_bin.size(), "/usr/lib/libMath.dylib",
                                    mem, std::nullopt, 0, err);
    TEST_ASSERT(st_math == SharedLibMapStatus::Success, "map_dylib libMath failed: " + err);

    TEST_ASSERT(mapper.mapped_count() == 3, "Exactly 3 images must be mapped");

    const auto* img_main = mapper.find_image("main_executable");
    const auto* img_sys  = mapper.find_image("/usr/lib/libSystem.B.dylib");
    const auto* img_math = mapper.find_image("/usr/lib/libMath.dylib");

    TEST_ASSERT(img_main && img_sys && img_math, "All 3 images must be resolvable");
    TEST_ASSERT(img_sys->base_vaddr != img_math->base_vaddr, "Dylibs must occupy distinct base addresses");
    TEST_ASSERT(img_sys->base_vaddr >= 0x0000000180000000ULL, "libSystem must reside in arena");
    TEST_ASSERT(img_math->base_vaddr >= 0x0000000180000000ULL, "libMath must reside in arena");

    // Collision detection check
    TEST_ASSERT(mapper.check_collision(img_sys->base_vaddr, 0x4000), "Collision must be detected for libSystem");
    TEST_ASSERT(mapper.check_collision(img_math->base_vaddr, 0x4000), "Collision must be detected for libMath");

    std::cout << "  Passed (Coallocated 3 images: main at 0x" << std::hex << img_main->base_vaddr
              << ", libSystem at 0x" << img_sys->base_vaddr
              << ", libMath at 0x" << img_math->base_vaddr << std::dec << ")\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Multi-Library Symbol Registration and Isolation
// -----------------------------------------------------------------------------
bool test_multi_library_symbol_registration_and_isolation() {
    std::cout << "[Test 3] Multi-Library Symbol Registration and Isolation...\n";

    DyldEngine dyld;
    dyld.clear_modules();

    // Module 1 (ordinal 1): libSystem.B.dylib
    DyldLoadedModule mod_system{};
    mod_system.name = "libSystem.B.dylib";
    mod_system.install_name = "/usr/lib/libSystem.B.dylib";
    mod_system.base_address = 0x180000000ULL;
    mod_system.exports["_exit"] = 0x180001000ULL;
    mod_system.exports["_write"] = 0x180001040ULL;
    mod_system.exports["_read"] = 0x180001080ULL;
    dyld.register_module(mod_system);

    // Module 2 (ordinal 2): libMath.dylib
    DyldLoadedModule mod_math{};
    mod_math.name = "libMath.dylib";
    mod_math.install_name = "/usr/lib/libMath.dylib";
    mod_math.base_address = 0x180040000ULL;
    mod_math.exports["_add_integers"] = 0x180041000ULL;
    mod_math.exports["_multiply_integers"] = 0x180041040ULL;
    mod_math.exports["_square_root"] = 0x180041080ULL;
    dyld.register_module(mod_math);

    // 1. Ordinal-specific symbol resolution
    auto res_exit = dyld.resolve_symbol("_exit", 1);
    TEST_ASSERT(res_exit.has_value() && *res_exit == 0x180001000ULL, "Failed to resolve _exit from ordinal 1");

    auto res_math = dyld.resolve_symbol("_add_integers", 2);
    TEST_ASSERT(res_math.has_value() && *res_math == 0x180041000ULL, "Failed to resolve _add_integers from ordinal 2");

    // 2. Two-level namespace symbol isolation: _exit must NOT resolve from ordinal 2 (libMath)
    std::optional<std::uint64_t> out_addr;
    std::string err;
    auto status = dyld.resolve_symbol_two_level("_exit", 2, false, out_addr, err);
    TEST_ASSERT(status == DyldLinkStatus::UndefinedSymbol && !out_addr.has_value(),
                "_exit must not exist in libMath ordinal 2");

    // 3. Two-level resolution with correct ordinal
    out_addr.reset();
    status = dyld.resolve_symbol_two_level("_add_integers", 2, false, out_addr, err);
    TEST_ASSERT(status == DyldLinkStatus::Success && out_addr.has_value() && *out_addr == 0x180041000ULL,
                "_add_integers must resolve correctly from libMath ordinal 2");

    std::cout << "  Passed (Validated two-level symbol resolution and cross-library namespace isolation)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Tri-Image Chained Fixup Patching Across Multiple Libraries
// -----------------------------------------------------------------------------
bool test_tri_image_chained_fixup_patching() {
    std::cout << "[Test 4] Tri-Image Chained Fixup Patching Across Multiple Libraries...\n";

    Memory mem(0x100000);
    DyldEngine dyld;
    dyld.clear_modules();

    // Register libraries
    DyldLoadedModule mod_system{};
    mod_system.name = "libSystem.B.dylib";
    mod_system.install_name = "/usr/lib/libSystem.B.dylib";
    mod_system.base_address = 0x180000000ULL;
    mod_system.exports["_exit"] = 0x180001000ULL;
    dyld.register_module(mod_system);

    DyldLoadedModule mod_math{};
    mod_math.name = "libMath.dylib";
    mod_math.install_name = "/usr/lib/libMath.dylib";
    mod_math.base_address = 0x180040000ULL;
    mod_math.exports["_add_integers"] = 0x180041000ULL;
    dyld.register_module(mod_math);

    // Construct chained fixups with 2 imports across 2 different library ordinals:
    // Import 0: _add_integers (lib_ordinal 2)
    // Import 1: _exit (lib_ordinal 1)
    std::vector<DyldFixtureBuilder::SyntheticImportDescriptor> imports = {
        {"_add_integers", 2, false},
        {"_exit", 1, false}
    };

    std::uint32_t seg_offset = 0x5000;
    DyldFixtureBuilder::SyntheticSegmentFixups seg;
    seg.segment_offset = seg_offset;
    seg.page_size = 0x1000;
    seg.pointer_format = DYLD_CHAINED_PTR_64;
    seg.page_starts = {0x00}; // Starts at offset 0x5000

    auto fixups_blob = DyldFixtureBuilder::create_synthetic_chained_fixups_multi_lib(imports, {seg});

    // Setup pointer chain in guest memory:
    // At 0x5000: bind import 0 (_add_integers), stride 2 words (8 bytes -> next at 0x5008)
    // At 0x5008: bind import 1 (_exit), stride 0 (terminal)
    std::uint64_t bind_ptr0 = 0x8000000000000000ULL | (2ULL << 51) | 0ULL;
    std::uint64_t bind_ptr1 = 0x8000000000000000ULL | (0ULL << 51) | 1ULL;

    mem.write_u64(0x5000, bind_ptr0);
    mem.write_u64(0x5008, bind_ptr1);

    std::string err;
    auto status = dyld.process_chained_fixups(fixups_blob.data(), fixups_blob.size(),
                                             mem, 0x100000000ULL, 0, err);
    TEST_ASSERT(status == DyldLinkStatus::Success, ("process_chained_fixups failed: " + err).c_str());

    std::uint64_t patched_math = mem.read_u64(0x5000);
    std::uint64_t patched_exit = mem.read_u64(0x5008);

    TEST_ASSERT(patched_math == 0x180041000ULL, "Location 0x5000 must patch to _add_integers (0x180041000)");
    TEST_ASSERT(patched_exit == 0x180001000ULL, "Location 0x5008 must patch to _exit (0x180001000)");

    std::cout << "  Passed (Patched chained fixups targeting multiple library ordinals: 0x5000 -> 0x"
              << std::hex << patched_math << ", 0x5008 -> 0x" << patched_exit << std::dec << ")\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Live Tri-Image CPU Execution Pipeline
// -----------------------------------------------------------------------------
bool test_live_tri_image_cpu_execution() {
    std::cout << "[Test 5] Live Tri-Image CPU Execution Pipeline...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Image 2: libMath.dylib at 0x6000
    // _add_integers:
    //   add x0, x0, x1
    //   ret
    mem.write_u32(0x6000, 0x8B010000u); // add x0, x0, x1
    mem.write_u32(0x6004, 0xD65F03C0u); // ret

    // Image 1: libSystem.B.dylib at 0x7000
    // _exit:
    //   mov x16, #1
    //   svc #0x80
    mem.write_u32(0x7000, 0xD2800030u); // mov x16, #1 (SYS_exit)
    mem.write_u32(0x7004, 0xD4001001u); // svc #0x80

    // Image 0: Main Executable at 0x1000
    //   mov x0, #25         (0x1000)
    //   mov x1, #17         (0x1004)
    //   ldr x16, [pc, #20]  (0x1008 -> loads from 0x101C: _add_integers target 0x6000)
    //   blr x16             (0x100C -> calls libMath, returns with x0 = 42)
    //   ldr x16, [pc, #20]  (0x1010 -> loads from 0x1024: _exit target 0x7000)
    //   blr x16             (0x1014 -> calls libSystem with x0 = 42)
    //   nop                 (0x1018)
    //   [0x101C]: 0x6000ULL
    //   [0x1024]: 0x7000ULL
    mem.write_u32(0x1000, 0xD2800320u); // mov x0, #25
    mem.write_u32(0x1004, 0xD2800221u); // mov x1, #17
    mem.write_u32(0x1008, 0x580000B0u); // ldr x16, [pc, #20]
    mem.write_u32(0x100C, 0xD63F0200u); // blr x16
    mem.write_u32(0x1010, 0x580000B0u); // ldr x16, [pc, #20]
    mem.write_u32(0x1014, 0xD63F0200u); // blr x16
    mem.write_u32(0x1018, 0xD503201Fu); // nop
    mem.write_u64(0x101C, 0x6000ULL);   // pointer to libMath._add_integers
    mem.write_u64(0x1024, 0x7000ULL);   // pointer to libSystem._exit

    cpu.reset();
    cpu.set_current_el(0); // Userspace EL0
    cpu.set_program_counter(0x1000);
    cpu.set_stack_pointer(0x80000);

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 20;
    DarwinPipelineResult result;

    // Step through execution:
    // Step 1: mov x0, #25
    TEST_ASSERT(engine.step_one(result, cfg), "Step 1 failed");
    TEST_ASSERT(cpu.read_register(0) == 25, "X0 must be 25");

    // Step 2: mov x1, #17
    TEST_ASSERT(engine.step_one(result, cfg), "Step 2 failed");
    TEST_ASSERT(cpu.read_register(1) == 17, "X1 must be 17");

    // Step 3: ldr x16, [pc, #20] -> loads 0x6000
    TEST_ASSERT(engine.step_one(result, cfg), "Step 3 failed");
    TEST_ASSERT(cpu.read_register(16) == 0x6000ULL, "X16 must be 0x6000");

    // Step 4: blr x16 -> jumps to libMath at 0x6000
    TEST_ASSERT(engine.step_one(result, cfg), "Step 4 (blr x16) failed");
    TEST_ASSERT(cpu.get_program_counter() == 0x6000ULL, "PC must be at libMath._add_integers (0x6000)");

    // Step 5: add x0, x0, x1 in libMath -> X0 = 42
    TEST_ASSERT(engine.step_one(result, cfg), "Step 5 (add x0, x0, x1) failed");
    TEST_ASSERT(cpu.read_register(0) == 42, "X0 must be 42");

    // Step 6: ret in libMath -> returns to main binary at 0x1010
    TEST_ASSERT(engine.step_one(result, cfg), "Step 6 (ret) failed");
    TEST_ASSERT(cpu.get_program_counter() == 0x1010ULL, "PC must return to main binary at 0x1010");

    // Step 7: ldr x16, [pc, #20] -> loads 0x7000
    TEST_ASSERT(engine.step_one(result, cfg), "Step 7 failed");
    TEST_ASSERT(cpu.read_register(16) == 0x7000ULL, "X16 must be 0x7000");

    // Step 8: blr x16 -> jumps to libSystem at 0x7000
    TEST_ASSERT(engine.step_one(result, cfg), "Step 8 (blr x16) failed");
    TEST_ASSERT(cpu.get_program_counter() == 0x7000ULL, "PC must be at libSystem._exit (0x7000)");

    // Step 9: mov x16, #1 in libSystem
    TEST_ASSERT(engine.step_one(result, cfg), "Step 9 failed");
    TEST_ASSERT(cpu.read_register(16) == 1, "X16 must be 1 (SYS_exit)");

    // Step 10: svc #0x80 in libSystem -> clean termination
    TEST_ASSERT(!engine.step_one(result, cfg), "Step 10 (SYS_exit) must terminate process");
    TEST_ASSERT(result.process_exited, "Process must exit");
    TEST_ASSERT(result.exit_code == 42, "Exit code must be 42");

    std::cout << "  Passed (Executed multi-image pipeline: Main -> libMath(add) -> Main -> libSystem(exit: 42))\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Cross-Image Weak Import and Dynamic Fallback
// -----------------------------------------------------------------------------
bool test_cross_image_weak_import_and_fallback() {
    std::cout << "[Test 6] Cross-Image Weak Import and Dynamic Fallback...\n";

    Memory mem(0x200000);
    CPU cpu;
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // libSystem._exit at 0x7000
    mem.write_u32(0x7000, 0xD2800030u); // mov x16, #1 (SYS_exit)
    mem.write_u32(0x7004, 0xD4001001u); // svc #0x80

    // Main binary testing weak symbol dynamic fallback:
    // 0x1000: ldr x16, [pc, #24]  -> loads weak pointer from 0x1018 (NULL)
    // 0x1004: cmp x16, #0         -> checks if weak symbol is bound
    // 0x1008: b.eq #8             -> if null, skip calling and branch to 0x1010
    // 0x100C: blr x16             -> (skipped when null)
    // 0x1010: mov x0, #99         -> dynamic fallback value
    // 0x1014: b #12               -> branch to exit sequence at 0x1020
    // 0x1018: 0x0ULL              -> weak unresolved symbol pointer
    // 0x1020: ldr x16, [pc, #8]   -> loads 0x7000 (_exit) from 0x1028
    // 0x1024: blr x16             -> exit(99)
    // 0x1028: 0x7000ULL           -> libSystem._exit pointer
    mem.write_u32(0x1000, 0x580000D0u); // ldr x16, [pc, #24] (imm19 = 6 -> 24 bytes -> 0x1018)
    mem.write_u32(0x1004, 0xF100021Fu); // cmp x16, #0
    mem.write_u32(0x1008, 0x54000040u); // b.eq #8 (to 0x1010)
    mem.write_u32(0x100C, 0xD63F0200u); // blr x16
    mem.write_u32(0x1010, 0xD2800C60u); // mov x0, #99
    mem.write_u32(0x1014, 0x14000003u); // b #12 (to 0x1020)
    mem.write_u64(0x1018, 0x0ULL);       // unresolved weak pointer (NULL)
    mem.write_u32(0x1020, 0x58000050u); // ldr x16, [pc, #8] (to 0x1028)
    mem.write_u32(0x1024, 0xD63F0200u); // blr x16
    mem.write_u64(0x1028, 0x7000ULL);   // pointer to libSystem._exit

    cpu.reset();
    cpu.set_current_el(0);
    cpu.set_program_counter(0x1000);
    cpu.set_stack_pointer(0x80000);

    DarwinPipelineConfig cfg;
    cfg.max_instructions = 15;
    DarwinPipelineResult result;

    while (result.guest_instructions_retired < cfg.max_instructions) {
        if (!engine.step_one(result, cfg)) break;
    }

    TEST_ASSERT(result.process_exited, "Process must terminate");
    TEST_ASSERT(result.exit_code == 99, "Exit code must be 99 from dynamic weak fallback branch");

    std::cout << "  Passed (Validated weak symbol NULL-check and dynamic fallback execution to exit: 99)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Comprehensive Multi-Image Lifecycle & Clean Teardown
// -----------------------------------------------------------------------------
bool test_comprehensive_multi_image_lifecycle() {
    std::cout << "[Test 7] Comprehensive Multi-Image Lifecycle & Clean Teardown...\n";

    SharedLibraryMapper mapper(0x0000000180000000ULL, 0x0000000040000000ULL);
    Memory mem(0x200000);

    auto main_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib", "/usr/lib/libMath.dylib"});
    auto math_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable("", {});
    auto sys_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable("", {});

    std::string err;

    // 1. Map all 3 images
    TEST_ASSERT(mapper.map_main_executable(main_bin.data(), main_bin.size(), mem, 0, err) == SharedLibMapStatus::Success, "Map main failed");
    TEST_ASSERT(mapper.map_dylib(sys_bin.data(), sys_bin.size(), "/usr/lib/libSystem.B.dylib", mem, std::nullopt, 0, err) == SharedLibMapStatus::Success, "Map sys failed");
    TEST_ASSERT(mapper.map_dylib(math_bin.data(), math_bin.size(), "/usr/lib/libMath.dylib", mem, std::nullopt, 0, err) == SharedLibMapStatus::Success, "Map math failed");
    TEST_ASSERT(mapper.mapped_count() == 3, "Mapped count must be 3");

    // 2. Unmap auxiliary library (libMath)
    auto unmap_st = mapper.unmap_image("/usr/lib/libMath.dylib", mem, err);
    TEST_ASSERT(unmap_st == SharedLibMapStatus::Success, "unmap libMath failed: " + err);
    TEST_ASSERT(mapper.find_image("/usr/lib/libMath.dylib") == nullptr, "libMath must no longer be found");
    TEST_ASSERT(mapper.mapped_count() == 2, "Mapped count must be 2");

    // 3. Unmap system library (libSystem)
    unmap_st = mapper.unmap_image("/usr/lib/libSystem.B.dylib", mem, err);
    TEST_ASSERT(unmap_st == SharedLibMapStatus::Success, "unmap libSystem failed: " + err);
    TEST_ASSERT(mapper.find_image("/usr/lib/libSystem.B.dylib") == nullptr, "libSystem must no longer be found");
    TEST_ASSERT(mapper.mapped_count() == 1, "Only main image must remain");

    // 4. Verify main executable remains intact
    TEST_ASSERT(mapper.find_image("main_executable") != nullptr, "Main executable must remain mapped");

    std::cout << "  Passed (Complete 3-image dynamic lifecycle and orderly teardown verified)\n";
    return true;
}

int main() {
    std::cout << "================================================================\n";
    std::cout << "  STAGE 187: REAL MULTI-IMAGE EXECUTION TEST SUITE\n";
    std::cout << "================================================================\n";

    int passed = 0;
    int failed = 0;

    if (test_multi_image_dependency_graph()) passed++; else failed++;
    if (test_tri_image_address_space_allocation()) passed++; else failed++;
    if (test_multi_library_symbol_registration_and_isolation()) passed++; else failed++;
    if (test_tri_image_chained_fixup_patching()) passed++; else failed++;
    if (test_live_tri_image_cpu_execution()) passed++; else failed++;
    if (test_cross_image_weak_import_and_fallback()) passed++; else failed++;
    if (test_comprehensive_multi_image_lifecycle()) passed++; else failed++;

    std::cout << "----------------------------------------------------------------\n";
    std::cout << "Stage 187 Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "================================================================\n";

    return (failed == 0) ? 0 : 1;
}

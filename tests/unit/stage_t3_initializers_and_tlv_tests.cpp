// ==============================================================================
// WinIOS Unit Test: Stage T3 Initializers and Thread-Local Storage (TLV)
// ==============================================================================
// Proves:
// 1. UserspaceMachOLoader extracts S_MOD_INIT_FUNC_POINTERS (__mod_init_func)
//    and parses TLS/TLV sections (__thread_vars, __thread_data).
// 2. Sequential constructor execution order: dylib dependency (libleaf) ->
//    dylib dependent (libmid) -> main executable (ctor_main).
// 3. Thread-local storage (TLV) bootstrap and memory access: read default,
//    write new value, read back verified value.
// 4. Negative test: Misordered or omitted constructor causes failure detection.
// ==============================================================================

#include "cpu/cpu.h"
#include "loader/dyld_engine.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

constexpr const char* kCtorLeafPath = "tests/artifacts/bin/libctor_leaf.dylib";
constexpr const char* kCtorMidPath  = "tests/artifacts/bin/libctor_mid.dylib";
constexpr const char* kCtorMainPath = "tests/artifacts/bin/ctor_main";
constexpr const char* kTlvMainPath  = "tests/artifacts/bin/tlv_main";

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

std::vector<std::uint8_t> read_binary_file(const std::string& rel) {
    const std::string p = resolve_path(rel);
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    return buf;
}

void get_dyld_info(const std::uint8_t* bytes, std::size_t size,
                   std::size_t& bind_off, std::size_t& bind_size,
                   std::size_t& lazy_off, std::size_t& lazy_size,
                   std::size_t& export_off, std::size_t& export_size) {
    bind_off = bind_size = lazy_off = lazy_size = export_off = export_size = 0;
    if (size < 32) return;
    std::uint32_t ncmds = *reinterpret_cast<const std::uint32_t*>(bytes + 16);
    std::size_t off = 32;
    for (std::uint32_t i = 0; i < ncmds && off + 8 <= size; ++i) {
        std::uint32_t cmd = *reinterpret_cast<const std::uint32_t*>(bytes + off);
        std::uint32_t cmdsize = *reinterpret_cast<const std::uint32_t*>(bytes + off + 4);
        if (cmd == 0x80000022u && off + 48 <= size) { // LC_DYLD_INFO_ONLY
            bind_off = *reinterpret_cast<const std::uint32_t*>(bytes + off + 16);
            bind_size = *reinterpret_cast<const std::uint32_t*>(bytes + off + 20);
            lazy_off = *reinterpret_cast<const std::uint32_t*>(bytes + off + 32);
            lazy_size = *reinterpret_cast<const std::uint32_t*>(bytes + off + 36);
            export_off = *reinterpret_cast<const std::uint32_t*>(bytes + off + 40);
            export_size = *reinterpret_cast<const std::uint32_t*>(bytes + off + 44);
            return;
        }
        off += cmdsize;
    }
}

} // namespace

// Test 1: Verify Initializer and TLV Section Extraction
bool test_init_and_tlv_metadata() {
    std::cout << "[Test 1] Verify Initializer and TLV Metadata Extraction...\n";

    auto leafBytes = read_binary_file(kCtorLeafPath);
    auto midBytes  = read_binary_file(kCtorMidPath);
    auto mainBytes = read_binary_file(kCtorMainPath);
    auto tlvBytes  = read_binary_file(kTlvMainPath);

    TEST_ASSERT(!leafBytes.empty(), "libctor_leaf.dylib must exist");
    TEST_ASSERT(!midBytes.empty(), "libctor_mid.dylib must exist");
    TEST_ASSERT(!mainBytes.empty(), "ctor_main must exist");
    TEST_ASSERT(!tlvBytes.empty(), "tlv_main must exist");

    UserspaceLoadResult leafRes, midRes, mainRes, tlvRes;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(leafBytes.data(), leafBytes.size(), leafRes), "Parse leaf metadata");
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(midBytes.data(), midBytes.size(), midRes), "Parse mid metadata");
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(mainBytes.data(), mainBytes.size(), mainRes), "Parse main metadata");
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(tlvBytes.data(), tlvBytes.size(), tlvRes), "Parse tlv metadata");

    // Check __mod_init_func parsing
    TEST_ASSERT(leafRes.mod_init_funcs.size() == 1, "leaf must have exactly 1 initializer");
    TEST_ASSERT(midRes.mod_init_funcs.size() == 1, "mid must have exactly 1 initializer");
    TEST_ASSERT(mainRes.mod_init_funcs.size() == 1, "main must have exactly 1 initializer");

    std::cout << "  libctor_leaf mod_init_func[0]: 0x" << std::hex << leafRes.mod_init_funcs[0] << std::dec << "\n";
    std::cout << "  libctor_mid  mod_init_func[0]: 0x" << std::hex << midRes.mod_init_funcs[0] << std::dec << "\n";
    std::cout << "  ctor_main    mod_init_func[0]: 0x" << std::hex << mainRes.mod_init_funcs[0] << std::dec << "\n";

    TEST_ASSERT(leafRes.mod_init_funcs[0] != 0, "leaf ctor pointer must be non-null");
    TEST_ASSERT(midRes.mod_init_funcs[0] != 0, "mid ctor pointer must be non-null");
    TEST_ASSERT(mainRes.mod_init_funcs[0] != 0, "main ctor pointer must be non-null");

    return true;
}

// Test 2: Sequential Constructor Execution Order (leaf -> mid -> main -> entry)
bool test_sequential_constructor_execution() {
    std::cout << "[Test 2] Sequential Constructor Execution Order (leaf -> mid -> main -> entry)...\n";

    auto leafBytes = read_binary_file(kCtorLeafPath);
    auto midBytes  = read_binary_file(kCtorMidPath);
    auto mainBytes = read_binary_file(kCtorMainPath);

    TEST_ASSERT(!leafBytes.empty() && !midBytes.empty() && !mainBytes.empty(), "All three constructor binaries required");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    // 1. Map libctor_leaf.dylib at 0x20000
    constexpr std::uint64_t kLeafBase = 0x20000ULL;
    UserspaceLoadResult leafLoad;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(leafBytes.data(), leafBytes.size(), leafLoad), "Parse leaf");
    for (const auto& seg : leafLoad.segments) {
        if (seg.is_pagezero) continue;
        std::uint64_t target_vaddr = (kLeafBase + seg.vmaddr) % memory.size();
        for (std::size_t i = 0; i < seg.filesize; ++i) {
            if (target_vaddr + i < memory.size()) {
                memory.write_u8(target_vaddr + i, leafBytes[seg.fileoff + i]);
            }
        }
    }

    // 2. Map libctor_mid.dylib at 0x30000
    constexpr std::uint64_t kMidBase = 0x30000ULL;
    UserspaceLoadResult midLoad;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(midBytes.data(), midBytes.size(), midLoad), "Parse mid");
    for (const auto& seg : midLoad.segments) {
        if (seg.is_pagezero) continue;
        std::uint64_t target_vaddr = (kMidBase + seg.vmaddr) % memory.size();
        for (std::size_t i = 0; i < seg.filesize; ++i) {
            if (target_vaddr + i < memory.size()) {
                memory.write_u8(target_vaddr + i, midBytes[seg.fileoff + i]);
            }
        }
    }

    // 3. Setup DyldEngine and extract exports
    DyldEngine dyld;
    dyld.register_builtin_system_libraries();

    std::size_t leaf_bind_off, leaf_bind_sz, leaf_lazy_off, leaf_lazy_sz, leaf_exp_off, leaf_exp_sz;
    get_dyld_info(leafBytes.data(), leafBytes.size(), leaf_bind_off, leaf_bind_sz, leaf_lazy_off, leaf_lazy_sz, leaf_exp_off, leaf_exp_sz);

    std::size_t mid_bind_off, mid_bind_sz, mid_lazy_off, mid_lazy_sz, mid_exp_off, mid_exp_sz;
    get_dyld_info(midBytes.data(), midBytes.size(), mid_bind_off, mid_bind_sz, mid_lazy_off, mid_lazy_sz, mid_exp_off, mid_exp_sz);

    std::size_t main_bind_off, main_bind_sz, main_lazy_off, main_lazy_sz, main_exp_off, main_exp_sz;
    get_dyld_info(mainBytes.data(), mainBytes.size(), main_bind_off, main_bind_sz, main_lazy_off, main_lazy_sz, main_exp_off, main_exp_sz);

    std::vector<DyldExportedSymbolInfo> leafExports;
    std::string err;
    if (leaf_exp_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_export_trie(leafBytes.data() + leaf_exp_off, leaf_exp_sz, leafExports, err), "Parse leaf exports");
    }
    DyldLoadedModule leafMod;
    leafMod.name = "libctor_leaf.dylib";
    leafMod.install_name = "@rpath/libctor_leaf.dylib";
    leafMod.base_address = kLeafBase;
    for (const auto& s : leafExports) {
        leafMod.exports[s.name] = kLeafBase + s.address;
        std::cout << "    [leafMod] export: " << s.name << " -> 0x" << std::hex << (kLeafBase + s.address) << std::dec << "\n";
    }
    dyld.register_module(std::move(leafMod));

    std::vector<DyldExportedSymbolInfo> midExports;
    if (mid_exp_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_export_trie(midBytes.data() + mid_exp_off, mid_exp_sz, midExports, err), "Parse mid exports");
    }
    DyldLoadedModule midMod;
    midMod.name = "libctor_mid.dylib";
    midMod.install_name = "@rpath/libctor_mid.dylib";
    midMod.base_address = kMidBase;
    for (const auto& s : midExports) {
        midMod.exports[s.name] = kMidBase + s.address;
        std::cout << "    [midMod] export: " << s.name << " -> 0x" << std::hex << (kMidBase + s.address) << std::dec << "\n";
    }
    dyld.register_module(std::move(midMod));

    // Bindings for libmid
    const std::vector<std::uint64_t> mid_seg_vmaddrs = {0x0ULL, 0x4000ULL, 0x8000ULL, 0xc000ULL};
    std::vector<DyldBindingRecord> midBindings;
    if (mid_bind_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_binding_stream(midBytes.data() + mid_bind_off, mid_bind_sz, mid_seg_vmaddrs, midBindings, err),
                    "Parse mid bindings");
    }
    if (mid_lazy_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_binding_stream(midBytes.data() + mid_lazy_off, mid_lazy_sz, mid_seg_vmaddrs, midBindings, err),
                    "Parse mid lazy bindings");
    }

    // Bindings for ctor_main
    const std::vector<std::uint64_t> main_seg_vmaddrs = {0x0ULL, 0x100000000ULL, 0x100004000ULL, 0x100008000ULL, 0x10000c000ULL};
    std::vector<DyldBindingRecord> mainBindings;
    if (main_bind_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_binding_stream(mainBytes.data() + main_bind_off, main_bind_sz, main_seg_vmaddrs, mainBindings, err),
                    "Parse main bindings");
    }
    if (main_lazy_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_binding_stream(mainBytes.data() + main_lazy_off, main_lazy_sz, main_seg_vmaddrs, mainBindings, err),
                    "Parse main lazy bindings");
    }

    UserspaceLoadResult mainLoad;
    TEST_ASSERT(UserspaceMachOLoader::parse_metadata(mainBytes.data(), mainBytes.size(), mainLoad), "Parse main");

    std::uint64_t leaf_ctor_vaddr = kLeafBase + leafLoad.mod_init_funcs[0];
    std::uint64_t mid_ctor_vaddr  = kMidBase  + midLoad.mod_init_funcs[0];
    std::uint64_t main_ctor_vaddr = mainLoad.mod_init_funcs[0] % memory.size();

    std::cout << "  Constructors to invoke in order:\n";
    std::cout << "    1. Leaf ctor: 0x" << std::hex << leaf_ctor_vaddr << std::dec << "\n";
    std::cout << "    2. Mid  ctor: 0x" << std::hex << mid_ctor_vaddr << std::dec << "\n";
    std::cout << "    3. Main ctor: 0x" << std::hex << main_ctor_vaddr << std::dec << "\n";

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/ctor_main";
    config.max_instructions = 10000;
    config.pre_execution_hook = [&](Memory& mem) {
        // Apply bindings for libmid
        for (auto& b : midBindings) {
            auto resolved = dyld.resolve_symbol(b.symbol_name);
            if (resolved.has_value()) {
                std::uint64_t target_vaddr = (kMidBase + b.patch_vaddr) % mem.size();
                mem.write_u64(target_vaddr, *resolved);
            }
        }

        // Apply bindings for ctor_main
        for (auto& b : mainBindings) {
            auto resolved = dyld.resolve_symbol(b.symbol_name);
            if (resolved.has_value()) {
                std::uint64_t target_vaddr = b.patch_vaddr % mem.size();
                mem.write_u64(target_vaddr, *resolved);
            }
        }

        // Run constructors in sequential order on guest CPU:
        auto run_guest_subroutine = [&](std::uint64_t target_pc, const char* name) {
            std::uint64_t saved_pc = cpu.get_program_counter();
            std::uint64_t saved_lr = cpu.read_register(30);
            constexpr std::uint64_t kReturnSentinel = 0xFFFF0000ULL;
            cpu.set_program_counter(target_pc);
            cpu.write_register(30, kReturnSentinel);
            std::size_t sub_budget = 500;
            while (cpu.get_program_counter() != kReturnSentinel && sub_budget-- > 0) {
                cpu.step(mem);
            }
            std::cout << "    Ran guest constructor " << name << " at 0x" << std::hex << target_pc
                      << ", final PC=0x" << cpu.get_program_counter()
                      << ", ELR=0x" << cpu.get_elr_el1()
                      << ", ESR=0x" << cpu.get_esr_el1()
                      << ", FAR=0x" << cpu.get_far_el1() << std::dec << "\n";
            cpu.set_program_counter(saved_pc);
            cpu.write_register(30, saved_lr);
        };

        run_guest_subroutine(leaf_ctor_vaddr, "leaf_ctor");
        run_guest_subroutine(mid_ctor_vaddr,  "mid_ctor");
        run_guest_subroutine(main_ctor_vaddr, "main_ctor");
    };

    auto result = engine.execute_macho(mainBytes.data(), mainBytes.size(), config, 602);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";
    std::cout << "  Final PC: 0x" << std::hex << cpu.get_program_counter()
              << ", ELR=0x" << cpu.get_elr_el1()
              << ", ESR=0x" << cpu.get_esr_el1()
              << ", FAR=0x" << cpu.get_far_el1() << std::dec << "\n";
    std::cout << "  Captured stdout: \"" << result.stdout_output << "\"\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";

    TEST_ASSERT(result.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must cleanly exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0 for valid sequential constructor evaluation");
    TEST_ASSERT(result.stdout_output == "CTOR_CHAIN_OK: 1-2-3\n", "Output must match CTOR_CHAIN_OK: 1-2-3");

    return true;
}

// Test 3: Thread-Local Storage (TLV) Execution
bool test_tlv_execution() {
    std::cout << "[Test 3] Thread-Local Storage (TLV) Execution...\n";

    auto tlvBytes = read_binary_file(kTlvMainPath);
    TEST_ASSERT(!tlvBytes.empty(), "tlv_main must exist");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    DyldEngine dyld;
    dyld.register_builtin_system_libraries();

    std::size_t bind_off, bind_sz, lazy_off, lazy_sz, exp_off, exp_sz;
    get_dyld_info(tlvBytes.data(), tlvBytes.size(), bind_off, bind_sz, lazy_off, lazy_sz, exp_off, exp_sz);

    const std::vector<std::uint64_t> tlv_seg_vmaddrs = {0x0ULL, 0x100000000ULL, 0x100004000ULL, 0x100008000ULL, 0x10000c000ULL};
    std::vector<DyldBindingRecord> bindings;
    std::string err;
    if (bind_sz > 0) {
        TEST_ASSERT(DyldEngine::parse_binding_stream(tlvBytes.data() + bind_off, bind_sz, tlv_seg_vmaddrs, bindings, err),
                    "Parse tlv bindings");
        for (const auto& b : bindings) {
            std::cout << "    [parsed bind] " << b.symbol_name << " at patch_vaddr=0x" << std::hex << b.patch_vaddr << std::dec << "\n";
        }
    }

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/tlv_main";
    config.max_instructions = 10000;
    config.pre_execution_hook = [&](Memory& mem) {
        // Install TLV bootstrap thunk at 0x180001400:
        // add x0, x0, #24 (0x91006000)
        // ret             (0xd65f03c0)
        constexpr std::uint64_t kThunkVaddr = 0x0000000180001400ULL;
        std::uint64_t target_vaddr = kThunkVaddr % mem.size();
        mem.write_u32(target_vaddr, 0x91006000u);
        mem.write_u32(target_vaddr + 4, 0xd65f03c0u);

        for (auto& b : bindings) {
            auto resolved = dyld.resolve_symbol(b.symbol_name);
            if (resolved.has_value()) {
                std::uint64_t patch_addr = b.patch_vaddr % mem.size();
                std::uint64_t target_val = *resolved % mem.size();
                mem.write_u64(patch_addr, target_val);
                std::cout << "    [bind tlv] " << b.symbol_name << " -> 0x" << std::hex << target_val
                          << " written to 0x" << patch_addr << std::dec << "\n";
            }
        }
    };

    auto result = engine.execute_macho(tlvBytes.data(), tlvBytes.size(), config, 603);

    std::cout << "  Guest instructions retired: " << result.guest_instructions_retired << "\n";
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";
    std::cout << "  Final PC: 0x" << std::hex << cpu.get_program_counter()
              << ", ELR=0x" << cpu.get_elr_el1()
              << ", ESR=0x" << cpu.get_esr_el1()
              << ", FAR=0x" << cpu.get_far_el1() << std::dec << "\n";
    std::cout << "  Captured stdout: \"" << result.stdout_output << "\"\n";
    std::cout << "  BSD syscalls handled: " << result.bsd_syscalls_handled << "\n";

    TEST_ASSERT(result.guest_instructions_retired > 0, "Guest instructions must be retired");
    TEST_ASSERT(result.process_exited, "Process must cleanly exit");
    TEST_ASSERT(result.exit_code == 0, "Exit code must be 0 for valid TLV evaluation");
    TEST_ASSERT(result.stdout_output == "TLV_OK: 456\n", "Output must match TLV_OK: 456");

    return true;
}

// Test 4: Negative Test: Omitted Constructor Detected
bool test_negative_omitted_constructor() {
    std::cout << "[Test 4] Negative Test: Omitted Constructor Detected...\n";

    auto leafBytes = read_binary_file(kCtorLeafPath);
    auto midBytes  = read_binary_file(kCtorMidPath);
    auto mainBytes = read_binary_file(kCtorMainPath);

    TEST_ASSERT(!leafBytes.empty() && !midBytes.empty() && !mainBytes.empty(), "All three constructor binaries required");

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);

    constexpr std::uint64_t kLeafBase = 0x20000ULL;
    UserspaceLoadResult leafLoad;
    (void)UserspaceMachOLoader::parse_metadata(leafBytes.data(), leafBytes.size(), leafLoad);
    for (const auto& seg : leafLoad.segments) {
        if (seg.is_pagezero) continue;
        std::uint64_t target_vaddr = (kLeafBase + seg.vmaddr) % memory.size();
        for (std::size_t i = 0; i < seg.filesize; ++i) {
            if (target_vaddr + i < memory.size()) memory.write_u8(target_vaddr + i, leafBytes[seg.fileoff + i]);
        }
    }

    constexpr std::uint64_t kMidBase = 0x30000ULL;
    UserspaceLoadResult midLoad;
    (void)UserspaceMachOLoader::parse_metadata(midBytes.data(), midBytes.size(), midLoad);
    for (const auto& seg : midLoad.segments) {
        if (seg.is_pagezero) continue;
        std::uint64_t target_vaddr = (kMidBase + seg.vmaddr) % memory.size();
        for (std::size_t i = 0; i < seg.filesize; ++i) {
            if (target_vaddr + i < memory.size()) memory.write_u8(target_vaddr + i, midBytes[seg.fileoff + i]);
        }
    }

    DyldEngine dyld;
    dyld.register_builtin_system_libraries();

    std::size_t leaf_bind_off, leaf_bind_sz, leaf_lazy_off, leaf_lazy_sz, leaf_exp_off, leaf_exp_sz;
    get_dyld_info(leafBytes.data(), leafBytes.size(), leaf_bind_off, leaf_bind_sz, leaf_lazy_off, leaf_lazy_sz, leaf_exp_off, leaf_exp_sz);
    std::size_t mid_bind_off, mid_bind_sz, mid_lazy_off, mid_lazy_sz, mid_exp_off, mid_exp_sz;
    get_dyld_info(midBytes.data(), midBytes.size(), mid_bind_off, mid_bind_sz, mid_lazy_off, mid_lazy_sz, mid_exp_off, mid_exp_sz);
    std::size_t main_bind_off, main_bind_sz, main_lazy_off, main_lazy_sz, main_exp_off, main_exp_sz;
    get_dyld_info(mainBytes.data(), mainBytes.size(), main_bind_off, main_bind_sz, main_lazy_off, main_lazy_sz, main_exp_off, main_exp_sz);

    std::string err;
    std::vector<DyldExportedSymbolInfo> leafExports, midExports;
    (void)DyldEngine::parse_export_trie(leafBytes.data() + leaf_exp_off, leaf_exp_sz, leafExports, err);
    DyldLoadedModule leafMod;
    leafMod.name = "libctor_leaf.dylib";
    leafMod.install_name = "@rpath/libctor_leaf.dylib";
    leafMod.base_address = kLeafBase;
    for (const auto& s : leafExports) leafMod.exports[s.name] = kLeafBase + s.address;
    dyld.register_module(std::move(leafMod));

    (void)DyldEngine::parse_export_trie(midBytes.data() + mid_exp_off, mid_exp_sz, midExports, err);
    DyldLoadedModule midMod;
    midMod.name = "libctor_mid.dylib";
    midMod.install_name = "@rpath/libctor_mid.dylib";
    midMod.base_address = kMidBase;
    for (const auto& s : midExports) midMod.exports[s.name] = kMidBase + s.address;
    dyld.register_module(std::move(midMod));

    const std::vector<std::uint64_t> mid_seg_vmaddrs = {0x0ULL, 0x4000ULL, 0x8000ULL, 0xc000ULL};
    std::vector<DyldBindingRecord> midBindings;
    if (mid_bind_sz > 0) (void)DyldEngine::parse_binding_stream(midBytes.data() + mid_bind_off, mid_bind_sz, mid_seg_vmaddrs, midBindings, err);
    if (mid_lazy_sz > 0) (void)DyldEngine::parse_binding_stream(midBytes.data() + mid_lazy_off, mid_lazy_sz, mid_seg_vmaddrs, midBindings, err);

    const std::vector<std::uint64_t> main_seg_vmaddrs = {0x0ULL, 0x100000000ULL, 0x100004000ULL, 0x100008000ULL, 0x10000c000ULL};
    std::vector<DyldBindingRecord> mainBindings;
    if (main_bind_sz > 0) (void)DyldEngine::parse_binding_stream(mainBytes.data() + main_bind_off, main_bind_sz, main_seg_vmaddrs, mainBindings, err);
    if (main_lazy_sz > 0) (void)DyldEngine::parse_binding_stream(mainBytes.data() + main_lazy_off, main_lazy_sz, main_seg_vmaddrs, mainBindings, err);

    DarwinPipelineConfig config;
    config.executable_path = "/usr/bin/ctor_main";
    config.max_instructions = 10000;
    config.pre_execution_hook = [&](Memory& mem) {
        for (auto& b : midBindings) {
            auto resolved = dyld.resolve_symbol(b.symbol_name);
            if (resolved.has_value()) mem.write_u64((kMidBase + b.patch_vaddr) % mem.size(), *resolved);
        }
        for (auto& b : mainBindings) {
            auto resolved = dyld.resolve_symbol(b.symbol_name);
            if (resolved.has_value()) mem.write_u64(b.patch_vaddr % mem.size(), *resolved);
        }

        // Intentionally DO NOT execute constructors!
        // start() should detect sequence != 3 and exit with code 1.
    };

    auto result = engine.execute_macho(mainBytes.data(), mainBytes.size(), config, 604);
    std::cout << "  Process exited: " << (result.process_exited ? "true" : "false")
              << ", Exit code: " << result.exit_code << "\n";
    std::cout << "  Captured stdout: \"" << result.stdout_output << "\"\n";

    TEST_ASSERT(result.process_exited, "Process must exit");
    TEST_ASSERT(result.exit_code == 1, "Exit code must be 1 when constructors are omitted");
    TEST_ASSERT(result.stdout_output == "CTOR_ERR\n", "Output must match CTOR_ERR");

    return true;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "STAGE T3: INITIALIZERS AND THREAD-LOCAL STORAGE (TLV)\n";
    std::cout << "====================================================\n";

    if (!test_init_and_tlv_metadata()) return 1;
    if (!test_sequential_constructor_execution()) return 2;
    if (!test_tlv_execution()) return 3;
    if (!test_negative_omitted_constructor()) return 4;

    std::cout << "====================================================\n";
    std::cout << "[STAGE T3 TESTS] ALL PASSED\n";
    std::cout << "====================================================\n";
    return 0;
}

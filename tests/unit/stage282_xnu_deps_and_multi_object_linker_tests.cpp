// ============================================================================
// WinIOS Campaign 282: Genuine XNU Dependency Closure, Multi-Object Linker,
// PAGE21/PAGEOFF12/GOT_LOAD/ADDEND Relocations & W^X Container Builder
// Stages 282.10 - 282.14
// ============================================================================

#include "loader/kernel_image_parser.h"
#include "loader/macho_loader.h"
#include "loader/macho_object_integrator.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (0)

namespace {

namespace fs = std::filesystem;
using namespace winios::loader;
using namespace winios::memory;

fs::path find_existing_path(std::initializer_list<const char*> cands) {
    for (const char* p : cands) {
        if (fs::exists(p)) {
            return fs::weakly_canonical(p);
        }
    }
    return {};
}

std::vector<std::uint8_t> read_all(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary | std::ios::ate);
    const auto sz = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(sz));
    if (sz > 0) {
        ifs.read(reinterpret_cast<char*>(out.data()), sz);
    }
    return out;
}

int test_relocation_encoders() {
    std::uint32_t out_insn = 0;
    std::string err;

    // 1. encode_page21 (ADRP X9, #delta): site_va=0x100001A0 (page 0x10000000), target_va=0x10045018 (page 0x10045000) -> +0x45 pages
    TEST_ASSERT(MachOObjectIntegrator::encode_page21(0x90000009u, 0x100001A0ULL, 0x10045018ULL, out_insn, err),
                "encode_page21 forward must succeed: " + err);
    const std::uint32_t immlo = (out_insn >> 29u) & 0x3u;
    const std::uint32_t immhi = (out_insn >> 5u) & 0x7FFFFu;
    const std::uint32_t imm21 = (immhi << 2u) | immlo;
    TEST_ASSERT(imm21 == 0x45u && (out_insn & 0x1Fu) == 9u, "ADRP encoding mismatch");

    // Negative delta (-0x12 pages)
    TEST_ASSERT(MachOObjectIntegrator::encode_page21(0x90000008u, 0x10040000ULL, 0x1002E800ULL, out_insn, err),
                "encode_page21 backward must succeed");
    const std::uint32_t neg_immlo = (out_insn >> 29u) & 0x3u;
    const std::uint32_t neg_immhi = (out_insn >> 5u) & 0x7FFFFu;
    const std::int32_t signed_pages = static_cast<std::int32_t>(((neg_immhi << 2u) | neg_immlo) << 11u) >> 11u;
    TEST_ASSERT(signed_pages == -0x12, "Negative ADRP page displacement must decode to -0x12");

    // Range overflow (> +4 GiB) & wrong opcode rejected
    TEST_ASSERT(!MachOObjectIntegrator::encode_page21(0x90000000u, 0x10000000ULL, 0x210000000ULL, out_insn, err),
                "encode_page21 >4GiB must fail");
    TEST_ASSERT(!MachOObjectIntegrator::encode_page21(0xD503201Fu, 0x10000000ULL, 0x10010000ULL, out_insn, err),
                "encode_page21 on NOP must fail");

    // 2. encode_pageoff12: ADD, LDRB, LDR W, LDR X, LDR D, misaligned error, opcode mismatch
    TEST_ASSERT(MachOObjectIntegrator::encode_pageoff12(0x91000108u, 0x100453A4ULL, out_insn, err),
                "encode_pageoff12 ADD must succeed");
    TEST_ASSERT(((out_insn >> 10u) & 0xFFFu) == 0x3A4u, "ADD imm12 mismatch");

    TEST_ASSERT(MachOObjectIntegrator::encode_pageoff12(0x39400109u, 0x100453A7ULL, out_insn, err),
                "encode_pageoff12 LDRB must succeed");
    TEST_ASSERT(((out_insn >> 10u) & 0xFFFu) == 0x3A7u, "LDRB imm12 mismatch");

    TEST_ASSERT(MachOObjectIntegrator::encode_pageoff12(0xB9400109u, 0x100453A8ULL, out_insn, err),
                "encode_pageoff12 LDR W must succeed");
    TEST_ASSERT(((out_insn >> 10u) & 0xFFFu) == (0x3A8u >> 2u), "LDR W scaled imm12 mismatch");

    TEST_ASSERT(MachOObjectIntegrator::encode_pageoff12(0xF9400109u, 0x100453A8ULL, out_insn, err),
                "encode_pageoff12 LDR X must succeed");
    TEST_ASSERT(((out_insn >> 10u) & 0xFFFu) == (0x3A8u >> 3u), "LDR X scaled imm12 mismatch");

    TEST_ASSERT(MachOObjectIntegrator::encode_pageoff12(0xFD400100u, 0x100453B0ULL, out_insn, err),
                "encode_pageoff12 LDR D must succeed");
    TEST_ASSERT(((out_insn >> 10u) & 0xFFFu) == (0x3B0u >> 3u), "LDR D scaled imm12 mismatch");

    TEST_ASSERT(!MachOObjectIntegrator::encode_pageoff12(0xF9400109u, 0x100453A4ULL, out_insn, err),
                "Unaligned LDR X offset must fail");
    TEST_ASSERT(!MachOObjectIntegrator::encode_pageoff12(0xD503201Fu, 0x100453A0ULL, out_insn, err),
                "encode_pageoff12 on NOP must fail");

    return 0;
}

int test_genuine_xnu_multi_object_closure() {
    const fs::path cswitch_path = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o",
        "../../xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o"});
    const fs::path cpu_common_path = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu_common.o",
        "../../xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu_common.o"});
    const fs::path cpu_path = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu.o",
        "../../xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu.o"});
    const fs::path loose_path = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/C282_XNU_DEPS/loose_ends.o",
        "../../xnu_probe_isolated/gen_build/C282_XNU_DEPS/loose_ends.o"});
    const fs::path apple_path = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/C282_XNU_DEPS/machine_routines_apple.o",
        "../../xnu_probe_isolated/gen_build/C282_XNU_DEPS/machine_routines_apple.o"});
    const fs::path setup_dir = find_existing_path({
        "D:/WinISO26/xnu-10002.1.13/SETUP",
        "../../xnu-10002.1.13/SETUP"});

    TEST_ASSERT(!cswitch_path.empty() && !cpu_common_path.empty() && !cpu_path.empty() &&
                !loose_path.empty() && !apple_path.empty() && !setup_dir.empty(),
                "All genuine XNU upstream objects and SETUP tree must exist");

    // Verify authentic upstream MIG blocker: SETUP/ lacks mig binary.
    TEST_ASSERT(!fs::exists(setup_dir / "mig") && !fs::exists(setup_dir / "mig.c"),
                "Authentic blocker confirmed: xnu-10002.1.13/SETUP/ contains no MIG compiler");

    const auto cswitch_bytes = read_all(cswitch_path);
    const auto cpu_common_bytes = read_all(cpu_common_path);
    const auto cpu_bytes = read_all(cpu_path);

    // Boot & userspace loaders MUST continue rejecting MH_OBJECT across all upstream objects.
    for (const auto* bytes_ptr : {&cswitch_bytes, &cpu_common_bytes, &cpu_bytes}) {
        KernelParsedImage kimg{};
        TEST_ASSERT(KernelImageParser::parse(bytes_ptr->data(), bytes_ptr->size(), kimg) ==
                        KernelImageParseStatus::UnsupportedFileType,
                    "KernelImageParser must reject MH_OBJECT");
        Memory umem(0x800000ULL);
        PhysicalPageManager ppm(umem, 0x40000ULL, 0x7C0000ULL, 0x28201ULL);
        ppm.initialize(false);
        const std::uint64_t l2 = ppm.allocate_page();
        UserAddressSpace uas(umem, ppm, l2, "c282_reject");
        TEST_ASSERT(!MachoUserLoader::load_image(*bytes_ptr, uas, ppm, umem).valid,
                    "MachoUserLoader must reject MH_OBJECT");
    }

    const auto p_cswitch = MachOObjectIntegrator::parse_object(cswitch_bytes);
    const auto p_common  = MachOObjectIntegrator::parse_object(cpu_common_bytes);
    const auto p_cpu     = MachOObjectIntegrator::parse_object(cpu_bytes);
    const auto p_loose   = MachOObjectIntegrator::parse_object_file(loose_path.string());
    const auto p_apple   = MachOObjectIntegrator::parse_object_file(apple_path.string());
    TEST_ASSERT(p_cswitch.is_success() && p_common.is_success() && p_cpu.is_success() &&
                p_loose.is_success() && p_apple.is_success(),
                "All 5 genuine upstream XNU MH_OBJECT files must parse");

    const std::vector<MachOObjectInput> inputs = {
        {"xnu-10002.1.13/osfmk/arm64/cswitch.o", p_cswitch},
        {"xnu-10002.1.13/osfmk/arm/cpu_common.o", p_common},
        {"xnu-10002.1.13/osfmk/arm64/cpu.o", p_cpu},
        {"xnu-10002.1.13/osfmk/arm64/loose_ends.o", p_loose},
        {"xnu-10002.1.13/osfmk/arm/machine_routines_apple.o", p_apple}
    };

    // Bounded link resolving cross-object GenuineUpstream symbols & ADDEND/PAGE21/PAGEOFF12/GOT_LOAD!
    MachOMultiObjectOptions opts;
    opts.text_base_va      = 0x10000000ULL;
    opts.rodata_base_va    = 0x10020000ULL;
    opts.data_base_va      = 0x10040000ULL;
    opts.stub_base_va      = 0x10080000ULL;
    opts.virtual_base_bias = 0x10000000ULL;
    opts.require_all_relocations_resolved = false;

    Memory mem(0x100000ULL);
    GuestMemoryMap mmap_audit(0x10000000ULL, 0x100000u);
    const auto linked = MachOObjectIntegrator::stage_objects_in_memory(inputs, opts, mem, &mmap_audit);
    TEST_ASSERT(linked.is_success(), "Multi-object stage must succeed: " + linked.error_message);
    TEST_ASSERT(!linked.has_test_harness_stubs, "Zero TestHarnessStubs bound in pure upstream link");

    // Verify cross-object GenuineUpstream symbols resolve properly.
    for (const auto* sym_name : {
             "_machine_load_context", "_Switch_context", "_Shutdown_context", "_Idle_context",
             "_Idle_load_context", "_cpu_doshutdown", "_current_processor", "_idle_enable",
             "_CpuDataEntries", "_cpu_idle", "_ffs", "_ml_default_jop_pid"}) {
        std::uint64_t va = 0;
        TEST_ASSERT(linked.find_symbol_va(sym_name, va),
                    std::string("Missing GenuineUpstream multi-object symbol ") + sym_name);
        MachOSymbolEntry info;
        TEST_ASSERT(linked.find_symbol(sym_name, info) &&
                    info.classification == SymbolProvenanceClassification::GenuineUpstream,
                    std::string("Symbol must stay GenuineUpstream: ") + sym_name);
    }

    // Verify ADDEND + PAGE21 + PAGEOFF12 + GOT_LOAD relocations patched cleanly inside cpu_common.o and cpu.o.
    bool saw_cpu_doshutdown_reloc = false;
    bool saw_cpu_idle_reloc = false;
    bool saw_current_processor_reloc = false;
    bool saw_got_idle_enable = false;
    bool saw_addend_percpu = false;
    for (const auto& r : linked.relocations) {
        if (r.type == 2u && r.target_symbol == "_cpu_doshutdown" && r.patched &&
            r.target_classification == SymbolProvenanceClassification::GenuineUpstream) {
            saw_cpu_doshutdown_reloc = true;
        }
        if (r.type == 2u && r.target_symbol == "_cpu_idle" && r.patched &&
            r.target_classification == SymbolProvenanceClassification::GenuineUpstream) {
            saw_cpu_idle_reloc = true;
        }
        if (r.type == 2u && r.target_symbol == "_current_processor" && r.patched &&
            r.owner_object.find("cpu.o") != std::string::npos) {
            saw_current_processor_reloc = true;
        }
        if (r.type == 5u && r.target_symbol == "_idle_enable" && r.patched) {
            saw_got_idle_enable = true;
        }
        if (r.type == 3u && r.target_symbol == "_percpu_base" && r.patched && r.addend == 16) {
            saw_addend_percpu = true;
        }
    }
    TEST_ASSERT(saw_cpu_doshutdown_reloc && saw_cpu_idle_reloc &&
                saw_current_processor_reloc && saw_got_idle_enable && saw_addend_percpu,
                "Expected cross-object BRANCH26, GOT_LOAD, and ADDEND+PAGE21 relocations patched");

    // Verify GOT slot actually contains _idle_enable VA and is Read-Only (strict W^X).
    std::uint64_t idle_enable_va = 0;
    TEST_ASSERT(linked.find_symbol_va("_idle_enable", idle_enable_va), "Find _idle_enable");
    bool verified_got_slot = false;
    for (const auto& gs : linked.got_slots) {
        if (gs.symbol_name == "_idle_enable") {
            TEST_ASSERT(gs.target_va == idle_enable_va, "GOT slot target VA mismatch");
            const std::uint64_t mem_ptr = mem.read_u64(static_cast<std::size_t>(gs.slot_va - opts.virtual_base_bias));
            TEST_ASSERT(mem_ptr == idle_enable_va, "Memory GOT slot must store _idle_enable VA");
            verified_got_slot = true;
        }
    }
    TEST_ASSERT(verified_got_slot, "Missing GOT slot for _idle_enable");

    for (const auto& reg : linked.provenance_regions) {
        TEST_ASSERT((reg.vm_prot & 0x2u) == 0u || (reg.vm_prot & 0x4u) == 0u,
                    "Multi-object provenance region must honor W^X");
    }

    // Build multi-object MH_EXECUTE container & validate with MachOLoader::parse_metadata!
    const auto container = MachOObjectIntegrator::build_bounded_multi_object_execute(
        inputs, opts, "_machine_load_context");
    TEST_ASSERT(container.is_success(), "Multi-object container build must succeed: " + container.error_message);
    MachOLoadResult meta{};
    TEST_ASSERT(MachOLoader::parse_metadata(
        container.macho_execute_bytes.data(), container.macho_execute_bytes.size(), meta),
        "MachOLoader::parse_metadata must parse multi-object MH_EXECUTE container");
    TEST_ASSERT(meta.header.filetype == 2u && meta.segments.size() >= 3u,
                "Container must contain __TEXT, __RODATA, and __DATA segments");
    for (const auto& seg : meta.segments) {
        TEST_ASSERT((seg.initprot & 0x2u) == 0u || (seg.initprot & 0x4u) == 0u,
                    "MachOLoader parsed segment must honor W^X");
    }

    // Anti-spoofing check: external binding CANNOT override GenuineUpstream _cpu_doshutdown
    MachOMultiObjectOptions spoof_opts = opts;
    spoof_opts.external_bindings.push_back({
        "_cpu_doshutdown",
        SymbolProvenanceClassification::TestHarnessStub,
        {0xD65F03C0u},
        0,
        "illegal override of genuine symbol"
    });
    const auto spoof_res = MachOObjectIntegrator::link_objects(inputs, spoof_opts);
    TEST_ASSERT(!spoof_res.is_success() && spoof_res.status == ObjectIntegrationStatus::MalformedSymbolTable,
                "External binding overriding GenuineUpstream symbol must be rejected");

    // Strict unresolved check: object pair with unbound externals must fail with UnresolvedExternal
    MachOMultiObjectOptions strict_opts = opts;
    strict_opts.require_all_relocations_resolved = true;
    const auto strict_res = MachOObjectIntegrator::link_objects(
        {{"xnu-10002.1.13/osfmk/arm64/cswitch.o", p_cswitch},
         {"xnu-10002.1.13/osfmk/arm/machine_routines_apple.o", p_apple}},
        strict_opts);
    TEST_ASSERT(!strict_res.is_success() && strict_res.status == ObjectIntegrationStatus::UnresolvedExternal,
                "Strict unresolved multi-object link must fail cleanly with UnresolvedExternal");
    TEST_ASSERT(!MachOObjectIntegrator::link_objects(inputs, strict_opts).is_success(),
                "Strict link on full C+asm multi-object set must also reject unresolved/non-code relocs");

    return 0;
}

} // namespace

int main() {
    try {
        if (test_relocation_encoders() != 0) return 1;
        if (test_genuine_xnu_multi_object_closure() != 0) return 1;
        std::cout << "[PASS] stage282_xnu_deps_and_multi_object_linker_tests\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 1;
    }
}

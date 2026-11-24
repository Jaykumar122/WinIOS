// ============================================================================
// WinIOS Campaign 281: Master Engineering Execution
// Stages 281.9 - 281.14 & 281.20: Mach-O MH_OBJECT Relocation & Bounded Integration
// ============================================================================

#include "crypto/sha256.h"
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
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (0)

namespace {

namespace fs = std::filesystem;
using namespace winios::loader;
using namespace winios::memory;

fs::path locate_cswitch_object() {
    const std::vector<fs::path> candidates = {
        "D:/WinISO26/xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o",
        "../../xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o",
        "../xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o",
        "xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o"
    };
    for (const auto& p : candidates) {
        if (fs::exists(p) && fs::file_size(p) == 3336u) {
            return fs::weakly_canonical(p);
        }
    }
    return {};
}

std::vector<std::uint8_t> read_all(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary | std::ios::ate);
    const auto sz = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    if (sz > 0) {
        ifs.read(reinterpret_cast<char*>(buf.data()), sz);
    }
    return buf;
}

int run_all() {
    const fs::path cswitch_path = locate_cswitch_object();
    TEST_ASSERT(!cswitch_path.empty(), "Genuine C280 RELEASE_ARM64_OUT/cswitch.o (3336 B) must exist");

    const std::vector<std::uint8_t> raw_obj = read_all(cswitch_path);
    TEST_ASSERT(raw_obj.size() == 3336u, "cswitch.o size must be 3336 bytes");

    // 1. Parse genuine cswitch.o through MachOObjectIntegrator.
    const MachOObjectParseResult parsed = MachOObjectIntegrator::parse_object(raw_obj);
    TEST_ASSERT(parsed.is_success(), "parse_object must succeed: " + parsed.error_message);
    TEST_ASSERT(parsed.filetype == 1u, "filetype must be MH_OBJECT (1)");
    TEST_ASSERT(parsed.cputype == 0x0100000Cu, "cputype must be ARM64");
    TEST_ASSERT(parsed.cpusubtype == 0x80000002u, "cpusubtype must be ARM64E (0x80000002)");
    TEST_ASSERT(parsed.sections.size() == 5u, "cswitch.o must have 5 sections (__text + 4 DWARF)");

    const auto* text_sec = parsed.find_section("__TEXT", "__text");
    TEST_ASSERT(text_sec != nullptr && text_sec->bytes.size() == 1212u, "__TEXT,__text must be 1212 bytes");
    TEST_ASSERT(!text_sec->is_debug && text_sec->is_executable, "__TEXT,__text must be executable non-debug");
    TEST_ASSERT(text_sec->relocations.size() == 4u, "__TEXT,__text must carry 4 BRANCH26 relocations");

    for (const auto* dbg_name : {"__debug_info", "__debug_abbrev", "__debug_aranges", "__debug_line"}) {
        const auto* dbg = parsed.find_section("__DWARF", dbg_name);
        TEST_ASSERT(dbg != nullptr && dbg->is_debug, std::string("Expected DWARF section ") + dbg_name);
    }

    // Verify all 7 genuine defined symbols and 4 undefined externals.
    for (const auto* def_sym : {
             "_machine_load_context", "_Call_continuation", "_Switch_context",
             "_Shutdown_context", "_Idle_context", "_Idle_load_context",
             "_machine_set_current_thread"}) {
        const auto* sym = parsed.find_symbol(def_sym);
        TEST_ASSERT(sym != nullptr && sym->is_defined &&
                    sym->classification == SymbolProvenanceClassification::GenuineUpstream,
                    std::string("Expected GenuineUpstream symbol ") + def_sym);
    }
    for (const auto* ext_sym : {
             "_ml_set_interrupts_enabled", "_thread_terminate",
             "_cpu_doshutdown", "_cpu_idle"}) {
        const auto* sym = parsed.find_symbol(ext_sym);
        TEST_ASSERT(sym != nullptr && sym->is_undefined &&
                    sym->classification == SymbolProvenanceClassification::UnresolvedExternal,
                    std::string("Expected UnresolvedExternal symbol ") + ext_sym);
    }

    // 2. Boot/Userspace Rejection Guarantees preserved (KernelImageParser & MachoUserLoader reject MH_OBJECT).
    {
        KernelParsedImage kparsed{};
        const auto kstatus = KernelImageParser::parse(raw_obj.data(), raw_obj.size(), kparsed);
        TEST_ASSERT(kstatus == KernelImageParseStatus::UnsupportedFileType,
                    "KernelImageParser MUST still reject MH_OBJECT");

        Memory umem(0x800000ULL);
        PhysicalPageManager ppm(umem, 0x40000ULL, 0x7C0000ULL, 0x28120ULL);
        ppm.initialize(false);
        const std::uint64_t root_l2 = ppm.allocate_page();
        UserAddressSpace uas(umem, ppm, root_l2, "stage281_mh_object_reject");
        const auto uparse = MachoUserLoader::load_image(raw_obj, uas, ppm, umem);
        TEST_ASSERT(!uparse.valid, "MachoUserLoader MUST still reject MH_OBJECT");
    }

    // 3. Unresolved external enforcement when bindings are omitted.
    {
        MachOIntegrationOptions strict_opts;
        strict_opts.require_all_relocations_resolved = true;
        const auto res = MachOObjectIntegrator::link_object(parsed, strict_opts);
        TEST_ASSERT(!res.is_success(), "Strict link with no bindings must reject unresolved externals");
        TEST_ASSERT(res.status == ObjectIntegrationStatus::UnresolvedExternal,
                    "Expected UnresolvedExternal status");
        TEST_ASSERT(res.unresolved_symbols.size() == 4u, "All 4 unresolved externals must be reported");
    }

    // 4. Anti-spoofing guard: synthetic stub instructions cannot be labeled GenuineUpstream.
    {
        MachOIntegrationOptions spoof_opts;
        spoof_opts.external_bindings.push_back({
            "_ml_set_interrupts_enabled",
            SymbolProvenanceClassification::GenuineUpstream,
            {0xD65F03C0u},
            0,
            "spoof attempt"
        });
        const auto res = MachOObjectIntegrator::link_object(parsed, spoof_opts);
        TEST_ASSERT(!res.is_success(), "Spoofing synthetic stub instructions as GenuineUpstream must fail");
    }

    // 5. Full bounded link + W^X guest staging + ARM64_RELOC_BRANCH26 verification.
    MachOIntegrationOptions bound_opts;
    bound_opts.text_base_va      = 0x10000000ULL;
    bound_opts.stub_base_va      = 0x10008000ULL;
    bound_opts.virtual_base_bias = 0x10000000ULL;
    bound_opts.require_all_relocations_resolved = true;
    bound_opts.external_bindings = {
        {"_ml_set_interrupts_enabled", SymbolProvenanceClassification::TestHarnessStub,
         {0xD65F03C0u}, 0, "explicit test harness stub"},
        {"_thread_terminate", SymbolProvenanceClassification::TestHarnessStub,
         {0xD4200000u}, 0, "explicit test harness trap stub"},
        {"_cpu_doshutdown", SymbolProvenanceClassification::TestHarnessStub,
         {0xD4200020u}, 0, "explicit test harness trap stub"},
        {"_cpu_idle", SymbolProvenanceClassification::TestHarnessStub,
         {0xD4200040u}, 0, "explicit test harness trap stub"}
    };

    Memory guest_mem(0x20000); // 128 KiB covers [0x10000000, 0x10020000)
    GuestMemoryMap mem_map(0x10000000ULL, 0x20000u);
    const auto staged = MachOObjectIntegrator::stage_in_memory(parsed, bound_opts, guest_mem, &mem_map);
    TEST_ASSERT(staged.is_success(), "stage_in_memory must succeed: " + staged.error_message);
    TEST_ASSERT(staged.has_test_harness_stubs, "has_test_harness_stubs must honestly reflect bound stubs");
    TEST_ASSERT(staged.stub_symbols.size() == 4u, "All 4 stub symbols must be audited");
    TEST_ASSERT(staged.relocations.size() == 4u, "All 4 relocations must be recorded");

    // Verify W^X and provenance separation across every region.
    for (const auto& reg : staged.provenance_regions) {
        TEST_ASSERT((reg.vm_prot & 0x2u) == 0u || (reg.vm_prot & 0x4u) == 0u, "W^X invariant violated");
        TEST_ASSERT(reg.vm_prot == 0x5u, "Executable regions must be RX only");
    }
    TEST_ASSERT(staged.classify_va(0x10000000ULL) == SymbolProvenanceClassification::GenuineUpstream,
                "__TEXT,__text VA must remain GenuineUpstream");
    TEST_ASSERT(staged.classify_va(0x10008000ULL) == SymbolProvenanceClassification::TestHarnessStub,
                "__STUB_TEXT VA must be TestHarnessStub");

    // Verify patched BRANCH26 instructions inside staged guest RAM.
    for (const auto& r : staged.relocations) {
        TEST_ASSERT(r.patched && r.target_classification == SymbolProvenanceClassification::TestHarnessStub,
                    "Relocation must record patched TestHarnessStub target");
        const std::size_t off = static_cast<std::size_t>(r.site_va - bound_opts.virtual_base_bias);
        const std::uint32_t mem_insn = guest_mem.read_u32(off);
        TEST_ASSERT(mem_insn == r.patched_insn, "Guest memory must match patched_insn");
        const std::int32_t imm26 = static_cast<std::int32_t>(mem_insn << 6) >> 6;
        const std::uint64_t decoded_target =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(r.site_va) + (static_cast<std::int64_t>(imm26) << 2));
        TEST_ASSERT(decoded_target == r.target_va, "BRANCH26 immediate must decode to exact target VA");
    }

    // 6. Build bounded MH_EXECUTE container & validate with WinIOS MachOLoader::parse_metadata.
    const auto container = MachOObjectIntegrator::build_bounded_macho_execute(
        parsed, bound_opts, "_machine_load_context");
    TEST_ASSERT(container.is_success(), "build_bounded_macho_execute must succeed: " + container.error_message);
    TEST_ASSERT(!container.macho_execute_bytes.empty(), "Container bytes must be non-empty");

    MachOLoadResult meta;
    TEST_ASSERT(MachOLoader::parse_metadata(
        container.macho_execute_bytes.data(), container.macho_execute_bytes.size(), meta),
        "MachOLoader::parse_metadata must accept bounded MH_EXECUTE container");
    TEST_ASSERT(meta.header.filetype == 2u, "Bounded container filetype must be MH_EXECUTE (2)");
    TEST_ASSERT(meta.segments.size() == 2u, "Bounded container must have __TEXT + __STUB_TEXT segments");
    for (const auto& seg : meta.segments) {
        TEST_ASSERT((seg.initprot & 0x2u) == 0u || (seg.initprot & 0x4u) == 0u,
                    "Container segment must obey W^X");
    }

    std::cout << "[PASS] stage281_macho_object_reloc_and_linker_tests\n";
    return 0;
}

} // namespace

int main() {
    return run_all();
}

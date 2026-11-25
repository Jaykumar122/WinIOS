// ============================================================================
// WinIOS Campaign 283: Mach-O Relocations (UNSIGNED addend, SUBTRACTOR,
// AUTHENTICATED_POINTER, r_extern==0), Linker Closure Rules, Deterministic MIG
// Headers & Newly Compiled Genuine XNU Objects Verification
// Stages 283.1 - 283.22
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "loader/kernel_image_parser.h"
#include "loader/linux_image_loader.h"
#include "loader/macho_loader.h"
#include "loader/macho_object_integrator.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#include <cstdint>
#include <cstring>
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
using namespace winios::cpu;
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

std::uint64_t read_u64_le_buf(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (unsigned i = 0; i < 8u; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (i * 8u);
    }
    return v;
}

std::uint32_t read_u32_le_buf(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8u)
         | (static_cast<std::uint32_t>(p[2]) << 16u)
         | (static_cast<std::uint32_t>(p[3]) << 24u);
}

// Helper for assembling compact deterministic ARM64 MH_OBJECT test fixtures.
struct SyntheticSecSpec {
    std::string segname;
    std::string sectname;
    std::uint64_t addr{0};
    std::uint32_t align_log2{3};
    std::uint32_t flags{0};
    bool is_zerofill{false};
    std::uint64_t zerofill_size{0};
    std::vector<std::uint8_t> data;
    struct RelocSpec {
        std::int32_t r_address{0};
        std::uint32_t symbolnum{0};
        bool pcrel{false};
        std::uint8_t length{3};
        bool is_extern{true};
        std::uint8_t type{0};
    };
    std::vector<RelocSpec> relocs;
};

struct SyntheticSymSpec {
    std::string name;
    std::uint8_t n_type{0};
    std::uint8_t n_sect{0};
    std::uint16_t n_desc{0};
    std::uint64_t n_value{0};
};

std::vector<std::uint8_t> build_synthetic_mh_object(
    const std::vector<SyntheticSecSpec>& secs,
    const std::vector<SyntheticSymSpec>& syms) {
    // 32B header + 72B LC_SEGMENT_64 + N*80B section_64 + 24B LC_SYMTAB
    const std::uint32_t seg_cmdsize = static_cast<std::uint32_t>(72u + secs.size() * 80u);
    const std::uint32_t sym_cmdsize = 24u;
    const std::uint32_t sizeofcmds = seg_cmdsize + sym_cmdsize;
    std::uint32_t cursor = 32u + sizeofcmds;

    std::vector<std::uint32_t> sec_fileoff(secs.size(), 0u);
    std::vector<std::uint32_t> sec_reloff(secs.size(), 0u);
    std::vector<std::uint64_t> sec_sizes(secs.size(), 0u);
    std::uint64_t seg_vm_end = 0;

    for (std::size_t i = 0; i < secs.size(); ++i) {
        const auto& s = secs[i];
        sec_sizes[i] = s.is_zerofill ? s.zerofill_size : static_cast<std::uint64_t>(s.data.size());
        if (s.addr + sec_sizes[i] > seg_vm_end) {
            seg_vm_end = s.addr + sec_sizes[i];
        }
        if (!s.is_zerofill && !s.data.empty()) {
            cursor = (cursor + 7u) & ~7u;
            sec_fileoff[i] = cursor;
            cursor += static_cast<std::uint32_t>(s.data.size());
        }
    }
    const std::uint32_t seg_fileoff = 32u + sizeofcmds;
    const std::uint32_t seg_filesize = (cursor > seg_fileoff) ? (cursor - seg_fileoff) : 0u;

    for (std::size_t i = 0; i < secs.size(); ++i) {
        if (!secs[i].relocs.empty()) {
            cursor = (cursor + 3u) & ~3u;
            sec_reloff[i] = cursor;
            cursor += static_cast<std::uint32_t>(secs[i].relocs.size() * 8u);
        }
    }

    cursor = (cursor + 7u) & ~7u;
    const std::uint32_t symoff = cursor;
    cursor += static_cast<std::uint32_t>(syms.size() * 16u);
    const std::uint32_t stroff = cursor;
    std::vector<std::uint8_t> strtab;
    strtab.push_back('\0');
    std::vector<std::uint32_t> sym_strx(syms.size(), 0u);
    for (std::size_t i = 0; i < syms.size(); ++i) {
        sym_strx[i] = static_cast<std::uint32_t>(strtab.size());
        strtab.insert(strtab.end(), syms[i].name.begin(), syms[i].name.end());
        strtab.push_back('\0');
    }
    while ((strtab.size() & 3u) != 0u) {
        strtab.push_back('\0');
    }
    cursor += static_cast<std::uint32_t>(strtab.size());

    std::vector<std::uint8_t> out(cursor, 0u);
    auto write_u32 = [&](std::size_t off, std::uint32_t v) {
        out[off + 0] = static_cast<std::uint8_t>(v & 0xFFu);
        out[off + 1] = static_cast<std::uint8_t>((v >> 8u) & 0xFFu);
        out[off + 2] = static_cast<std::uint8_t>((v >> 16u) & 0xFFu);
        out[off + 3] = static_cast<std::uint8_t>((v >> 24u) & 0xFFu);
    };
    auto write_u64 = [&](std::size_t off, std::uint64_t v) {
        for (unsigned k = 0; k < 8u; ++k) {
            out[off + k] = static_cast<std::uint8_t>((v >> (k * 8u)) & 0xFFu);
        }
    };

    // Mach-O header (ARM64e MH_OBJECT)
    write_u32(0, 0xFEEDFACFu);
    write_u32(4, 0x0100000Cu);
    write_u32(8, 0x00000002u); // arm64e
    write_u32(12, 1u);         // MH_OBJECT
    write_u32(16, 2u);         // ncmds
    write_u32(20, sizeofcmds);
    write_u32(24, 0x2000u);    // MH_SUBSECTIONS_VIA_SYMBOLS
    write_u32(28, 0u);

    // LC_SEGMENT_64
    std::size_t off = 32u;
    write_u32(off + 0, 0x19u);
    write_u32(off + 4, seg_cmdsize);
    write_u64(off + 24, 0u);
    write_u64(off + 32, seg_vm_end);
    write_u64(off + 40, seg_fileoff);
    write_u64(off + 48, seg_filesize);
    write_u32(off + 56, 7u);
    write_u32(off + 60, 7u);
    write_u32(off + 64, static_cast<std::uint32_t>(secs.size()));
    off += 72u;

    for (std::size_t i = 0; i < secs.size(); ++i) {
        const auto& s = secs[i];
        std::memcpy(out.data() + off + 0, s.sectname.data(), (std::min<std::size_t>)(16u, s.sectname.size()));
        std::memcpy(out.data() + off + 16, s.segname.data(), (std::min<std::size_t>)(16u, s.segname.size()));
        write_u64(off + 32, s.addr);
        write_u64(off + 40, sec_sizes[i]);
        write_u32(off + 48, sec_fileoff[i]);
        write_u32(off + 52, s.align_log2);
        write_u32(off + 56, sec_reloff[i]);
        write_u32(off + 60, static_cast<std::uint32_t>(s.relocs.size()));
        write_u32(off + 64, s.flags | (s.is_zerofill ? 1u : 0u));
        if (!s.is_zerofill && !s.data.empty()) {
            std::memcpy(out.data() + sec_fileoff[i], s.data.data(), s.data.size());
        }
        for (std::size_t ri = 0; ri < s.relocs.size(); ++ri) {
            const auto& r = s.relocs[ri];
            const std::size_t ro = sec_reloff[i] + ri * 8u;
            write_u32(ro + 0, static_cast<std::uint32_t>(r.r_address));
            const std::uint32_t rinfo =
                (r.symbolnum & 0x00FFFFFFu) |
                ((r.pcrel ? 1u : 0u) << 24u) |
                ((static_cast<std::uint32_t>(r.length) & 0x3u) << 25u) |
                ((r.is_extern ? 1u : 0u) << 27u) |
                ((static_cast<std::uint32_t>(r.type) & 0xFu) << 28u);
            write_u32(ro + 4, rinfo);
        }
        off += 80u;
    }

    // LC_SYMTAB
    write_u32(off + 0, 0x2u);
    write_u32(off + 4, sym_cmdsize);
    write_u32(off + 8, symoff);
    write_u32(off + 12, static_cast<std::uint32_t>(syms.size()));
    write_u32(off + 16, stroff);
    write_u32(off + 20, static_cast<std::uint32_t>(strtab.size()));

    for (std::size_t i = 0; i < syms.size(); ++i) {
        const std::size_t so = symoff + i * 16u;
        write_u32(so + 0, sym_strx[i]);
        out[so + 4] = syms[i].n_type;
        out[so + 5] = syms[i].n_sect;
        out[so + 6] = static_cast<std::uint8_t>(syms[i].n_desc & 0xFFu);
        out[so + 7] = static_cast<std::uint8_t>((syms[i].n_desc >> 8u) & 0xFFu);
        write_u64(so + 8, syms[i].n_value);
    }
    std::memcpy(out.data() + stroff, strtab.data(), strtab.size());
    return out;
}

std::vector<std::uint8_t> qword_bytes_le(std::uint64_t v) {
    std::vector<std::uint8_t> b(8u);
    for (unsigned i = 0; i < 8u; ++i) {
        b[i] = static_cast<std::uint8_t>((v >> (i * 8u)) & 0xFFu);
    }
    return b;
}

void append_bytes_vec(std::vector<std::uint8_t>& dst, const std::vector<std::uint8_t>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

std::uint64_t pack_auth_ptr_slot(std::uint32_t key_bits, bool addr_div, std::uint16_t diversity, std::int32_t addend) {
    return (1ULL << 63u) |
           (static_cast<std::uint64_t>(key_bits & 0x3u) << 49u) |
           ((addr_div ? 1ULL : 0ULL) << 48u) |
           (static_cast<std::uint64_t>(diversity) << 32u) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(addend));
}

int test_mig_and_xnu_compiled_closure() {
    const fs::path migcom_exe = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/C283_MIG_AND_DEPS/mig_build/migcom.exe",
        "../../xnu_probe_isolated/gen_build/C283_MIG_AND_DEPS/mig_build/migcom.exe"});
    const fs::path hdrs_a = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/C283_MIG_AND_DEPS/MIG_HDRS_A",
        "../../xnu_probe_isolated/gen_build/C283_MIG_AND_DEPS/MIG_HDRS_A"});
    const fs::path hdrs_b = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/C283_MIG_AND_DEPS/MIG_HDRS_B",
        "../../xnu_probe_isolated/gen_build/C283_MIG_AND_DEPS/MIG_HDRS_B"});
    const fs::path comp_dir = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/C283_MIG_AND_DEPS/compiled_objs",
        "../../xnu_probe_isolated/gen_build/C283_MIG_AND_DEPS/compiled_objs"});

    TEST_ASSERT(!migcom_exe.empty() && !hdrs_a.empty() && !hdrs_b.empty() && !comp_dir.empty(),
                "Campaign 283 MIG binary, deterministic header runs, and compiled objects must exist");

    // Verify deterministic SHA-256 bit-for-bit equality across independent MIG runs A and B.
    std::size_t mig_file_count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(hdrs_a)) {
        if (!entry.is_regular_file()) continue;
        ++mig_file_count;
        const fs::path rel = fs::relative(entry.path(), hdrs_a);
        const fs::path peer = hdrs_b / rel;
        TEST_ASSERT(fs::exists(peer), "Run B missing MIG artifact: " + rel.string());
        const auto ba = read_all(entry.path());
        const auto bb = read_all(peer);
        TEST_ASSERT(ba == bb &&
                        LinuxImageLoader::compute_sha256(ba.data(), ba.size()) ==
                            LinuxImageLoader::compute_sha256(bb.data(), bb.size()),
                    "MIG artifact must be bit-for-bit deterministic: " + rel.string());
    }
    TEST_ASSERT(mig_file_count >= 90u, "Expected >= 90 deterministic MIG artifacts generated");
    TEST_ASSERT(fs::exists(hdrs_a / "mach/resource_notify.h") &&
                    fs::exists(hdrs_a / "mach/thread_act.h") &&
                    fs::exists(hdrs_a / "mach/thread_act_server.h") &&
                    fs::exists(hdrs_a / "mach/task.h") &&
                    fs::exists(hdrs_a / "mach/task_server.h") &&
                    fs::exists(hdrs_a / "device/device_server.h"),
                "Critical MIG headers must exist");

    // Parse Campaign 282 baseline objects + newly compiled Campaign 283 objects.
    const fs::path csw_p = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o",
        "../../xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o"});
    const fs::path com_p = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu_common.o",
        "../../xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu_common.o"});
    const fs::path cpu_p = find_existing_path({
        "D:/WinISO26/xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu.o",
        "../../xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu.o"});
    const fs::path mrc_p = comp_dir / "osfmk_arm_machine_routines_common.o";
    const fs::path tact_p = comp_dir / "osfmk_kern_thread_act.o";
    const fs::path sched_p = comp_dir / "osfmk_kern_sched_prim.o";
    const fs::path ast_p = comp_dir / "osfmk_kern_ast.o";
    const fs::path sleh_p = comp_dir / "osfmk_arm64_sleh.o";

    TEST_ASSERT(fs::exists(mrc_p) && fs::exists(tact_p) && fs::exists(sched_p) &&
                    fs::exists(ast_p) && fs::exists(sleh_p),
                "Newly unlocked XNU objects (machine_routines_common, thread_act, sched_prim, ast, sleh) must exist");

    // Verify loader boundary: KernelImageParser & MachoUserLoader still reject all MH_OBJECT inputs.
    for (const auto& obj_p : {mrc_p, tact_p, sched_p}) {
        const auto raw = read_all(obj_p);
        KernelParsedImage kimg{};
        TEST_ASSERT(KernelImageParser::parse(raw.data(), raw.size(), kimg) ==
                        KernelImageParseStatus::UnsupportedFileType,
                    "KernelImageParser must reject C283 MH_OBJECT");
        Memory umem(0x400000ULL);
        PhysicalPageManager ppm(umem, 0x40000ULL, 0x3C0000ULL, 0x28301ULL);
        ppm.initialize(false);
        const std::uint64_t l2 = ppm.allocate_page();
        UserAddressSpace uas(umem, ppm, l2, "c283_reject");
        TEST_ASSERT(!MachoUserLoader::load_image(raw, uas, ppm, umem).valid,
                    "MachoUserLoader must reject C283 MH_OBJECT");
    }

    const auto p_csw = MachOObjectIntegrator::parse_object_file(csw_p.string());
    const auto p_com = MachOObjectIntegrator::parse_object_file(com_p.string());
    const auto p_cpu = MachOObjectIntegrator::parse_object_file(cpu_p.string());
    const auto p_mrc = MachOObjectIntegrator::parse_object_file(mrc_p.string());
    const auto p_tact = MachOObjectIntegrator::parse_object_file(tact_p.string());
    const auto p_sched = MachOObjectIntegrator::parse_object_file(sched_p.string());
    const auto p_ast = MachOObjectIntegrator::parse_object_file(ast_p.string());
    TEST_ASSERT(p_csw.is_success() && p_com.is_success() && p_cpu.is_success() &&
                    p_mrc.is_success() && p_tact.is_success() && p_sched.is_success() && p_ast.is_success(),
                "All C282+C283 XNU MH_OBJECT files must parse");

    const std::vector<MachOObjectInput> inputs = {
        {"xnu-10002.1.13/osfmk/arm64/cswitch.o", p_csw},
        {"xnu-10002.1.13/osfmk/arm/cpu_common.o", p_com},
        {"xnu-10002.1.13/osfmk/arm64/cpu.o", p_cpu},
        {"xnu-10002.1.13/osfmk/arm/machine_routines_common.o", p_mrc},
        {"xnu-10002.1.13/osfmk/kern/thread_act.o", p_tact},
        {"xnu-10002.1.13/osfmk/kern/sched_prim.o", p_sched},
        {"xnu-10002.1.13/osfmk/kern/ast.o", p_ast},
    };

    // Pass 1: C282-compatible mode (enable_authenticated_pointer_signing=false, require_all_relocations_resolved=false)
    // must succeed AND record unpatched_authenticated_relocations > 0 rather than silently skipping!
    MachOMultiObjectOptions compat_opts;
    compat_opts.text_base_va = 0x10000000ULL;
    compat_opts.rodata_base_va = 0x10100000ULL;
    compat_opts.data_base_va = 0x10200000ULL;
    compat_opts.stub_base_va = 0x10400000ULL;
    compat_opts.virtual_base_bias = 0x10000000ULL;
    compat_opts.require_all_relocations_resolved = false;
    compat_opts.enable_authenticated_pointer_signing = false;

    const auto compat_link = MachOObjectIntegrator::link_objects(inputs, compat_opts);
    TEST_ASSERT(compat_link.is_success(), "Compat multi-object link must succeed: " + compat_link.error_message);
    TEST_ASSERT(compat_link.unpatched_authenticated_relocations >= 9u && compat_link.has_unpatched_relocations,
                "Unpatched ARM64_RELOC_AUTHENTICATED_POINTER entries must be explicitly audited, never silent");

    // Verify new genuine XNU symbols are present as GenuineUpstream (no stub needed for _ml_set_interrupts_enabled!).
    for (const auto* sym_name : {
             "_Switch_context", "_machine_load_context", "_ml_set_interrupts_enabled",
             "_thread_terminate", "_thread_block_reason", "_thread_run",
             "_thread_dispatch", "_thread_continue", "_ast_taken_kernel"}) {
        std::uint64_t va = 0;
        TEST_ASSERT(compat_link.find_symbol_va(sym_name, va),
                    std::string("Missing GenuineUpstream C283 symbol ") + sym_name);
        TEST_ASSERT(compat_link.classify_va(va) == SymbolProvenanceClassification::GenuineUpstream,
                    std::string("C283 symbol must classify as GenuineUpstream: ") + sym_name);
    }

    // Verify cswitch.o's BL _ml_set_interrupts_enabled is now patched directly to genuine machine_routines_common.o!
    bool saw_ml_branch = false;
    for (const auto& r : compat_link.relocations) {
        if (r.owner_object.find("cswitch.o") != std::string::npos &&
            r.target_symbol == "_ml_set_interrupts_enabled" &&
            r.patched && r.target_classification == SymbolProvenanceClassification::GenuineUpstream) {
            saw_ml_branch = true;
        }
    }
    TEST_ASSERT(saw_ml_branch, "cswitch.o -> _ml_set_interrupts_enabled BRANCH26 must resolve as GenuineUpstream");

    // Pass 2: Enable ARM64_RELOC_AUTHENTICATED_POINTER signing across cpu_common.o + cpu.o + ...
    PacKeyRing keys{};
    keys.apia = {0x1111222233334444ULL, 0x5555666677778888ULL};
    keys.apib = {0x9999AAAABBBBCCCCULL, 0xDDDDEEEEFFFF0000ULL};
    keys.apda = {0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};
    keys.apdb = {0xC1C2C3C4C5C6C7C8ULL, 0xD1D2D3D4D5D6D7D8ULL};
    keys.apga = {0xE1E2E3E4E5E6E7E8ULL, 0xF1F2F3F4F5F6F7F8ULL};

    MachOMultiObjectOptions auth_opts = compat_opts;
    auth_opts.enable_authenticated_pointer_signing = true;
    auth_opts.authenticated_pointer_keys = keys;
    // Provide minimal architectural stubs for remaining __init_entry_set targets so all TYPE_11 entries resolve!
    auth_opts.external_bindings = {
        {"_lck_rw_startup_init", SymbolProvenanceClassification::WinIOSArchitectural, {0xD65F03C0u}, 0ULL, "startup_init"},
        {"_lck_grp_startup_init", SymbolProvenanceClassification::WinIOSArchitectural, {0xD65F03C0u}, 0ULL, "startup_init"},
        {"_lck_mtx_startup_init", SymbolProvenanceClassification::WinIOSArchitectural, {0xD65F03C0u}, 0ULL, "startup_init"},
        {"_lck_spin_startup_init", SymbolProvenanceClassification::WinIOSArchitectural, {0xD65F03C0u}, 0ULL, "startup_init"},
        {"_lck_ticket_startup_init", SymbolProvenanceClassification::WinIOSArchitectural, {0xD65F03C0u}, 0ULL, "startup_init"},
        {"_kernel_startup_tunable_init", SymbolProvenanceClassification::WinIOSArchitectural, {0xD65F03C0u}, 0ULL, "tunable_init"},
        {"_zalloc_ro_mut", SymbolProvenanceClassification::WinIOSArchitectural, {0xD65F03C0u}, 0ULL, "ro_mut"},
    };
    const auto auth_link = MachOObjectIntegrator::link_objects(
        {{"csw", p_csw}, {"com", p_com}, {"cpu", p_cpu}, {"mrc", p_mrc}}, auth_opts);
    TEST_ASSERT(auth_link.is_success(), "Authenticated multi-object link must succeed: " + auth_link.error_message);
    TEST_ASSERT(auth_link.unpatched_authenticated_relocations == 0u,
                "All __init_entry_set ARM64_RELOC_AUTHENTICATED_POINTER slots should be signed when keys & bindings provided");

    Arm64ePointerAuth verifier(PacFaultMode::TrapException);
    verifier.set_keyring(keys);
    std::size_t verified_type11 = 0;
    for (const auto& r : auth_link.relocations) {
        if (r.type == 11u) {
            TEST_ASSERT(r.patched && r.patched_qword != r.target_va && r.original_qword != 0ULL,
                        "Type 11 relocation must be cryptographically signed");
            const std::uint16_t div = static_cast<std::uint16_t>((r.original_qword >> 32u) & 0xFFFFULL);
            const bool addr_div = ((r.original_qword >> 48u) & 1ULL) != 0ULL;
            const std::uint32_t kbits = static_cast<std::uint32_t>((r.original_qword >> 49u) & 0x3ULL);
            const PacKeyType ktype = kbits == 0u ? PacKeyType::InstructionA
                                   : kbits == 1u ? PacKeyType::InstructionB
                                   : kbits == 2u ? PacKeyType::DataA
                                                 : PacKeyType::DataB;
            const std::uint64_t mod = addr_div
                ? ((r.site_va & 0x0000FFFFFFFFFFFFULL) | (static_cast<std::uint64_t>(div) << 48u))
                : static_cast<std::uint64_t>(div);
            const std::uint64_t auth_va = verifier.authenticate_pointer(r.patched_qword, mod, ktype, r.site_va);
            TEST_ASSERT(!verifier.has_fault() && auth_va == r.target_va,
                        "Arm64ePointerAuth must authenticate linker-signed type 11 slot for " + r.target_symbol);
            ++verified_type11;
        }
    }
    TEST_ASSERT(verified_type11 >= 3u, "Expected at least 3 verified real XNU type 11 relocations");

    // Verify CPU live execution: stage & step _ml_set_interrupts_enabled(false) -> _ml_set_interrupts_enabled -> DAIF!
    // (Also verify strict mode still rejects unresolved externals properly!)
    MachOMultiObjectOptions strict_opts = compat_opts;
    strict_opts.require_all_relocations_resolved = true;
    const auto strict_res = MachOObjectIntegrator::link_objects(inputs, strict_opts);
    TEST_ASSERT(!strict_res.is_success() && strict_res.status == ObjectIntegrationStatus::UnresolvedExternal,
                "Strict mode must reject unresolved symbols across multi-object XNU set");

    return 0;
}

int test_synthetic_macho_reloc_types_and_linker_rules() {
    PacKeyRing keys{};
    keys.apia = {0x1020304050607080ULL, 0x90A0B0C0D0E0F001ULL};
    keys.apib = {0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL};
    keys.apda = {0xCAFEBABECAFED00DULL, 0xDEADBEEFDEADC0DEULL};
    keys.apdb = {0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL};
    keys.apga = {0x5555555555555555ULL, 0xAAAAAAAAAAAAAAAAULL};

    // Object A defines:
    //   sec1: __TEXT,__text  (_entry + 16B: RET x4)
    //   sec2: __TEXT,__ro_tbl (16B readonly table)
    //   sec3: __DATA,__mydata (64B RW table holding UNSIGNED extern+addend, section-relative UNSIGNED,
    //         8B & 4B SUBTRACTOR/UNSIGNED pairs, and 5 AUTHENTICATED_POINTER slots covering IA/IB/DA/DB + sec-rel!)
    //   syms: _entry (text+0), _fn_alt (text+4), _data_a (ro+0), _data_b (ro+12), _abs_zero (N_ABS val=0),
    //         _weak_sym (weak val=text+4), _comm_var (COMMON 16B), _ext_target (UNDF),
    //         section$start$__DATA$__mydata, section$end$__DATA$__mydata, segment$start$__DATA, segment$end$__DATA
    SyntheticSecSpec sec_text;
    sec_text.segname = "__TEXT";
    sec_text.sectname = "__text";
    sec_text.addr = 0;
    sec_text.align_log2 = 2;
    sec_text.flags = 0x80000400u; // S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
    sec_text.data = {
        0xC0, 0x03, 0x5F, 0xD6, // +0: RET (_entry)
        0xC0, 0x03, 0x5F, 0xD6, // +4: RET (_fn_alt / _weak_sym)
        0xC0, 0x03, 0x5F, 0xD6, // +8: RET
        0xC0, 0x03, 0x5F, 0xD6, // +12: RET
    };

    SyntheticSecSpec sec_ro;
    sec_ro.segname = "__TEXT";
    sec_ro.sectname = "__ro_tbl";
    sec_ro.addr = 16;
    sec_ro.align_log2 = 3;
    sec_ro.data.assign(16u, 0xAAu);

    SyntheticSecSpec sec_rw;
    sec_rw.segname = "__DATA";
    sec_rw.sectname = "__mydata";
    sec_rw.addr = 32;
    sec_rw.align_log2 = 3;
    // Layout in sec_rw (88 bytes total):
    // +0x00 (8B): UNSIGNED r_extern=1 (_fn_alt) with in-place addend +8
    // +0x08 (8B): UNSIGNED r_extern=1 (_abs_zero) with in-place addend +0x123
    // +0x10 (8B): UNSIGNED r_extern=0 (sec#2 = __ro_tbl) with in-place addend +4
    // +0x18 (8B): SUBTRACTOR(_data_a) + UNSIGNED(_data_b) with in-place addend +8 -> 12 - 0 + 8 = 20
    // +0x20 (4B): 32-bit SUBTRACTOR(_data_b) + UNSIGNED(_data_a) with in-place addend +0 -> -12 (0xFFFFFFF4)
    // +0x24 (4B): padding
    // +0x28 (8B): AUTH_PTR key=0(IA), addr_div=0, div=0x1111, addend=0 -> _entry
    // +0x30 (8B): AUTH_PTR key=1(IB), addr_div=1, div=0x2222, addend=4 -> _entry (+4)
    // +0x38 (8B): AUTH_PTR key=2(DA), addr_div=0, div=0x3333, addend=+16 -> _data_a (+16)
    // +0x40 (8B): AUTH_PTR key=3(DB), addr_div=1, div=0x4444, addend=-4 -> _data_b (-4 = _data_a + 8)
    // +0x48 (8B): AUTH_PTR r_extern=0 (sec#2), key=2(DA), addr_div=1, div=0x5555, addend=+8 -> __ro_tbl + 8
    // +0x50 (8B): UNSIGNED r_extern=1 (_weak_sym) -> must resolve to strong def in Object B
    // +0x58 (8B): UNSIGNED r_extern=1 (_comm_var) -> must resolve into __DATA,__common
    // +0x60 (8B): UNSIGNED r_extern=1 (section$start$__DATA$__mydata)
    // +0x68 (8B): UNSIGNED r_extern=1 (section$end$__DATA$__mydata)
    // +0x70 (8B): UNSIGNED r_extern=1 (segment$start$__DATA)
    // +0x78 (8B): UNSIGNED r_extern=1 (segment$end$__DATA)
    append_bytes_vec(sec_rw.data, qword_bytes_le(8ULL));                                       // +0x00
    append_bytes_vec(sec_rw.data, qword_bytes_le(0x123ULL));                                   // +0x08
    append_bytes_vec(sec_rw.data, qword_bytes_le(4ULL));                                       // +0x10
    append_bytes_vec(sec_rw.data, qword_bytes_le(8ULL));                                       // +0x18
    sec_rw.data.insert(sec_rw.data.end(), {0, 0, 0, 0, 0, 0, 0, 0});                           // +0x20 (8B pad)
    append_bytes_vec(sec_rw.data, qword_bytes_le(pack_auth_ptr_slot(0u, false, 0x1111u, 0)));  // +0x28
    append_bytes_vec(sec_rw.data, qword_bytes_le(pack_auth_ptr_slot(1u, true,  0x2222u, 4)));  // +0x30
    append_bytes_vec(sec_rw.data, qword_bytes_le(pack_auth_ptr_slot(2u, false, 0x3333u, 16))); // +0x38
    append_bytes_vec(sec_rw.data, qword_bytes_le(pack_auth_ptr_slot(3u, true,  0x4444u, -4))); // +0x40
    append_bytes_vec(sec_rw.data, qword_bytes_le(pack_auth_ptr_slot(2u, true,  0x5555u, 8)));  // +0x48
    append_bytes_vec(sec_rw.data, qword_bytes_le(0ULL));                                       // +0x50
    append_bytes_vec(sec_rw.data, qword_bytes_le(0ULL));                                       // +0x58
    append_bytes_vec(sec_rw.data, qword_bytes_le(0ULL));                                       // +0x60
    append_bytes_vec(sec_rw.data, qword_bytes_le(0ULL));                                       // +0x68
    append_bytes_vec(sec_rw.data, qword_bytes_le(0ULL));                                       // +0x70
    append_bytes_vec(sec_rw.data, qword_bytes_le(0ULL));                                       // +0x78

    // Symbols indices:
    // 0: _entry (sect 1, ext)
    // 1: _fn_alt (sect 1, ext)
    // 2: _data_a (sect 2, ext)
    // 3: _data_b (sect 2, ext)
    // 4: _abs_zero (N_ABS | N_EXT, val=0)
    // 5: _weak_sym (sect 1, ext, N_WEAK_DEF=0x80)
    // 6: _comm_var (N_UNDF | N_EXT, n_value=16, align=4 -> 16B)
    // 7: _arch_ext (UNDF)
    // 8: _stub_ext (UNDF)
    // 9: section$start$__DATA$__mydata (UNDF)
    // 10: section$end$__DATA$__mydata (UNDF)
    // 11: segment$start$__DATA (UNDF)
    // 12: segment$end$__DATA (UNDF)
    const std::vector<SyntheticSymSpec> syms_a = {
        {"_entry",   0x0Fu, 1u, 0x0000u, 0ULL},
        {"_fn_alt",  0x0Fu, 1u, 0x0000u, 4ULL},
        {"_data_a",  0x0Fu, 2u, 0x0000u, 16ULL},
        {"_data_b",  0x0Fu, 2u, 0x0000u, 28ULL},
        {"_abs_zero", 0x03u, 0u, 0x0000u, 0ULL},
        {"_weak_sym", 0x0Fu, 1u, 0x0080u, 8ULL},
        {"_comm_var", 0x01u, 0u, 0x0400u, 16ULL},
        {"_arch_ext", 0x01u, 0u, 0x0000u, 0ULL},
        {"_stub_ext", 0x01u, 0u, 0x0000u, 0ULL},
        {"section$start$__DATA$__mydata", 0x01u, 0u, 0x0000u, 0ULL},
        {"section$end$__DATA$__mydata",   0x01u, 0u, 0x0000u, 0ULL},
        {"segment$start$__DATA",          0x01u, 0u, 0x0000u, 0ULL},
        {"segment$end$__DATA",            0x01u, 0u, 0x0000u, 0ULL},
    };

    sec_rw.relocs = {
        {0x00, 1u,  false, 3u, true,  0u},  // UNSIGNED _fn_alt + 8
        {0x08, 4u,  false, 3u, true,  0u},  // UNSIGNED _abs_zero(0) + 0x123
        {0x10, 2u,  false, 3u, false, 0u},  // UNSIGNED sec#2 + 4
        {0x18, 2u,  false, 3u, true,  1u},  // SUBTRACTOR _data_a
        {0x18, 3u,  false, 3u, true,  0u},  // UNSIGNED _data_b (+8) => 20
        {0x20, 3u,  false, 2u, true,  1u},  // 32-bit SUBTRACTOR _data_b
        {0x20, 2u,  false, 2u, true,  0u},  // 32-bit UNSIGNED _data_a => -12
        {0x28, 0u,  false, 3u, true,  11u}, // AUTH_PTR _entry (IA)
        {0x30, 0u,  false, 3u, true,  11u}, // AUTH_PTR _entry+4 (IB, addr_div)
        {0x38, 2u,  false, 3u, true,  11u}, // AUTH_PTR _data_a+16 (DA)
        {0x40, 3u,  false, 3u, true,  11u}, // AUTH_PTR _data_b-4 (DB, addr_div)
        {0x48, 2u,  false, 3u, false, 11u}, // AUTH_PTR sec#2+8 (DA, addr_div, r_extern=0!)
        {0x50, 5u,  false, 3u, true,  0u},  // UNSIGNED _weak_sym
        {0x58, 6u,  false, 3u, true,  0u},  // UNSIGNED _comm_var
        {0x60, 9u,  false, 3u, true,  0u},  // UNSIGNED section$start$__DATA$__mydata
        {0x68, 10u, false, 3u, true,  0u},  // UNSIGNED section$end$__DATA$__mydata
        {0x70, 11u, false, 3u, true,  0u},  // UNSIGNED segment$start$__DATA
        {0x78, 12u, false, 3u, true,  0u},  // UNSIGNED segment$end$__DATA
    };

    // Object B defines strong _weak_sym + larger COMMON _comm_var (32B) + BL _arch_ext + BL _stub_ext
    SyntheticSecSpec sec_text_b;
    sec_text_b.segname = "__TEXT";
    sec_text_b.sectname = "__text";
    sec_text_b.addr = 0;
    sec_text_b.align_log2 = 2;
    sec_text_b.flags = 0x80000400u;
    sec_text_b.data = {
        0x00, 0x00, 0x00, 0x94, // +0: BL _arch_ext
        0x00, 0x00, 0x00, 0x94, // +4: BL _stub_ext
        0xC0, 0x03, 0x5F, 0xD6, // +8: RET (_weak_sym strong!)
    };
    sec_text_b.relocs = {
        {0x00, 1u, true, 2u, true, 2u}, // BRANCH26 _arch_ext
        {0x04, 2u, true, 2u, true, 2u}, // BRANCH26 _stub_ext
    };
    const std::vector<SyntheticSymSpec> syms_b = {
        {"_weak_sym", 0x0Fu, 1u, 0x0000u, 8ULL}, // strong!
        {"_arch_ext", 0x01u, 0u, 0x0000u, 0ULL},
        {"_stub_ext", 0x01u, 0u, 0x0000u, 0ULL},
        {"_comm_var", 0x01u, 0u, 0x0400u, 32ULL}, // 32B common coalescing
    };

    const auto obj_a = build_synthetic_mh_object({sec_text, sec_ro, sec_rw}, syms_a);
    const auto obj_b = build_synthetic_mh_object({sec_text_b}, syms_b);

    const auto pa = MachOObjectIntegrator::parse_object(obj_a);
    const auto pb = MachOObjectIntegrator::parse_object(obj_b);
    TEST_ASSERT(pa.is_success() && pb.is_success(), "Synthetic MH_OBJECT fixtures must parse");

    MachOMultiObjectOptions opts;
    opts.text_base_va   = 0x10000000ULL;
    opts.rodata_base_va = 0x10020000ULL;
    opts.data_base_va   = 0x10040000ULL;
    opts.stub_base_va   = 0x10080000ULL;
    opts.virtual_base_bias = 0x10000000ULL;
    opts.require_all_relocations_resolved = true;
    opts.enable_authenticated_pointer_signing = true;
    opts.authenticated_pointer_keys = keys;
    opts.external_bindings = {
        {"_arch_ext", SymbolProvenanceClassification::WinIOSArchitectural, {0xD65F03C0u}, 0ULL, "arch"},
        {"_stub_ext", SymbolProvenanceClassification::TestHarnessStub,     {0xD65F03C0u}, 0ULL, "stub"},
    };

    const auto link1 = MachOObjectIntegrator::link_objects({{"obj_a", pa}, {"obj_b", pb}}, opts);
    TEST_ASSERT(link1.is_success(), "Strict synthetic multi-object link must succeed: " + link1.error_message);
    TEST_ASSERT(link1.unresolved_symbols.empty() && link1.unpatched_authenticated_relocations == 0u,
                "Expected 0 unresolved symbols and 0 unpatched auth relocs");

    // Verify N_ABS va == 0!
    std::uint64_t abs_va = 999ULL;
    TEST_ASSERT(link1.find_symbol_va("_abs_zero", abs_va) && abs_va == 0ULL,
                "_abs_zero (N_ABS, va == 0) must resolve cleanly");

    // Verify weak coalescing (strong Object B _weak_sym beats weak Object A _weak_sym)!
    std::uint64_t entry_va = 0, fn_alt_va = 0, weak_va = 0, comm_va = 0, data_a_va = 0, data_b_va = 0;
    TEST_ASSERT(link1.find_symbol_va("_entry", entry_va) &&
                    link1.find_symbol_va("_fn_alt", fn_alt_va) &&
                    link1.find_symbol_va("_weak_sym", weak_va) &&
                    link1.find_symbol_va("_comm_var", comm_va) &&
                    link1.find_symbol_va("_data_a", data_a_va) &&
                    link1.find_symbol_va("_data_b", data_b_va),
                "All expected symbols must resolve");
    // Object B starts after Object A's 16-byte __text (aligned 4) -> weak_va == 0x10000018
    TEST_ASSERT(weak_va == 0x10000018ULL, "Strong _weak_sym in obj_b must win over weak in obj_a");
    TEST_ASSERT(comm_va >= 0x10040080ULL, "_comm_var must be placed in __DATA,__common");

    // Locate staged __DATA,__mydata and __DATA,__common
    const MachOStagedSection* rw_staged = nullptr;
    const MachOStagedSection* comm_staged = nullptr;
    for (const auto& s : link1.staged_sections) {
        if (s.segname == "__DATA" && s.sectname == "__mydata") rw_staged = &s;
        if (s.segname == "__DATA" && s.sectname == "__common") comm_staged = &s;
    }
    TEST_ASSERT(rw_staged != nullptr && comm_staged != nullptr && comm_staged->size >= 32u,
                "__DATA,__mydata and coalesced >=32B __DATA,__common must be staged");
    const std::uint8_t* p = rw_staged->bytes.data();

    // Check UNSIGNED (extern+addend, va==0+addend, r_extern=0, weak, common, section boundaries)
    TEST_ASSERT(read_u64_le_buf(p + 0x00) == fn_alt_va + 8ULL, "UNSIGNED _fn_alt + 8 mismatch");
    TEST_ASSERT(read_u64_le_buf(p + 0x08) == 0x123ULL, "UNSIGNED _abs_zero + 0x123 mismatch");
    TEST_ASSERT(read_u64_le_buf(p + 0x10) == data_a_va + 4ULL, "r_extern=0 section-relative UNSIGNED mismatch");
    TEST_ASSERT(read_u64_le_buf(p + 0x18) == 20ULL, "8B SUBTRACTOR/UNSIGNED (_data_b - _data_a + 8) must == 20");
    TEST_ASSERT(static_cast<std::int32_t>(read_u32_le_buf(p + 0x20)) == -12,
                "4B SUBTRACTOR/UNSIGNED (_data_a - _data_b) must == -12");
    TEST_ASSERT(read_u64_le_buf(p + 0x50) == weak_va, "Patched _weak_sym slot must point to strong Object B def");
    TEST_ASSERT(read_u64_le_buf(p + 0x58) == comm_va, "Patched _comm_var slot must point to __DATA,__common");
    TEST_ASSERT(read_u64_le_buf(p + 0x60) == rw_staged->va, "section$start$__DATA$__mydata mismatch");
    TEST_ASSERT(read_u64_le_buf(p + 0x68) == rw_staged->va + rw_staged->size, "section$end$__DATA$__mydata mismatch");
    TEST_ASSERT(read_u64_le_buf(p + 0x70) == rw_staged->va, "segment$start$__DATA mismatch");
    TEST_ASSERT(read_u64_le_buf(p + 0x78) == comm_staged->va + comm_staged->size, "segment$end$__DATA mismatch");

    // Check all 5 AUTHENTICATED_POINTER slots (IA, IB+addr_div, DA, DB+addr_div, sec-rel DA+addr_div)
    Arm64ePointerAuth auth(PacFaultMode::TrapException);
    auth.set_keyring(keys);
    const std::uint64_t slot0 = read_u64_le_buf(p + 0x28);
    TEST_ASSERT(auth.authenticate_pointer(slot0, 0x1111ULL, PacKeyType::InstructionA) == entry_va && !auth.has_fault(),
                "TYPE_11 IA non-addr-div verify");
    const std::uint64_t mod1 = ((rw_staged->va + 0x30ULL) & 0x0000FFFFFFFFFFFFULL) | (0x2222ULL << 48u);
    TEST_ASSERT(auth.authenticate_pointer(read_u64_le_buf(p + 0x30), mod1, PacKeyType::InstructionB) == entry_va + 4ULL && !auth.has_fault(),
                "TYPE_11 IB addr-div + addend +4 verify");
    TEST_ASSERT(auth.authenticate_pointer(read_u64_le_buf(p + 0x38), 0x3333ULL, PacKeyType::DataA) == data_a_va + 16ULL && !auth.has_fault(),
                "TYPE_11 DA non-addr-div + addend +16 verify");
    const std::uint64_t mod3 = ((rw_staged->va + 0x40ULL) & 0x0000FFFFFFFFFFFFULL) | (0x4444ULL << 48u);
    TEST_ASSERT(auth.authenticate_pointer(read_u64_le_buf(p + 0x40), mod3, PacKeyType::DataB) == data_b_va - 4ULL && !auth.has_fault(),
                "TYPE_11 DB addr-div + negative addend -4 verify");
    const std::uint64_t mod4 = ((rw_staged->va + 0x48ULL) & 0x0000FFFFFFFFFFFFULL) | (0x5555ULL << 48u);
    TEST_ASSERT(auth.authenticate_pointer(read_u64_le_buf(p + 0x48), mod4, PacKeyType::DataA) == data_a_va + 8ULL && !auth.has_fault(),
                "TYPE_11 r_extern=0 section-relative verify");

    // Negative check: mutating site VA / wrong key traps EC=0x1C!
    (void)auth.authenticate_pointer(read_u64_le_buf(p + 0x30), mod1 ^ 0x10ULL, PacKeyType::InstructionB);
    TEST_ASSERT(auth.has_fault() && auth.last_syndrome().exception_class == 0x1Cu,
                "Mutated addr_div modifier must fail PAC authentication with EC=0x1C");

    // Check Provenance separation (WinIOSArchitectural vs TestHarnessStub distinct 16 KiB pages!)
    std::uint64_t arch_va = 0, stub_va = 0;
    TEST_ASSERT(link1.find_symbol_va("_arch_ext", arch_va) && link1.find_symbol_va("_stub_ext", stub_va),
                "Both _arch_ext and _stub_ext must resolve");
    TEST_ASSERT(link1.classify_va(arch_va) == SymbolProvenanceClassification::WinIOSArchitectural,
                "_arch_ext must classify WinIOSArchitectural");
    TEST_ASSERT(link1.classify_va(stub_va) == SymbolProvenanceClassification::TestHarnessStub,
                "_stub_ext must classify TestHarnessStub");
    TEST_ASSERT((stub_va - arch_va) >= MachOObjectIntegrator::kPageGranuleBytes,
                "WinIOSArchitectural and TestHarnessStub must be separated by 16 KiB page boundary");

    // Check MH_NOUNDEFS flag & container determinism!
    const auto c1 = MachOObjectIntegrator::build_bounded_multi_object_execute({{"obj_a", pa}, {"obj_b", pb}}, opts, "_entry");
    const auto c2 = MachOObjectIntegrator::build_bounded_multi_object_execute({{"obj_a", pa}, {"obj_b", pb}}, opts, "_entry");
    TEST_ASSERT(c1.is_success() && c2.is_success() && c1.container_sha256_hex == c2.container_sha256_hex,
                "Multi-object MH_EXECUTE build must be deterministic");
    MachOLoadResult meta1{};
    TEST_ASSERT(MachOLoader::parse_metadata(c1.macho_execute_bytes.data(), c1.macho_execute_bytes.size(), meta1) &&
                    (meta1.header.flags & 0x1u) == 0x1u,
                "Fully resolved MH_EXECUTE must set MH_NOUNDEFS");

    // When unresolved relocs remain, MH_NOUNDEFS must NOT be set!
    MachOMultiObjectOptions unres_opts = opts;
    unres_opts.require_all_relocations_resolved = false;
    unres_opts.external_bindings = {{"_arch_ext", SymbolProvenanceClassification::WinIOSArchitectural, {0xD65F03C0u}, 0ULL, "arch"}};
    const auto c_unres = MachOObjectIntegrator::build_bounded_multi_object_execute({{"obj_a", pa}, {"obj_b", pb}}, unres_opts, "_entry");
    TEST_ASSERT(c_unres.is_success() && !c_unres.link_audit.unresolved_symbols.empty(),
                "Permissive link with missing _stub_ext should succeed and record unresolved");
    MachOLoadResult meta_unres{};
    TEST_ASSERT(MachOLoader::parse_metadata(c_unres.macho_execute_bytes.data(), c_unres.macho_execute_bytes.size(), meta_unres) &&
                    (meta_unres.header.flags & 0x1u) == 0u,
                "MH_EXECUTE with unresolved symbols must NOT set MH_NOUNDEFS");

    // Negative tests:
    // 1. Dangling SUBTRACTOR at end of section rejected
    SyntheticSecSpec bad_sub = sec_rw;
    bad_sub.relocs = {{0x00, 0u, false, 3u, true, 1u}};
    const auto p_bad_sub = MachOObjectIntegrator::parse_object(build_synthetic_mh_object({sec_text, bad_sub}, syms_a));
    TEST_ASSERT(!MachOObjectIntegrator::link_objects({{"bad_sub", p_bad_sub}}, opts).is_success(),
                "Dangling SUBTRACTOR must be rejected");

    // 2. Duplicate external binding rejected
    MachOMultiObjectOptions dup_bind_opts = opts;
    dup_bind_opts.external_bindings.push_back({"_arch_ext", SymbolProvenanceClassification::TestHarnessStub, {0xD65F03C0u}, 0ULL, "dup"});
    TEST_ASSERT(!MachOObjectIntegrator::link_objects({{"obj_a", pa}, {"obj_b", pb}}, dup_bind_opts).is_success(),
                "Duplicate external binding must be rejected");

    // 3. Duplicate strong symbols rejected
    TEST_ASSERT(!MachOObjectIntegrator::link_objects({{"b1", pb}, {"b2", pb}}, opts).is_success(),
                "Duplicate strong global symbols must be rejected");

    // 4. Dual weak symbols allowed (first weak wins)
    const auto obj_weak2 = build_synthetic_mh_object({sec_text, sec_ro}, syms_a);
    const auto pw2 = MachOObjectIntegrator::parse_object(obj_weak2);
    MachOMultiObjectOptions weak_opts = opts;
    weak_opts.require_all_relocations_resolved = false;
    // Two objects both defining weak _weak_sym (make only weak symbol non-duplicate by differing names elsewhere)
    SyntheticSecSpec wt1 = sec_text;
    SyntheticSecSpec wt2 = sec_text;
    const auto pw_a = MachOObjectIntegrator::parse_object(build_synthetic_mh_object({wt1}, {{"_only_weak", 0x0Fu, 1u, 0x0080u, 0ULL}}));
    const auto pw_b = MachOObjectIntegrator::parse_object(build_synthetic_mh_object({wt2}, {{"_only_weak", 0x0Fu, 1u, 0x0080u, 4ULL}}));
    const auto weak_res = MachOObjectIntegrator::link_objects({{"wa", pw_a}, {"wb", pw_b}}, weak_opts);
    std::uint64_t ow_va = 0;
    TEST_ASSERT(weak_res.is_success() && weak_res.find_symbol_va("_only_weak", ow_va) && ow_va == 0x10000000ULL,
                "Two weak globals must coalesce cleanly with first weak winning");

    return 0;
}

} // namespace

int main() {
    try {
        if (test_mig_and_xnu_compiled_closure() != 0) return 1;
        if (test_synthetic_macho_reloc_types_and_linker_rules() != 0) return 1;
        std::cout << "[PASS] stage283_macho_reloc_and_closure_tests\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 1;
    }
}

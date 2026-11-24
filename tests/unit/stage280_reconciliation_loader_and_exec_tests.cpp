// ============================================================================
// WinIOS Campaign 280: Master Engineering Execution
// Stages 280.19 - 280.21: Semantic Reconciliation, Loader & Boundary Audit,
// and Separated Execution Profiles (Profiles A, B, C, D) in WinIOS CPU
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "crypto/sha256.h"
#include "loader/embedded_xnu_image_210.inl"
#include "loader/kernel_image_parser.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

namespace {

namespace fs = std::filesystem;
using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::memory;

fs::path find_winios_root() {
    for (const fs::path& cand : {fs::current_path(), fs::current_path().parent_path(), fs::path("D:/WinISO26/WinIOS")}) {
        if (fs::exists(cand / "CMakeLists.txt")) {
            return fs::weakly_canonical(cand);
        }
    }
    return fs::path("D:/WinISO26/WinIOS");
}

std::vector<std::uint8_t> read_binary_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    TEST_ASSERT(in.is_open(), "Failed to open file: " + p.string());
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    if (sz > 0) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    }
    return buf;
}

template <typename T>
T read_pod(const std::vector<std::uint8_t>& buf, std::size_t off) {
    TEST_ASSERT(off <= buf.size() && sizeof(T) <= buf.size() - off, "Read out of bounds");
    T val{};
    std::memcpy(&val, buf.data() + off, sizeof(T));
    return val;
}

std::vector<std::uint32_t> extract_text_words(const std::vector<std::uint8_t>& macho_obj) {
    const auto ncmds = read_pod<std::uint32_t>(macho_obj, 16);
    std::size_t off = 32;
    for (std::uint32_t c = 0; c < ncmds; ++c) {
        const auto cmd = read_pod<std::uint32_t>(macho_obj, off);
        const auto cmdsize = read_pod<std::uint32_t>(macho_obj, off + 4);
        if (cmd == 0x19u) {
            const auto nsects = read_pod<std::uint32_t>(macho_obj, off + 64);
            std::size_t soff = off + 72;
            for (std::uint32_t s = 0; s < nsects; ++s) {
                char sectname[17]{}, segname[17]{};
                std::memcpy(sectname, macho_obj.data() + soff, 16);
                std::memcpy(segname, macho_obj.data() + soff + 16, 16);
                if (std::string(segname) == "__TEXT" && std::string(sectname) == "__text") {
                    const auto sz = static_cast<std::size_t>(read_pod<std::uint64_t>(macho_obj, soff + 40));
                    const auto foff = static_cast<std::size_t>(read_pod<std::uint32_t>(macho_obj, soff + 48));
                    TEST_ASSERT((sz % 4u) == 0u, "__text size multiple of 4");
                    std::vector<std::uint32_t> words(sz / 4u);
                    std::memcpy(words.data(), macho_obj.data() + foff, sz);
                    return words;
                }
                soff += 80;
            }
        }
        off += cmdsize;
    }
    TEST_ASSERT(false, "No __TEXT,__text section found");
    return {};
}

// Stage 280.19 - 280.20: Semantic Reconciliation & Honest Loader Boundary
void test_stage280_19_and_20_loader_boundaries() {
    std::cout << "[Stages 280.19-280.20] Testing Real Loader Boundaries on cswitch.o & C279 Object...\n";
    const fs::path root = find_winios_root();
    const auto cswitch_obj = read_binary_file(root.parent_path() / "xnu_probe_isolated" / "gen_build" / "RELEASE_ARM64_OUT" / "cswitch.o");
    const auto c279_obj = read_binary_file(root / "build" / "load_context_darwin.o");

    // KernelImageParser MUST accept genuine MH_EXECUTE kernel and reject MH_OBJECT files
    KernelParsedImage parsed_exec{};
    const auto exec_st = KernelImageParser::parse(kEmbeddedXnuImage210Data, kEmbeddedXnuImage210Size, parsed_exec);
    TEST_ASSERT(exec_st == KernelImageParseStatus::Success, "Embedded MH_EXECUTE kernel accepted");

    KernelParsedImage parsed_cswitch{};
    const auto cswitch_st = KernelImageParser::parse(cswitch_obj.data(), cswitch_obj.size(), parsed_cswitch);
    TEST_ASSERT(cswitch_st == KernelImageParseStatus::UnsupportedFileType,
                "KernelImageParser rejects genuine XNU cswitch.o MH_OBJECT cleanly");

    KernelParsedImage parsed_c279{};
    const auto c279_st = KernelImageParser::parse(c279_obj.data(), c279_obj.size(), parsed_c279);
    TEST_ASSERT(c279_st == KernelImageParseStatus::UnsupportedFileType,
                "KernelImageParser rejects C279 load_context_darwin.o MH_OBJECT cleanly");

    // MachoUserLoader MUST reject unrelocated MH_OBJECT / W^X objects cleanly
    Memory mem(0x800000ULL);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "stage280_loader_boundary");

    const auto load_cswitch = MachoUserLoader::load_image(cswitch_obj, uas, ppm, mem);
    TEST_ASSERT(!load_cswitch.valid, "MachoUserLoader refuses direct loading of cswitch.o MH_OBJECT");

    const auto load_c279 = MachoUserLoader::load_image(c279_obj, uas, ppm, mem);
    TEST_ASSERT(!load_c279.valid, "MachoUserLoader refuses direct loading of C279 MH_OBJECT");

    std::cout << "  [PASS] Stages 280.19-280.20 honest MH_OBJECT loader boundaries verified.\n";
}

// Stage 280.21 Profile A: WinIOS 8-instruction Category C Adaptation
void test_profile_a_winios_adaptation() {
    std::cout << "[Stage 280.21 Profile A] Executing WinIOS Category C Object __TEXT,__text...\n";
    const fs::path root = find_winios_root();
    const auto c279_obj = read_binary_file(root / "build" / "load_context_darwin.o");
    const auto code = extract_text_words(c279_obj);
    TEST_ASSERT(code.size() == 8u, "Profile A object __text is 8 instructions");

    Memory mem(0x800000ULL);
    const std::uint64_t code_base = 0x2000ULL;
    for (std::size_t i = 0; i < code.size(); ++i) {
        mem.write_u32(code_base + i * 4u, code[i]);
    }
    const std::uint64_t cpu_data = 0x70000ULL;
    const std::uint64_t tcb = 0x60000ULL;
    const std::uint64_t target_sp = 0x00000000000A4000ULL;
    const std::uint64_t target_pc = 0x8000ULL;
    const std::uint64_t param = 0x1122334455667788ULL;

    mem.write_u64(tcb + 0x00u, target_sp);
    mem.write_u64(tcb + 0x40u, target_pc);
    mem.write_u64(tcb + 0x48u, param);
    mem.write_u32(target_pc, 0xD503201Fu); // nop

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_tpidr_el1(cpu_data);
    cpu.write_register(0, tcb);
    cpu.write_register(30, 0xDEADBEEFULL);
    cpu.set_program_counter(code_base);

    for (std::size_t step = 0; step < code.size(); ++step) {
        cpu.step(mem);
    }

    TEST_ASSERT(mem.read_u64(cpu_data + 0x10u) == tcb, "Profile A sets cpu_data[+0x10] = TCB");
    TEST_ASSERT(cpu.get_stack_pointer() == target_sp, "Profile A loads SP from TCB+0x00");
    TEST_ASSERT(cpu.read_register(0) == param, "Profile A loads param into X0 from TCB+0x48");
    TEST_ASSERT(cpu.read_register(30) == 0ULL, "Profile A zeroes LR");
    TEST_ASSERT(cpu.get_program_counter() == target_pc, "Profile A branches to continuation PC");
    std::cout << "  [PASS] Profile A Category C execution verified.\n";
}

// Stage 280.21 Profile B: Genuine Upstream cswitch.o Execution
// Executes _machine_set_current_thread and _Call_continuation (x3 == 0 path)
// straight from compiled upstream XNU cswitch.o bytes using assym.s ABI & PAC keys.
void test_profile_b_genuine_xnu_cswitch_execution() {
    std::cout << "[Stage 280.21 Profile B] Executing Genuine Upstream cswitch.o Routines with PAC...\n";
    const fs::path xnu_out = find_winios_root().parent_path() / "xnu_probe_isolated" / "gen_build" / "RELEASE_ARM64_OUT";
    const auto cswitch_obj = read_binary_file(xnu_out / "cswitch.o");
    const auto text_words = extract_text_words(cswitch_obj);
    TEST_ASSERT(text_words.size() == 303u, "Genuine cswitch.o has 303 __text instructions");

    Memory mem(0x800000ULL);
    const std::uint64_t code_base = 0x2000ULL;
    for (std::size_t i = 0; i < text_words.size(); ++i) {
        mem.write_u32(code_base + i * 4u, text_words[i]);
    }

    // Configure A16 PAC keyring
    PacKeyRing keys{};
    keys.apia = {0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL};
    keys.apib = {0x1111222233334444ULL, 0x5555666677778888ULL};
    keys.apda = {0xAAAABBBBCCCCDDDDULL, 0x13579BDF02468ACEULL};
    keys.apdb = {0x9999888877776666ULL, 0x5555444433332222ULL};

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_pac_keyring(keys);

    // Populate authentic assym.s struct offsets in guest RAM
    const std::uint64_t thread_addr = 0x50000ULL;
    const std::uint64_t cpu_data_addr = 0x60000ULL;
    const std::uint64_t raw_kstackptr = 0x70000ULL;
    const std::uint64_t user_tpidr_el0 = 0x0000000100004000ULL;
    const std::uint64_t user_tpidrro = 0x0000000100008000ULL;

    // thread->machine.CpuDatap @ +0x1B8 (440), TH_CTH_SELF @ +0x148 (328)
    mem.write_u64(thread_addr + 0x1B8u, cpu_data_addr);
    mem.write_u64(thread_addr + 0x148u, user_tpidrro);
    // cpu_data->cpu_tpidr_el0 @ +0x398 (920)
    mem.write_u64(cpu_data_addr + 0x398u, user_tpidr_el0);

    // Sign TH_KSTACKPTR @ thread + 0x158 (344) using APDAKey & modifier ((thread + 0x158) | (0x6d42 << 48))
    const std::uint64_t kstack_slot_addr = thread_addr + 0x158u;
    const std::uint64_t kstack_mod = kstack_slot_addr | (0x6D42ULL << 48u);
    const std::uint64_t signed_kstackptr = cpu.get_pac().sign_pointer(raw_kstackptr, kstack_mod, PacKeyType::DataA);
    mem.write_u64(kstack_slot_addr, signed_kstackptr);

    // Step B1: Execute genuine _machine_set_current_thread at +0x49C (8 instructions, ret to 0x8000)
    const std::uint64_t resume_pc = 0x8000ULL;
    mem.write_u32(resume_pc, 0xD503201Fu);
    cpu.write_register(0, thread_addr);
    cpu.write_register(30, resume_pc);
    cpu.set_program_counter(code_base + 0x49Cu);
    for (int i = 0; i < 8; ++i) {
        cpu.step(mem);
    }
    TEST_ASSERT(cpu.get_program_counter() == resume_pc, "_machine_set_current_thread returns cleanly");
    TEST_ASSERT(cpu.get_tpidr_el1() == thread_addr, "Upstream sets TPIDR_EL1 = thread_t (NOT cpu_data)");
    TEST_ASSERT(mem.read_u64(cpu_data_addr + 0x28u) == thread_addr, "cpu_data->cpu_active_thread (+0x28) == thread_t");
    TEST_ASSERT(cpu.get_tpidr_el0() == user_tpidr_el0, "TPIDR_EL0 loaded from cpu_data+0x398");
    TEST_ASSERT(cpu.get_tpidrro_el0() == user_tpidrro, "TPIDRRO_EL0 loaded from thread+0x148");

    // Step B2: Execute genuine _Call_continuation at +0x0C0 with x3 = 0 (skipping _ml_set_interrupts_enabled reloc)
    const std::uint64_t cont_target = 0x9000ULL;
    mem.write_u32(cont_target, 0xD503201Fu);
    const std::uint64_t signed_cont = cpu.get_pac().sign_pointer(cont_target, 0xD507ULL, PacKeyType::InstructionA);
    const std::uint64_t param_val = 0xCAFEBABE1337BEEFULL;
    const std::uint64_t wait_res = 2ULL;

    cpu.write_register(0, signed_cont);
    cpu.write_register(1, param_val);
    cpu.write_register(2, wait_res);
    cpu.write_register(3, 0ULL); // enable_interrupts = 0 -> cbz x3 skips external reloc at +0x114
    cpu.write_register(29, 0xBAD0F9ULL);
    cpu.set_program_counter(code_base + 0x0C0u);

    // Trace max 30 steps until PC hits cont_target
    bool reached = false;
    for (int step = 0; step < 30; ++step) {
        cpu.step(mem);
        if (cpu.get_program_counter() == cont_target) {
            reached = true;
            break;
        }
    }
    TEST_ASSERT(reached, "Genuine _Call_continuation reached continuation target via blraa x20, x21");
    TEST_ASSERT(cpu.get_stack_pointer() == raw_kstackptr, "_Call_continuation authenticated & installed TH_KSTACKPTR SP");
    TEST_ASSERT(cpu.read_register(29) == 0ULL, "_Call_continuation cleared FP (x29) before dispatch");
    TEST_ASSERT(cpu.read_register(0) == param_val, "_Call_continuation delivered parameter in X0");
    TEST_ASSERT(cpu.read_register(1) == wait_res, "_Call_continuation delivered wait_result in X1");

    std::cout << "  [PASS] Profile B genuine upstream _machine_set_current_thread & _Call_continuation verified.\n";
}

// Stage 280.21 Profile C: PAC Tampering Detection & Trap/BRK Verification
void test_profile_c_pac_enforcement() {
    std::cout << "[Stage 280.21 Profile C] Verifying PAC Failure Traps & XPACD/BRK Protection...\n";
    const fs::path xnu_out = find_winios_root().parent_path() / "xnu_probe_isolated" / "gen_build" / "RELEASE_ARM64_OUT";
    const auto text_words = extract_text_words(read_binary_file(xnu_out / "cswitch.o"));

    Memory mem(0x800000ULL);
    const std::uint64_t code_base = 0x2000ULL;
    for (std::size_t i = 0; i < text_words.size(); ++i) {
        mem.write_u32(code_base + i * 4u, text_words[i]);
    }

    PacKeyRing keys{};
    keys.apia = {0x1234ULL, 0x5678ULL};
    keys.apda = {0x9ABCULL, 0xDEF0ULL};

    const std::uint64_t thread_addr = 0x50000ULL;
    const std::uint64_t kstack_mod = (thread_addr + 0x158u) | (0x6D42ULL << 48u);

    // Case C1: Tampered APDA tag in TrapException mode raises immediate AUTDA fault
    {
        CPU cpu;
        cpu.reset();
        cpu.set_current_el(1);
        cpu.set_pac_keyring(keys);
        cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);
        cpu.set_tpidr_el1(thread_addr);
        const std::uint64_t bad_ptr = cpu.get_pac().sign_pointer(0x70000ULL, kstack_mod, PacKeyType::DataA) ^ (1ULL << 52u);
        mem.write_u64(thread_addr + 0x158u, bad_ptr);
        cpu.set_program_counter(code_base + 0x0C0u);

        bool caught = false;
        try {
            for (int i = 0; i < 12; ++i) {
                cpu.step(mem);
            }
        } catch (const std::runtime_error&) {
            caught = true;
        }
        TEST_ASSERT(caught && cpu.get_pac().has_fault(), "Profile C1: TrapException caught corrupted TH_KSTACKPTR PAC");
    }

    // Case C2: Tampered continuation APIA discriminator (!= 0xd507) trapped by blraa x20, x21
    {
        CPU cpu;
        cpu.reset();
        cpu.set_current_el(1);
        cpu.set_pac_keyring(keys);
        cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);
        cpu.set_tpidr_el1(thread_addr);
        mem.write_u64(thread_addr + 0x158u, cpu.get_pac().sign_pointer(0x70000ULL, kstack_mod, PacKeyType::DataA));
        mem.write_u64(thread_addr + 0x1B8u, 0x60000ULL);
        cpu.write_register(0, cpu.get_pac().sign_pointer(0x9000ULL, 0xDEADULL, PacKeyType::InstructionA));
        cpu.write_register(3, 0ULL);
        cpu.set_program_counter(code_base + 0x0C0u);

        bool caught = false;
        try {
            for (int i = 0; i < 30; ++i) {
                cpu.step(mem);
            }
        } catch (const std::runtime_error&) {
            caught = true;
        }
        TEST_ASSERT(caught && cpu.get_pac().has_fault(), "Profile C2: Wrong continuation type discriminator rejected at blraa");
    }

    std::cout << "  [PASS] Profile C PAC authentication enforcement verified.\n";
}

// Stage 280.21 Profile D: Honest Missing-Subsystem & Unlinked-Relocation Boundary
void test_profile_d_honest_blocking_boundaries() {
    std::cout << "[Stage 280.21 Profile D] Verifying Honest Unlinked Relocation & Sysreg Boundaries...\n";
    const fs::path xnu_out = find_winios_root().parent_path() / "xnu_probe_isolated" / "gen_build" / "RELEASE_ARM64_OUT";
    const auto text_words = extract_text_words(read_binary_file(xnu_out / "cswitch.o"));

    Memory mem(0x800000ULL);
    const std::uint64_t code_base = 0x2000ULL;
    for (std::size_t i = 0; i < text_words.size(); ++i) {
        mem.write_u32(code_base + i * 4u, text_words[i]);
    }

    // D1: Calling _Call_continuation with x3 = 1 reaches unlinked self-branch at 0x2114 (bl #0)
    PacKeyRing keys{};
    keys.apda = {0x9ABCULL, 0xDEF0ULL};
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_pac_keyring(keys);
    const std::uint64_t thread_addr = 0x50000ULL;
    mem.write_u64(thread_addr + 0x158u,
                  cpu.get_pac().sign_pointer(0x70000ULL, (thread_addr + 0x158u) | (0x6D42ULL << 48u), PacKeyType::DataA));
    mem.write_u64(thread_addr + 0x1B8u, 0x60000ULL);
    cpu.set_tpidr_el1(thread_addr);
    cpu.write_register(3, 1ULL); // enable_interrupts = 1 triggers external reloc path
    cpu.set_program_counter(code_base + 0x0C0u);

    bool hit_unlinked_reloc = false;
    for (int i = 0; i < 20; ++i) {
        if (cpu.get_program_counter() == code_base + 0x114u) {
            TEST_ASSERT(mem.read_u32(code_base + 0x114u) == 0x94000000u, "Unlinked BRANCH26 site contains imm26=0");
            cpu.step(mem);
            TEST_ASSERT(cpu.get_program_counter() == code_base + 0x114u, "Unlinked bl #0 loops on itself");
            hit_unlinked_reloc = true;
            break;
        }
        cpu.step(mem);
    }
    TEST_ASSERT(hit_unlinked_reloc, "Profile D1 honestly reaches unlinked _ml_set_interrupts_enabled relocation");

    // D2: After Campaign 281 implements FEAT_SSBS, _machine_load_context crosses 0x07C (MSR SSBS, X2)
    // and halts cleanly on 0x080 (LDR d8, [x1, #0x70]) when CPACR_EL1.FPEN == 0, or runs through RETAA (0x0B4)
    // when CPACR_EL1.FPEN == 3 and LR is APIA-signed.
    CPU cpu2;
    cpu2.reset();
    cpu2.set_current_el(1);
    cpu2.set_pac_keyring(keys);
    const std::uint64_t kctx_addr = 0x70000ULL;
    mem.write_u64(thread_addr + 0x158u,
                  cpu2.get_pac().sign_pointer(kctx_addr, (thread_addr + 0x158u) | (0x6D42ULL << 48u), PacKeyType::DataA));
    mem.write_u64(thread_addr + 0x1B8u, 0x60000ULL);
    mem.write_u64(kctx_addr + 0x60u, 0x74000ULL); // SS64_KERNEL_SP
    mem.write_u8(kctx_addr + 0x69u, 1u);         // SSBS = 1
    cpu2.write_register(0, thread_addr);
    cpu2.set_program_counter(code_base + 0x000u);

    bool crossed_ssbs_then_hit_fpen = false;
    try {
        for (int i = 0; i < 40; ++i) {
            cpu2.step(mem);
        }
    } catch (const std::exception&) {
        crossed_ssbs_then_hit_fpen = (cpu2.get_program_counter() == code_base + 0x080u) && cpu2.get_pstate_ssbs();
    }
    TEST_ASSERT(crossed_ssbs_then_hit_fpen,
                "Profile D2 crosses MSR SSBS (+0x07C) and honestly stops at CPACR_EL1.FPEN (+0x080)");
    std::cout << "  [PASS] Profile D honest missing-subsystem & unlinked-relocation boundaries verified.\n";
}

} // namespace

int main() {
    test_stage280_19_and_20_loader_boundaries();
    test_profile_a_winios_adaptation();
    test_profile_b_genuine_xnu_cswitch_execution();
    test_profile_c_pac_enforcement();
    test_profile_d_honest_blocking_boundaries();
    std::cout << "ALL STAGE 280.19 - 280.21 RECONCILIATION, LOADER & EXECUTION PROFILE TESTS PASSED.\n";
    return 0;
}

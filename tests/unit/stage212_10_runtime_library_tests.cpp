#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/guest_dynamic_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::memory;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_runtime_library_loading() {
    std::cout << "[Test 1] Load Genuine ARM64 libwinios_runtime.dylib & Verify Exports...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "runtime_uas");

    GuestDynamicLoader loader(uas, ppm, mem);

    const auto runtime_bin = GuestDynamicLoader::get_embedded_runtime_library();
    TEST_CHECK(!runtime_bin.empty(), "Retrieved embedded libwinios_runtime.dylib");

    LoadedDylibInfo info{};
    std::string err;
    TEST_CHECK(loader.load_library("libwinios_runtime.dylib", runtime_bin, &info, &err),
               "Loaded libwinios_runtime.dylib into guest address space");

    TEST_CHECK(info.base_va != 0, "Base VA is non-zero");

    // Check exported symbols
    const std::uint64_t sym_write = loader.find_exported_symbol("_winios_write");
    const std::uint64_t sym_exit = loader.find_exported_symbol("_winios_exit");
    const std::uint64_t sym_getpid = loader.find_exported_symbol("_winios_getpid");
    const std::uint64_t sym_getppid = loader.find_exported_symbol("_winios_getppid");
    const std::uint64_t sym_yield = loader.find_exported_symbol("_winios_yield");
    const std::uint64_t sym_strlen = loader.find_exported_symbol("_winios_strlen");
    const std::uint64_t sym_print = loader.find_exported_symbol("_winios_print");

    TEST_CHECK(sym_write != 0, "_winios_write exported");
    TEST_CHECK(sym_exit != 0, "_winios_exit exported");
    TEST_CHECK(sym_getpid != 0, "_winios_getpid exported");
    TEST_CHECK(sym_getppid != 0, "_winios_getppid exported");
    TEST_CHECK(sym_yield != 0, "_winios_yield exported");
    TEST_CHECK(sym_strlen != 0, "_winios_strlen exported");
    TEST_CHECK(sym_print != 0, "_winios_print exported");

    // Inspect machine code of _winios_write in guest memory
    const std::uint64_t write_pa = uas.translate(sym_write);
    TEST_CHECK(write_pa != 0, "_winios_write VA translates to physical page");

    const std::uint32_t insn0 = mem.read_u32(static_cast<std::size_t>(write_pa));
    const std::uint32_t insn1 = mem.read_u32(static_cast<std::size_t>(write_pa + 4));
    const std::uint32_t insn2 = mem.read_u32(static_cast<std::size_t>(write_pa + 8));

    TEST_CHECK(insn0 == 0xD2800090u, "First instruction is 'mov x16, #4'");
    TEST_CHECK(insn1 == 0xD4001001u, "Second instruction is 'svc #0x80'");
    TEST_CHECK(insn2 == 0xD65F03C0u, "Third instruction is 'ret'");
}

void test_runtime_strlen_execution() {
    std::cout << "[Test 2] Execute _winios_strlen on Virtual CPU at EL0...\n";

    CPU cpu;
    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "strlen_uas");

    GuestDynamicLoader loader(uas, ppm, mem);
    const auto runtime_bin = GuestDynamicLoader::get_embedded_runtime_library();
    loader.load_library("libwinios_runtime.dylib", runtime_bin);

    const std::uint64_t sym_strlen = loader.find_exported_symbol("_winios_strlen");
    TEST_CHECK(sym_strlen != 0, "Found _winios_strlen VA");

    // Map a data page for test string at VA 0x30000
    const std::uint64_t str_pa = ppm.allocate_page();
    const std::uint64_t str_va = 0x30000ULL;
    uas.map_user_page(str_va, str_pa, PagePermissions::UserReadWrite);

    const std::string test_str = "Hello Darwin!";
    uas.copyin(str_va, test_str.c_str(), test_str.size() + 1); // include null terminator

    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x00300000ULL);

    const std::uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa());
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));

    cpu.set_current_el(0);
    cpu.set_program_counter(sym_strlen);
    cpu.write_register(0, str_va);
    cpu.write_register(30, 0xDEAD0000ULL); // return address sentinel

    // Step CPU until PC returns to sentinel
    std::size_t steps = 0;
    while (cpu.get_program_counter() != 0xDEAD0000ULL && steps < 100) {
        cpu.step(mem);
        ++steps;
    }

    TEST_CHECK(cpu.get_program_counter() == 0xDEAD0000ULL, "_winios_strlen returned");
    TEST_CHECK(cpu.read_register(0) == test_str.size(), "X0 holds correct string length (13)");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 212.10 Minimal Dynamic Runtime Library Tests ===\n";
    test_runtime_library_loading();
    test_runtime_strlen_execution();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 212.10: All runtime library tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 212.10: " << g_failures << " tests failed.\n";
        return 1;
    }
}

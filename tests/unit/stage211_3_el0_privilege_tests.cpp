#include <iostream>
#include <vector>
#include <cstdint>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/virtual_page_table.h"

using namespace winios;
using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_el0_svc_transition_and_eret() {
    std::cout << "[Test 1] Stage 211.3: Real EL0 -> EL1 (SVC) Exception Vectoring & ERET Return...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000); // 2 MiB memory

    const std::uint64_t vbar_addr = 0x8000ULL;
    const std::uint64_t el0_sync_handler_addr = 0x9000ULL;
    const std::uint64_t user_code_addr = 0x10000ULL;

    cpu.set_vbar_el1(vbar_addr);

    // 1. Setup VBAR_EL1 vector table:
    // Offset 0x400 is Lower EL using AArch64 Synchronous vector (SVC)
    // Branch from (vbar + 0x400) = 0x8400 to handler 0x9000:
    // offset = 0x9000 - 0x8400 = 0xC00 bytes = 0x300 instructions.
    // B instruction: 0x14000000 | (0x300 & 0x03FFFFFF) = 0x14000300
    const std::uint32_t b_handler = 0x14000000u | ((0xC00u / 4u) & 0x03FFFFFFu);
    mem.write_u32(static_cast<std::size_t>(vbar_addr + 0x400u), b_handler);

    // 2. Setup handler at 0x9000:
    // [00] add x0, x0, #1 (0x91000400)
    // [04] eret           (0xD69F03E0)
    mem.write_u32(static_cast<std::size_t>(el0_sync_handler_addr), 0x91000400u);
    mem.write_u32(static_cast<std::size_t>(el0_sync_handler_addr + 4u), 0xD69F03E0u);

    // 3. Setup EL0 user code at 0x10000:
    // [00] mov x0, #42    (0xD2800540)
    // [04] svc #0         (0xD4000001)
    // [08] add x0, x0, #5 (0x91001400)
    // [0c] wfi            (0xD503207F)
    mem.write_u32(static_cast<std::size_t>(user_code_addr), 0xD2800540u);
    mem.write_u32(static_cast<std::size_t>(user_code_addr + 4u), 0xD4000001u);
    mem.write_u32(static_cast<std::size_t>(user_code_addr + 8u), 0x91001400u);
    mem.write_u32(static_cast<std::size_t>(user_code_addr + 12u), 0xD503207Fu);

    // Initial state: Start CPU at EL0
    cpu.set_current_el(0);
    cpu.set_program_counter(user_code_addr);
    cpu.set_sp_el0(0x7FFFF000ULL);

    TEST_CHECK(cpu.get_current_el() == 0, "CPU starts at EL0");
    TEST_CHECK(cpu.get_program_counter() == user_code_addr, "PC at user code start");

    // Step 1: execute `mov x0, #42` at EL0
    cpu.step(mem);
    TEST_CHECK(cpu.get_current_el() == 0, "Current EL remains 0 after EL0 mov");
    TEST_CHECK(cpu.read_register(0) == 42, "X0 = 42");
    TEST_CHECK(cpu.get_program_counter() == user_code_addr + 4u, "PC points to SVC");

    // Step 2: execute `svc #0` at EL0 -> synchronous exception entry to EL1
    cpu.step(mem);
    TEST_CHECK(cpu.get_current_el() == 1, "SVC elevates privilege to EL1");
    TEST_CHECK(cpu.get_elr_el1() == user_code_addr + 8u, "ELR_EL1 captured return PC (0x10008)");
    TEST_CHECK((cpu.get_esr_el1() >> 26u) == 0x15u, "ESR_EL1.EC is 0x15 (SvcAArch64)");
    TEST_CHECK(cpu.get_program_counter() == vbar_addr + 0x400u, "PC branched to VBAR_EL1 + 0x400");

    // Step 3: execute branch at VBAR_EL1 + 0x400
    cpu.step(mem);
    TEST_CHECK(cpu.get_program_counter() == el0_sync_handler_addr, "PC at el0_sync_handler (0x9000)");

    // Step 4: execute handler instruction `add x0, x0, #1`
    cpu.step(mem);
    TEST_CHECK(cpu.read_register(0) == 43, "Kernel handler incremented X0 to 43");
    TEST_CHECK(cpu.get_current_el() == 1, "Still in EL1 inside kernel handler");

    // Step 5: execute `eret` at 0x9004
    cpu.step(mem);
    TEST_CHECK(cpu.get_current_el() == 0, "ERET successfully returned CPU to EL0");
    TEST_CHECK(cpu.get_program_counter() == user_code_addr + 8u, "PC restored to 0x10008 in userspace");

    // Step 6: execute `add x0, x0, #5` in userspace
    cpu.step(mem);
    TEST_CHECK(cpu.get_current_el() == 0, "Running at EL0");
    TEST_CHECK(cpu.read_register(0) == 48, "X0 = 48 (43 + 5)");

    // Step 7: execute `wfi`
    cpu.step(mem);
    TEST_CHECK(cpu.is_waiting_for_interrupt(), "EL0 WFI reached cleanly");
}

void test_user_kernel_memory_permission_boundary() {
    std::cout << "[Test 2] Stage 211.3: User vs Kernel Page Permissions & Hardware Boundary...\n";

    // Build descriptors
    const auto u_rx = VirtualPageTable::make_page_descriptor(0x10000ULL, PagePermissions::UserExecute);
    const auto u_rw = VirtualPageTable::make_page_descriptor(0x20000ULL, PagePermissions::UserReadWrite);
    const auto k_rx = VirtualPageTable::make_page_descriptor(0x30000ULL, PagePermissions::KernelExecute);
    const auto k_rw = VirtualPageTable::make_page_descriptor(0x40000ULL, PagePermissions::KernelReadWrite);

    const auto f_u_rx = VirtualPageTable::parse_descriptor(u_rx, 3);
    const auto f_u_rw = VirtualPageTable::parse_descriptor(u_rw, 3);
    const auto f_k_rx = VirtualPageTable::parse_descriptor(k_rx, 3);
    const auto f_k_rw = VirtualPageTable::parse_descriptor(k_rw, 3);

    // User page verifications
    TEST_CHECK(f_u_rx.el0_accessible, "UserExecute is EL0 accessible");
    TEST_CHECK(f_u_rx.user_execute, "UserExecute allows EL0 execution");
    TEST_CHECK(!f_u_rx.writable, "UserExecute is not writable (W^X)");

    TEST_CHECK(f_u_rw.el0_accessible, "UserReadWrite is EL0 accessible");
    TEST_CHECK(f_u_rw.writable, "UserReadWrite is writable");
    TEST_CHECK(!f_u_rw.user_execute, "UserReadWrite forbids execution (W^X)");

    // Kernel page verifications
    TEST_CHECK(!f_k_rx.el0_accessible, "KernelExecute strictly denies EL0 access");
    TEST_CHECK(f_k_rx.privileged_execute, "KernelExecute allows EL1 execution");

    TEST_CHECK(!f_k_rw.el0_accessible, "KernelReadWrite strictly denies EL0 access");
    TEST_CHECK(f_k_rw.writable, "KernelReadWrite is writable at EL1 only");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.3 EL0 Privilege & Exception Tests ===\n";
    test_el0_svc_transition_and_eret();
    test_user_kernel_memory_permission_boundary();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.3: All EL0 privilege and exception tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.3: " << g_failures << " tests failed.\n";
        return 1;
    }
}

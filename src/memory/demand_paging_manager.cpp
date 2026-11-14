#include "memory/demand_paging_manager.h"

namespace winios::memory {

bool DemandPagingManager::initialize_guest_control_data() {
    if (cfg_.config_data_pa + 16 > mem_.size() || cfg_.fault_log_pa + 16 > mem_.size()) {
        return false;
    }

    // Write control parameters for guest EL1 fault handler:
    // [config_data_pa + 0]: l3_table_pa
    // [config_data_pa + 8]: next_phys_page_pa
    mem_.write_u64(static_cast<std::size_t>(cfg_.config_data_pa + 0), cfg_.l3_table_pa);
    mem_.write_u64(static_cast<std::size_t>(cfg_.config_data_pa + 8), cfg_.backing_pool_start_pa);

    // Clear fault statistics in guest RAM
    mem_.write_u64(static_cast<std::size_t>(cfg_.fault_log_pa + 0), 0); // last_fault_address
    mem_.write_u64(static_cast<std::size_t>(cfg_.fault_log_pa + 8), 0); // fault_count
    return true;
}

DemandPagingStats DemandPagingManager::get_guest_stats() const {
    DemandPagingStats stats;
    if (cfg_.fault_log_pa + 16 <= mem_.size()) {
        stats.last_fault_address = mem_.read_u64(static_cast<std::size_t>(cfg_.fault_log_pa + 0));
        stats.total_faults_resolved = mem_.read_u64(static_cast<std::size_t>(cfg_.fault_log_pa + 8));
    }
    if (cfg_.config_data_pa + 16 <= mem_.size()) {
        const std::uint64_t current_pool_pa = mem_.read_u64(static_cast<std::size_t>(cfg_.config_data_pa + 8));
        if (current_pool_pa >= cfg_.backing_pool_start_pa) {
            stats.allocated_backing_pages = (current_pool_pa - cfg_.backing_pool_start_pa) / kPhysicalPageSize16KB;
        }
    }
    return stats;
}

std::vector<std::uint32_t> DemandPagingManager::emit_page_fault_handler_code() {
    // Authentic ARM64 EL1 page fault resolution handler with caller register preservation:
    // 1. Pushes scratch registers X0..X10 onto SP_EL1
    // 2. Reads FAR_EL1 & ESR_EL1
    // 3. Checks EC: if Lower EL (0x24), sets AP=0b01 (User RW); else AP=0b00 (Kernel RW)
    // 4. Extracts 16KB page frame base and computes L3 table entry index
    // 5. Reads L3 table base & next backing page from [0x40080..0x40088]
    // 6. Builds Level 3 Page Descriptor (Normal WB, AF=1, SH=3, Page=3)
    // 7. Stores descriptor into Level 3 table in guest RAM
    // 8. Advances backing physical page pointer by 16KB
    // 9. Logs fault address to [0x40010] and increments counter at [0x40018]
    // 10. Executes TLBI VMALLE1IS, DSB ISH, ISB
    // 11. Pops scratch registers X0..X10 from SP_EL1
    // 12. Executes ERET to resume faulting instruction with intact register state
    return {
        0xA9BF07E0u, // [00] stp x0, x1, [sp, #-16]!
        0xA9BF0FE2u, // [04] stp x2, x3, [sp, #-16]!
        0xA9BF17E4u, // [08] stp x4, x5, [sp, #-16]!
        0xA9BF1FE6u, // [0c] stp x6, x7, [sp, #-16]!
        0xA9BF27E8u, // [10] stp x8, x9, [sp, #-16]!
        0xF81F0FEAu, // [14] str x10, [sp, #-16]!
        0xD5386000u, // [18] mrs x0, far_el1
        0xD5385201u, // [1c] mrs x1, esr_el1
        0xD35AFC22u, // [20] lsr x2, x1, #26
        0xD287FFE3u, // [24] mov x3, #0x3FFF
        0x8A230004u, // [28] bic x4, x0, x3
        0xD34EFC85u, // [2c] lsr x5, x4, #14
        0x924028A5u, // [30] and x5, x5, #0x7FF
        0xD37DF0A5u, // [34] lsl x5, x5, #3
        0xD2A00086u, // [38] mov x6, #0x40000
        0xF94040C7u, // [3c] ldr x7, [x6, #128]  (0x40080: l3_table_pa)
        0xF94044C8u, // [40] ldr x8, [x6, #136]  (0x40088: next_phys_page_pa)
        0xF100905Fu, // [44] cmp x2, #0x24       (EC == Lower EL Data Abort?)
        0x54000061u, // [48] b.ne .Lkernel_ap
        0xD280E869u, // [4c] mov x9, #0x743      (User RW: AP=0b01 | AF | SH | Page)
        0x14000002u, // [50] b .Ldo_orr
        0xD280E069u, // [54] mov x9, #0x703      (Kernel RW: AP=0b00 | AF | SH | Page)
        0xAA090109u, // [58] orr x9, x8, x9
        0xF82568E9u, // [5c] str x9, [x7, x5]    (write L3 table entry in RAM)
        0x91401108u, // [60] add x8, x8, #0x4000 (advance 16KB)
        0xF90044C8u, // [64] str x8, [x6, #136]
        0xF90008C0u, // [68] str x0, [x6, #16]   (0x40010: last_fault_va)
        0xF9400CCAu, // [6c] ldr x10, [x6, #24]  (0x40018: fault_count)
        0x9100054Au, // [70] add x10, x10, #1
        0xF9000CCAu, // [74] str x10, [x6, #24]
        0xD508831Fu, // [78] tlbi vmalle1is
        0xD5033B9Fu, // [7c] dsb ish
        0xD5033FDFu, // [80] isb
        0xF84107EAu, // [84] ldr x10, [sp], #16
        0xA8C127E8u, // [88] ldp x8, x9, [sp], #16
        0xA8C11FE6u, // [8c] ldp x6, x7, [sp], #16
        0xA8C117E4u, // [90] ldp x4, x5, [sp], #16
        0xA8C10FE2u, // [94] ldp x2, x3, [sp], #16
        0xA8C107E0u, // [98] ldp x0, x1, [sp], #16
        0xD69F03E0u  // [9c] eret
    };
}

bool DemandPagingManager::install_vector_table(std::uint64_t vbar_base) {
    if ((vbar_base % 2048) != 0 || vbar_base + 2048 > mem_.size()) {
        return false;
    }

    // Default vector table filler: infinite loop (b .)
    const std::uint32_t hang_insn = 0x14000000u;
    for (std::size_t offset = 0; offset < 2048; offset += 4) {
        mem_.write_u32(static_cast<std::size_t>(vbar_base + offset), hang_insn);
    }

    auto handler = emit_page_fault_handler_code();

    // Install at Current EL with SPx Sync (offset 0x200)
    for (std::size_t i = 0; i < handler.size(); ++i) {
        mem_.write_u32(static_cast<std::size_t>(vbar_base + 0x200u + (i * 4)), handler[i]);
    }

    // Install at Lower EL AArch64 Sync (offset 0x400)
    for (std::size_t i = 0; i < handler.size(); ++i) {
        mem_.write_u32(static_cast<std::size_t>(vbar_base + 0x400u + (i * 4)), handler[i]);
    }

    return true;
}

} // namespace winios::memory

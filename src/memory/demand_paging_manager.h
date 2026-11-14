#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "memory/virtual_page_table.h"

namespace winios::memory {

struct DemandPagingConfig {
    std::uint64_t root_l2_table_pa{0x40000ULL};
    std::uint64_t l3_table_pa{0x60000ULL};
    std::uint64_t backing_pool_start_pa{0x100000ULL};
    std::uint64_t config_data_pa{0x40080ULL};
    std::uint64_t fault_log_pa{0x40010ULL};
};

struct DemandPagingStats {
    std::uint64_t total_faults_resolved{0};
    std::uint64_t last_fault_address{0};
    std::uint64_t allocated_backing_pages{0};
};

class DemandPagingManager {
public:
    explicit DemandPagingManager(Memory& mem, const DemandPagingConfig& config = {})
        : mem_(mem), cfg_(config) {}
    ~DemandPagingManager() = default;

    // Initializes control data slots in guest memory (stores L3 table address & initial backing PA)
    bool initialize_guest_control_data();

    // Queries stats directly from guest RAM written by the EL1 handler
    [[nodiscard]] DemandPagingStats get_guest_stats() const;

    // Generates the complete ARM64 EL1 demand page-fault handler machine code
    [[nodiscard]] static std::vector<std::uint32_t> emit_page_fault_handler_code();

    // Installs a complete 2KB ARM64 vector table at vbar_base in guest memory with demand page fault resolution
    bool install_vector_table(std::uint64_t vbar_base);

private:
    Memory& mem_;
    DemandPagingConfig cfg_;
};

} // namespace winios::memory

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "cpu/synchronous_exception.h"

namespace mmu {
class MMU;
}

namespace winios {
class SmpExclusiveMonitorManager;
}

namespace winios::memory {

// -----------------------------------------------------------------------------
// UnifiedMemoryView
//
// Centralized guest memory abstraction uniting:
//  - Stage 1 and Stage 2 MMU address translation
//  - SCTLR_EL1.A alignment enforcement and natural alignment checks
//  - Multi-byte accesses spanning 16KB granule page boundaries
//  - Normal vs Device memory attributes and permissions
//  - Architectural DC ZVA block clearing and cache maintenance validation
//  - Physical memory access and exclusive reservation invalidation
// -----------------------------------------------------------------------------
class UnifiedMemoryView {
public:
    UnifiedMemoryView(Memory& memory,
                      mmu::MMU* mmu = nullptr,
                      unsigned current_el = 0,
                      bool architectural = true,
                      std::uint64_t instruction_address = 0,
                      std::uint32_t instruction = 0,
                      bool is_atomic = false);

    [[nodiscard]] std::size_t size() const noexcept { return memory_.size(); }
    [[nodiscard]] Memory& raw_memory() noexcept { return memory_; }
    [[nodiscard]] const Memory& raw_memory() const noexcept { return memory_; }

    void set_current_el(unsigned el) noexcept { current_el_ = el; }
    [[nodiscard]] unsigned current_el() const noexcept { return current_el_; }

    void set_core_id(std::size_t cid) noexcept { core_id_ = cid; }
    [[nodiscard]] std::size_t core_id() const noexcept { return core_id_; }

    void set_architectural(bool enabled) noexcept { architectural_ = enabled; }
    [[nodiscard]] bool architectural() const noexcept { return architectural_; }

    void set_instruction_context(std::uint64_t addr, std::uint32_t insn = 0, bool is_atomic = false) noexcept {
        instruction_address_ = addr;
        instruction_ = insn;
        is_atomic_ = is_atomic;
    }

    void set_mmu(mmu::MMU* mmu) noexcept { mmu_ = mmu; }
    [[nodiscard]] mmu::MMU* get_mmu() const noexcept { return mmu_; }

    void set_exclusive_monitor(SmpExclusiveMonitorManager* mon) noexcept { monitor_ = mon; }
    [[nodiscard]] SmpExclusiveMonitorManager* exclusive_monitor() const noexcept { return monitor_; }

    void set_store_mutex(std::recursive_mutex* mutex) noexcept { store_mutex_ = mutex; }

    // Address translation and validation
    [[nodiscard]] std::uint64_t page_mask_for_va(std::uint64_t va) const noexcept;

    std::uint64_t translate_or_fault(std::uint64_t address,
                                     unsigned alignment_width,
                                     unsigned check_bytes,
                                     AccessType access,
                                     bool require_natural_align = false,
                                     bool unaligned_override = false,
                                     bool reject_device = false) const;

    void translate_access_pages(std::uint64_t address,
                                unsigned width,
                                AccessType access,
                                bool enforce_alignment,
                                bool require_natural_align,
                                bool reject_device,
                                std::uint64_t& pa1,
                                unsigned& first_bytes,
                                std::uint64_t& pa2,
                                unsigned& second_bytes) const;

    // Virtual memory accessors (architectural translation + permission/alignment checks)
    [[nodiscard]] std::uint8_t read_u8(std::uint64_t va) const;
    [[nodiscard]] std::uint16_t read_u16(std::uint64_t va) const;
    [[nodiscard]] std::uint32_t read_u32(std::uint64_t va) const;
    [[nodiscard]] std::uint64_t read_u64(std::uint64_t va) const;
    void read_bytes(std::uint64_t va, std::uint8_t* dst, std::size_t size) const;

    void write_u8(std::uint64_t va, std::uint8_t value);
    void write_u16(std::uint64_t va, std::uint16_t value);
    void write_u32(std::uint64_t va, std::uint32_t value);
    void write_u64(std::uint64_t va, std::uint64_t value);
    void write_bytes(std::uint64_t va, const std::uint8_t* src, std::size_t size);

    // Direct physical memory accessors (bounds-checked)
    [[nodiscard]] std::uint8_t read_phys_u8(std::uint64_t pa) const;
    [[nodiscard]] std::uint16_t read_phys_u16(std::uint64_t pa) const;
    [[nodiscard]] std::uint32_t read_phys_u32(std::uint64_t pa) const;
    [[nodiscard]] std::uint64_t read_phys_u64(std::uint64_t pa) const;
    void read_phys_bytes(std::uint64_t pa, std::uint8_t* dst, std::size_t size) const;

    void write_phys_u8(std::uint64_t pa, std::uint8_t value);
    void write_phys_u16(std::uint64_t pa, std::uint16_t value);
    void write_phys_u32(std::uint64_t pa, std::uint32_t value);
    void write_phys_u64(std::uint64_t pa, std::uint64_t value);
    void write_phys_bytes(std::uint64_t pa, const std::uint8_t* src, std::size_t size);

    // Cache operations
    void cache_zero_block(std::uint64_t block_va, std::size_t size = 64);
    void check_cache_range(std::uint64_t va, std::size_t size, AccessType access, bool reject_device = false) const;

    // Reservation invalidation
    void invalidate_reservation(std::size_t core_id, std::uint64_t pa, std::size_t size) noexcept;

private:
    [[noreturn]] void fault(std::uint64_t address, AccessType access,
                            FaultStatus status, unsigned width) const;
    void check_address_size_and_range(std::uint64_t address, unsigned check_bytes,
                                      AccessType access) const;

    Memory& memory_;
    mmu::MMU* mmu_{nullptr};
    SmpExclusiveMonitorManager* monitor_{nullptr};
    std::recursive_mutex* store_mutex_{nullptr};
    std::size_t core_id_{0};
    unsigned current_el_{0};
    bool architectural_{true};
    std::uint64_t instruction_address_{0};
    std::uint32_t instruction_{0};
    bool is_atomic_{false};
};

} // namespace winios::memory

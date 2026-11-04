#pragma once

#include "memory/memory.h"
#include "cpu/smp_exclusive_monitor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace winios {

// -----------------------------------------------------------------------------
// CoreMemoryTelemetry: Diagnostic access counters per virtual core
// -----------------------------------------------------------------------------
struct CoreMemoryTelemetry {
    std::uint64_t reads{0};
    std::uint64_t writes{0};
    std::uint64_t bytes_read{0};
    std::uint64_t bytes_written{0};
};

// -----------------------------------------------------------------------------
// SmpSharedMemory: Centralized physical memory architecture for SMP virtual cores
//
// Features:
// - Single shared physical memory address space across all cores
// - Core-tagged read/write accesses
// - Automatic notification of SmpExclusiveMonitorManager on stores
// - Per-core diagnostic telemetry (reads/writes/bytes)
// - 100% architectural memory identity across cores
// -----------------------------------------------------------------------------
class SmpSharedMemory {
public:
    explicit SmpSharedMemory(std::size_t size_bytes, std::size_t max_cores = 16);
    explicit SmpSharedMemory(std::shared_ptr<Memory> memory, std::size_t max_cores = 16);

    [[nodiscard]] std::size_t size() const noexcept { return memory_->size(); }
    [[nodiscard]] Memory& raw_memory() noexcept { return *memory_; }
    [[nodiscard]] const Memory& raw_memory() const noexcept { return *memory_; }
    [[nodiscard]] std::shared_ptr<Memory> shared_memory_ptr() const noexcept { return memory_; }

    void attach_exclusive_monitor(SmpExclusiveMonitorManager* monitor) noexcept {
        exclusive_monitor_ = monitor;
    }
    [[nodiscard]] SmpExclusiveMonitorManager* exclusive_monitor() const noexcept {
        return exclusive_monitor_;
    }

    // Core-tagged memory operations
    std::uint8_t read_u8(std::size_t core_id, std::uint64_t address);
    void write_u8(std::size_t core_id, std::uint64_t address, std::uint8_t value);

    std::uint16_t read_u16(std::size_t core_id, std::uint64_t address);
    void write_u16(std::size_t core_id, std::uint64_t address, std::uint16_t value);

    std::uint32_t read_u32(std::size_t core_id, std::uint64_t address);
    void write_u32(std::size_t core_id, std::uint64_t address, std::uint32_t value);

    std::uint64_t read_u64(std::size_t core_id, std::uint64_t address);
    void write_u64(std::size_t core_id, std::uint64_t address, std::uint64_t value);

    void read_bytes(std::size_t core_id, std::uint64_t address, void* dst, std::size_t size);
    void write_bytes(std::size_t core_id, std::uint64_t address, const void* src, std::size_t size);

    // Exclusive store through monitor: invalidates other reservations and commits write
    bool write_exclusive_u8(std::size_t core_id, std::uint64_t address, std::uint8_t value);
    bool write_exclusive_u16(std::size_t core_id, std::uint64_t address, std::uint16_t value);
    bool write_exclusive_u32(std::size_t core_id, std::uint64_t address, std::uint32_t value);
    bool write_exclusive_u64(std::size_t core_id, std::uint64_t address, std::uint64_t value);

    // Per-core telemetry
    [[nodiscard]] CoreMemoryTelemetry get_telemetry(std::size_t core_id) const;
    void reset_telemetry() noexcept;

private:
    void ensure_core_telemetry(std::size_t core_id) const;

    std::shared_ptr<Memory> memory_;
    SmpExclusiveMonitorManager* exclusive_monitor_{nullptr};
    mutable std::vector<CoreMemoryTelemetry> telemetry_;
};

} // namespace winios

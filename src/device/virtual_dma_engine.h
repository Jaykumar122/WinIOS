#pragma once

#include "device/virtual_dma_types.h"
#include "cpu/smp_shared_memory.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

namespace winios::device {

// -----------------------------------------------------------------------------
// VirtualDmaEngineResult: Structured outcome of a descriptor submission
// -----------------------------------------------------------------------------
struct VirtualDmaEngineResult {
    VirtualDmaDescriptorStatus status{VirtualDmaDescriptorStatus::Pending};
    std::uint64_t descriptors_processed{0};
    std::uint64_t bytes_transferred{0};
    std::string message;

    [[nodiscard]] bool is_success() const noexcept {
        return status == VirtualDmaDescriptorStatus::Success;
    }
    [[nodiscard]] bool is_failure() const noexcept {
        return status != VirtualDmaDescriptorStatus::Success;
    }
};

// -----------------------------------------------------------------------------
// VirtualDmaEngine: Deterministic, bounded memory-to-memory virtual DMA processor
// -----------------------------------------------------------------------------
class VirtualDmaEngine {
public:
    explicit VirtualDmaEngine(std::shared_ptr<SmpSharedMemory> shared_memory,
                              VirtualDmaLimits limits = {});

    [[nodiscard]] const VirtualDmaLimits& limits() const noexcept { return limits_; }
    void set_limits(const VirtualDmaLimits& limits) noexcept { limits_ = limits; }

    void set_shared_memory(std::shared_ptr<SmpSharedMemory> shared_memory) noexcept {
        shared_memory_ = std::move(shared_memory);
    }

    [[nodiscard]] bool is_enabled() const noexcept { return enabled_; }
    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }

    // Execute descriptor chain starting at guest physical address
    VirtualDmaEngineResult execute_descriptor_chain(std::uint64_t descriptor_paddr,
                                                    std::size_t core_id = 0);

    // Single descriptor validation without execution
    [[nodiscard]] VirtualDmaDescriptorStatus validate_descriptor(const VirtualDmaDescriptor& desc,
                                                                 std::size_t memory_size) const noexcept;

    // Load descriptor from guest physical memory
    [[nodiscard]] bool read_descriptor(std::uint64_t descriptor_paddr,
                                       VirtualDmaDescriptor& out_desc,
                                       std::size_t core_id = 0) const noexcept;

    // Write updated status into descriptor in guest physical memory
    void write_descriptor_status(std::uint64_t descriptor_paddr,
                                 VirtualDmaDescriptorStatus status,
                                 std::size_t core_id = 0) noexcept;

    // Telemetry & metrics
    [[nodiscard]] std::uint64_t total_submissions() const noexcept { return total_submissions_; }
    [[nodiscard]] std::uint64_t total_descriptors_processed() const noexcept { return total_descriptors_processed_; }
    [[nodiscard]] std::uint64_t total_bytes_transferred() const noexcept { return total_bytes_transferred_; }
    [[nodiscard]] std::uint64_t total_rejected_submissions() const noexcept { return total_rejected_submissions_; }

    void reset_telemetry() noexcept;

private:
    std::shared_ptr<SmpSharedMemory> shared_memory_;
    VirtualDmaLimits limits_;
    bool enabled_{true};

    std::uint64_t total_submissions_{0};
    std::uint64_t total_descriptors_processed_{0};
    std::uint64_t total_bytes_transferred_{0};
    std::uint64_t total_rejected_submissions_{0};
};

} // namespace winios::device

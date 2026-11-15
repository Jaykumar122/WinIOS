#pragma once

#include "device/virtual_dma_types.h"
#include "device/virtual_iommu.h"
#include "cpu/gicv3.h"
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

    void set_iommu(std::shared_ptr<VirtualIommu> iommu) noexcept { iommu_ = std::move(iommu); }
    [[nodiscard]] std::shared_ptr<VirtualIommu> iommu() const noexcept { return iommu_; }

    void set_addressing_mode(DmaAddressingMode mode) noexcept { addressing_mode_ = mode; }
    [[nodiscard]] DmaAddressingMode addressing_mode() const noexcept { return addressing_mode_; }

    void set_default_device_id(DeviceId id) noexcept { default_device_id_ = id; }
    [[nodiscard]] DeviceId default_device_id() const noexcept { return default_device_id_; }

    // Interrupt controller integration
    void set_gic(std::shared_ptr<GICv3> gic, std::uint32_t irq = 48) noexcept {
        gic_ = std::move(gic);
        completion_irq_ = irq;
        completion_irq_enabled_ = (gic_ != nullptr && irq >= 32);
    }
    [[nodiscard]] std::shared_ptr<GICv3> gic() const noexcept { return gic_; }
    void set_completion_irq_enabled(bool enabled) noexcept { completion_irq_enabled_ = enabled; }
    [[nodiscard]] bool is_completion_irq_enabled() const noexcept { return completion_irq_enabled_; }
    [[nodiscard]] std::uint32_t completion_irq() const noexcept { return completion_irq_; }
    void set_completion_irq(std::uint32_t irq) noexcept { completion_irq_ = irq; }

    void trigger_completion_irq() noexcept {
        if (gic_ && completion_irq_enabled_ && completion_irq_ >= 32) {
            gic_->set_spi_level(completion_irq_, true);
        }
    }

    void acknowledge_completion_irq() noexcept {
        if (gic_ && completion_irq_enabled_ && completion_irq_ >= 32) {
            gic_->set_spi_level(completion_irq_, false);
        }
    }

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
    std::shared_ptr<VirtualIommu> iommu_;
    std::shared_ptr<GICv3> gic_{nullptr};
    DmaAddressingMode addressing_mode_{DmaAddressingMode::Physical};
    DeviceId default_device_id_{0};
    std::uint32_t completion_irq_{48};
    bool completion_irq_enabled_{false};
    VirtualDmaLimits limits_;
    bool enabled_{true};

    std::uint64_t total_submissions_{0};
    std::uint64_t total_descriptors_processed_{0};
    std::uint64_t total_bytes_transferred_{0};
    std::uint64_t total_rejected_submissions_{0};
};

} // namespace winios::device

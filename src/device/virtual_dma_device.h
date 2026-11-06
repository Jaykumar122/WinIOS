#pragma once

#include "device/virtual_device.h"
#include "device/virtual_dma_engine.h"
#include "device/virtual_dma_types.h"
#include "sndl/sndl_v2_recorder.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace winios::device {

// -----------------------------------------------------------------------------
// VirtualDmaDevice Register Offsets
// -----------------------------------------------------------------------------
namespace dma_reg {
inline constexpr std::uint64_t kControl           = 0x00; // Control register
inline constexpr std::uint64_t kStatus            = 0x08; // Status register
inline constexpr std::uint64_t kDescriptorAddr    = 0x10; // Descriptor physical address
inline constexpr std::uint64_t kTransferCount     = 0x18; // Completed transfer count
inline constexpr std::uint64_t kBytesTransferred  = 0x20; // Completed byte count
inline constexpr std::uint64_t kErrorCode         = 0x28; // Last error code
inline constexpr std::uint64_t kInterruptId       = 0x30; // Interrupt line ID
inline constexpr std::uint64_t kDeviceId          = 0x38; // Device identification magic
} // namespace dma_reg

// Control Register Bits
inline constexpr std::uint64_t kControlEnable     = 1ULL << 0;
inline constexpr std::uint64_t kControlStart      = 1ULL << 1;
inline constexpr std::uint64_t kControlReset      = 1ULL << 2;
inline constexpr std::uint64_t kControlIrqEnable  = 1ULL << 3;

// Status Register Bits
inline constexpr std::uint64_t kStatusBusy        = 1ULL << 0;
inline constexpr std::uint64_t kStatusComplete    = 1ULL << 1;
inline constexpr std::uint64_t kStatusError       = 1ULL << 2;

inline constexpr std::uint32_t kDefaultDmaDeviceIdMagic = 0x56444D41u; // 'VDMA'

// -----------------------------------------------------------------------------
// VirtualDmaDevice: Memory-Mapped Synthetic DMA Controller
// -----------------------------------------------------------------------------
class VirtualDmaDevice : public VirtualDevice {
public:
    VirtualDmaDevice(std::uint32_t device_id,
                     std::uint64_t mmio_base,
                     std::uint64_t mmio_size,
                     std::shared_ptr<SmpSharedMemory> shared_memory,
                     std::uint32_t irq_line = 48,
                     VirtualDmaLimits limits = {});

    VirtualDmaDevice(std::uint64_t mmio_base,
                     std::uint64_t mmio_size,
                     std::uint32_t irq_line = 48)
        : VirtualDmaDevice(kDefaultDmaDeviceIdMagic, mmio_base, mmio_size, nullptr, irq_line) {}

    void set_shared_memory(std::shared_ptr<SmpSharedMemory> mem) noexcept {
        shared_memory_ = mem;
        engine_.set_shared_memory(mem);
    }

    // VirtualDevice interface
    [[nodiscard]] std::uint64_t read_register(std::uint64_t offset, unsigned width) override;
    void write_register(std::uint64_t offset, std::uint64_t value, unsigned width) override;
    void reset() noexcept override;

    [[nodiscard]] std::vector<std::uint8_t> serialize_state() const override;
    [[nodiscard]] bool deserialize_state(const std::uint8_t* data, std::size_t size) override;

    // Attach SNDL recorder and capability config
    void attach_sndl_recorder(sndl::SndlV2Recorder* recorder) noexcept { sndl_recorder_ = recorder; }
    void detach_sndl_recorder() noexcept { sndl_recorder_ = nullptr; }
    void set_capabilities(const VirtualDeviceCapabilities& caps) noexcept { capabilities_ = caps; }
    [[nodiscard]] const VirtualDeviceCapabilities& capabilities() const noexcept { return capabilities_; }

    // Status query
    [[nodiscard]] bool is_busy() const noexcept override {
        return (reg_status_ & kStatusBusy) != 0 || status_ == VirtualDeviceStatus::Active;
    }

    // Direct access to DMA engine
    [[nodiscard]] VirtualDmaEngine& engine() noexcept { return engine_; }
    [[nodiscard]] const VirtualDmaEngine& engine() const noexcept { return engine_; }

    // Explicit submission trigger
    VirtualDmaEngineResult trigger_transfer(std::uint64_t desc_addr, std::size_t core_id = 0);

private:
    void execute_submission(std::size_t core_id = 0);

    VirtualDmaEngine engine_;
    std::shared_ptr<SmpSharedMemory> shared_memory_;
    VirtualDeviceCapabilities capabilities_;
    sndl::SndlV2Recorder* sndl_recorder_{nullptr};

    std::uint64_t reg_control_{kControlEnable};
    std::uint64_t reg_status_{0};
    std::uint64_t reg_descriptor_addr_{0};
    std::uint64_t reg_transfer_count_{0};
    std::uint64_t reg_bytes_transferred_{0};
    std::uint64_t reg_error_code_{0};
    std::uint64_t reg_interrupt_id_{48};
};

} // namespace winios::device

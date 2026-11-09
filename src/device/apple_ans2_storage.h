#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "device/virtual_device.h"
#include "device/virtual_device_types.h"
#include "memory/memory.h"

namespace winios::device {

// -----------------------------------------------------------------------------
// AppleANS2StorageDevice: Apple NAND Subsystem 2 (ANS2) / NVMe Controller
// Emulates the high-speed internal flash storage controller for iPhone 15 / A16:
// - Standard Apple ARM-IO MMIO base: 0x0C000000ULL, size: 64 KB
// - Native 4096-byte (4 KB) Apple Silicon LBA block size
// - Direct DMA transfer into guest physical RAM
// - Support for both MMIO fast DMA command engine and NVMe ASQ/ACQ doorbells
// - DeviceTree binding: "apple,ans2", "apple,nvme"
// - IOKit driver match: AppleANS2NVMeController
// -----------------------------------------------------------------------------
class AppleANS2StorageDevice : public VirtualDevice {
public:
    static constexpr std::uint64_t kDefaultAns2Base     = 0x0C000000ULL;
    static constexpr std::uint64_t kDefaultAns2Size     = 0x00010000ULL; // 64 KB MMIO window
    static constexpr std::uint32_t kDefaultAns2Irq      = 48u;
    static constexpr std::uint32_t kDefaultSectorSize   = 4096u; // 4 KB Apple LBA
    static constexpr std::uint64_t kDefaultStorageBytes = 64ULL * 1024ULL * 1024ULL; // 64 MB default capacity

    // NVMe 1.4 / ANS2 Controller Registers
    static constexpr std::uint64_t kRegCap         = 0x0000u; // Controller Capabilities (64-bit)
    static constexpr std::uint64_t kRegVersion     = 0x0008u; // Version (32-bit: 0x00010400 = 1.4)
    static constexpr std::uint64_t kRegIntMaskSet  = 0x000Cu; // Interrupt Mask Set
    static constexpr std::uint64_t kRegIntMaskClr  = 0x0010u; // Interrupt Mask Clear
    static constexpr std::uint64_t kRegControl     = 0x0014u; // Controller Configuration (CC)
    static constexpr std::uint64_t kRegStatus      = 0x001Cu; // Controller Status (CSTS: RDY bit 0)
    static constexpr std::uint64_t kRegAdminSqBase = 0x0028u; // Admin Submission Queue Base (64-bit)
    static constexpr std::uint64_t kRegAdminCqBase = 0x0030u; // Admin Completion Queue Base (64-bit)

    // Direct MMIO DMA Command Interface (Hardware Accelerator for guest boot)
    static constexpr std::uint64_t kRegCmdLba      = 0x0100u; // Starting LBA (64-bit)
    static constexpr std::uint64_t kRegCmdBlockCnt = 0x0108u; // Block Count (32-bit)
    static constexpr std::uint64_t kRegCmdDmaAddr  = 0x0110u; // Target Physical RAM DMA Address (64-bit)
    static constexpr std::uint64_t kRegCmdTrigger  = 0x0118u; // 1 = Read, 2 = Write, 3 = Flush, 4 = Identify
    static constexpr std::uint64_t kRegCmdStatus   = 0x0120u; // 0 = Idle/Success, 1 = Busy, 2 = Error

    // NVMe Doorbells
    static constexpr std::uint64_t kRegSq0TailDb   = 0x1000u; // Admin SQ Tail Doorbell
    static constexpr std::uint64_t kRegCq0HeadDb   = 0x1004u; // Admin CQ Head Doorbell

    // Command Codes
    static constexpr std::uint32_t kCmdNone        = 0u;
    static constexpr std::uint32_t kCmdRead        = 1u;
    static constexpr std::uint32_t kCmdWrite       = 2u;
    static constexpr std::uint32_t kCmdFlush       = 3u;
    static constexpr std::uint32_t kCmdIdentify    = 4u;

    explicit AppleANS2StorageDevice(
        std::uint32_t device_id = 0x414E5332u, // 'ANS2'
        std::uint64_t mmio_base = kDefaultAns2Base,
        std::uint64_t mmio_size = kDefaultAns2Size,
        std::uint32_t irq_line = kDefaultAns2Irq,
        std::size_t capacity_bytes = kDefaultStorageBytes,
        std::uint64_t ram_base = 0x80000000ULL);

    ~AppleANS2StorageDevice() override = default;

    // Attach physical guest RAM for DMA transactions
    void attach_memory(Memory* memory) noexcept { memory_ = memory; }

    // Load disk image into backing storage
    bool load_image(const std::uint8_t* data, std::size_t size);
    [[nodiscard]] const std::vector<std::uint8_t>& backing_data() const noexcept { return backing_data_; }
    [[nodiscard]] std::size_t total_capacity_bytes() const noexcept { return backing_data_.size(); }
    [[nodiscard]] std::uint64_t total_lba_count() const noexcept {
        return backing_data_.size() / sector_size_;
    }
    [[nodiscard]] std::uint32_t sector_size() const noexcept { return sector_size_; }

    // VirtualDevice Interface
    std::uint64_t read_register(std::uint64_t offset, unsigned width) override;
    void write_register(std::uint64_t offset, std::uint64_t value, unsigned width) override;
    void reset() noexcept override;

    std::vector<std::uint8_t> serialize_state() const override;
    bool deserialize_state(const std::uint8_t* data, std::size_t size) override;

    // Direct Block I/O APIs
    bool read_blocks_dma(std::uint64_t lba, std::uint32_t block_count, std::uint64_t dma_paddr);
    bool write_blocks_dma(std::uint64_t lba, std::uint32_t block_count, std::uint64_t dma_paddr);

    // Diagnostics
    [[nodiscard]] std::uint64_t total_dma_bytes_transferred() const noexcept { return total_dma_bytes_; }
    [[nodiscard]] std::uint64_t total_io_operations() const noexcept { return total_io_ops_; }
    [[nodiscard]] bool is_controller_ready() const noexcept { return (reg_status_ & 0x01u) != 0; }
    [[nodiscard]] bool is_interrupt_asserted() const noexcept { return interrupt_asserted_; }
    void clear_interrupt() noexcept {
        std::lock_guard<std::mutex> guard(lock_);
        interrupt_asserted_ = false;
        trigger_interrupt(false);
    }

private:
    void execute_dma_command();

    mutable std::mutex lock_;
    Memory* memory_{nullptr};
    std::uint64_t ram_base_{0x80000000ULL};
    std::uint32_t sector_size_{kDefaultSectorSize};

    // Controller Registers
    std::uint64_t reg_cap_{0x000000000001003FULL}; // MPSMIN=0 (4KB), CSS=1, TO=0, DSTRD=0
    std::uint32_t reg_version_{0x00010400u};       // NVMe 1.4
    std::uint32_t reg_int_mask_{0u};
    std::uint32_t reg_control_{0u};
    std::uint32_t reg_status_{0x01u};              // CSTS: Ready = 1
    std::uint64_t reg_asq_{0u};
    std::uint64_t reg_acq_{0u};

    // Command Interface Registers
    std::uint64_t cmd_lba_{0u};
    std::uint32_t cmd_block_cnt_{1u};
    std::uint64_t cmd_dma_addr_{0u};
    std::uint32_t cmd_status_{0u};

    // Statistics
    std::uint64_t total_dma_bytes_{0u};
    std::uint64_t total_io_ops_{0u};
    bool interrupt_asserted_{false};

    // Backing Storage (NAND Flash emulation)
    std::vector<std::uint8_t> backing_data_;
};

} // namespace winios::device

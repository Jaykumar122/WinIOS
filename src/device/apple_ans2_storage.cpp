#include "device/apple_ans2_storage.h"

#include <algorithm>
#include <cstring>

namespace winios::device {

AppleANS2StorageDevice::AppleANS2StorageDevice(
    std::uint32_t device_id,
    std::uint64_t mmio_base,
    std::uint64_t mmio_size,
    std::uint32_t irq_line,
    std::size_t capacity_bytes,
    std::uint64_t ram_base)
    : VirtualDevice(device_id, "AppleANS2Storage", VirtualDeviceType::SyntheticStorage, mmio_base, mmio_size, irq_line),
      ram_base_(ram_base),
      backing_data_(capacity_bytes, 0) {
    status_ = VirtualDeviceStatus::Idle;
    reset();
}

void AppleANS2StorageDevice::reset() noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    reg_control_ = 0;
    reg_status_ = 0x01u; // CSTS: Ready = 1
    reg_int_mask_ = 0;
    reg_asq_ = 0;
    reg_acq_ = 0;

    cmd_lba_ = 0;
    cmd_block_cnt_ = 1;
    cmd_dma_addr_ = 0;
    cmd_status_ = 0;

    status_ = VirtualDeviceStatus::Idle;
    interrupt_asserted_ = false;
    trigger_interrupt(false);
}

bool AppleANS2StorageDevice::load_image(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) return false;
    std::lock_guard<std::mutex> guard(lock_);
    backing_data_.assign(data, data + size);

    // Pad to multiple of sector size
    const std::size_t remainder = backing_data_.size() % sector_size_;
    if (remainder != 0) {
        backing_data_.resize(backing_data_.size() + (sector_size_ - remainder), 0);
    }
    return true;
}

std::uint64_t AppleANS2StorageDevice::read_register(std::uint64_t offset, unsigned width) {
    std::lock_guard<std::mutex> guard(lock_);
    (void)width;

    switch (offset) {
    case kRegCap:
        return reg_cap_;
    case kRegVersion:
        return reg_version_;
    case kRegIntMaskSet:
    case kRegIntMaskClr:
        return reg_int_mask_;
    case kRegControl:
        return reg_control_;
    case kRegStatus:
        return reg_status_;
    case kRegAdminSqBase:
        return reg_asq_;
    case kRegAdminCqBase:
        return reg_acq_;

    case kRegCmdLba:
        return cmd_lba_;
    case kRegCmdBlockCnt:
        return cmd_block_cnt_;
    case kRegCmdDmaAddr:
        return cmd_dma_addr_;
    case kRegCmdStatus:
        return cmd_status_;

    default:
        return 0ULL;
    }
}

void AppleANS2StorageDevice::write_register(std::uint64_t offset, std::uint64_t value, unsigned width) {
    std::lock_guard<std::mutex> guard(lock_);
    (void)width;

    switch (offset) {
    case kRegIntMaskSet:
        reg_int_mask_ |= static_cast<std::uint32_t>(value);
        break;
    case kRegIntMaskClr:
        reg_int_mask_ &= ~static_cast<std::uint32_t>(value);
        break;
    case kRegControl: {
        reg_control_ = static_cast<std::uint32_t>(value);
        const bool enabled = (reg_control_ & 0x01u) != 0;
        if (enabled) {
            reg_status_ |= 0x01u; // Ready
        } else {
            reg_status_ &= ~0x01u; // Not Ready
        }
        break;
    }
    case kRegAdminSqBase:
        reg_asq_ = value;
        break;
    case kRegAdminCqBase:
        reg_acq_ = value;
        break;

    case kRegCmdLba:
        cmd_lba_ = value;
        break;
    case kRegCmdBlockCnt:
        cmd_block_cnt_ = static_cast<std::uint32_t>(value);
        break;
    case kRegCmdDmaAddr:
        cmd_dma_addr_ = value;
        break;
    case kRegCmdTrigger: {
        const auto trig = static_cast<std::uint32_t>(value);
        if (trig == kCmdRead) {
            if (read_blocks_dma(cmd_lba_, cmd_block_cnt_, cmd_dma_addr_)) {
                cmd_status_ = 0; // Success
                interrupt_asserted_ = true;
                trigger_interrupt(true);
            } else {
                cmd_status_ = 2; // Error
            }
        } else if (trig == kCmdWrite) {
            if (write_blocks_dma(cmd_lba_, cmd_block_cnt_, cmd_dma_addr_)) {
                cmd_status_ = 0; // Success
                interrupt_asserted_ = true;
                trigger_interrupt(true);
            } else {
                cmd_status_ = 2; // Error
            }
        } else if (trig == kCmdFlush) {
            cmd_status_ = 0;
        }
        break;
    }

    default:
        break;
    }
}

bool AppleANS2StorageDevice::read_blocks_dma(std::uint64_t lba, std::uint32_t block_count, std::uint64_t dma_paddr) {
    if (block_count == 0) return true;
    if (!memory_) return false;

    const std::uint64_t byte_count = static_cast<std::uint64_t>(block_count) * sector_size_;
    const std::uint64_t disk_offset = lba * sector_size_;

    if (disk_offset + byte_count > backing_data_.size()) {
        return false;
    }

    // Physical RAM bounds check
    if (dma_paddr < ram_base_ || (dma_paddr - ram_base_ + byte_count) > memory_->size()) {
        return false;
    }

    const std::uint64_t mem_offset = dma_paddr - ram_base_;
    std::memcpy(memory_->raw_data() + mem_offset, backing_data_.data() + disk_offset, static_cast<std::size_t>(byte_count));

    total_dma_bytes_ += byte_count;
    total_io_ops_++;
    return true;
}

bool AppleANS2StorageDevice::write_blocks_dma(std::uint64_t lba, std::uint32_t block_count, std::uint64_t dma_paddr) {
    if (block_count == 0) return true;
    if (!memory_) return false;

    const std::uint64_t byte_count = static_cast<std::uint64_t>(block_count) * sector_size_;
    const std::uint64_t disk_offset = lba * sector_size_;

    if (disk_offset + byte_count > backing_data_.size()) {
        return false;
    }

    // Physical RAM bounds check
    if (dma_paddr < ram_base_ || (dma_paddr - ram_base_ + byte_count) > memory_->size()) {
        return false;
    }

    const std::uint64_t mem_offset = dma_paddr - ram_base_;
    std::memcpy(backing_data_.data() + disk_offset, memory_->raw_data() + mem_offset, static_cast<std::size_t>(byte_count));

    total_dma_bytes_ += byte_count;
    total_io_ops_++;
    return true;
}

std::vector<std::uint8_t> AppleANS2StorageDevice::serialize_state() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<std::uint8_t> out;
    out.reserve(64 + backing_data_.size());

    const std::uint32_t magic = 0x414E5332u;
    const std::uint32_t version = 1u;

    auto append_u32 = [&](std::uint32_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        out.insert(out.end(), p, p + 4);
    };
    auto append_u64 = [&](std::uint64_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        out.insert(out.end(), p, p + 8);
    };

    append_u32(magic);
    append_u32(version);
    append_u32(sector_size_);
    append_u32(reg_control_);
    append_u32(reg_status_);
    append_u32(reg_int_mask_);
    append_u64(reg_asq_);
    append_u64(reg_acq_);
    append_u64(cmd_lba_);
    append_u32(cmd_block_cnt_);
    append_u64(cmd_dma_addr_);
    append_u32(cmd_status_);

    const std::uint64_t disk_sz = backing_data_.size();
    append_u64(disk_sz);
    out.insert(out.end(), backing_data_.begin(), backing_data_.end());

    return out;
}

bool AppleANS2StorageDevice::deserialize_state(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 64) return false;
    std::lock_guard<std::mutex> guard(lock_);

    const auto* p = data;
    auto read_u32 = [&]() -> std::uint32_t {
        std::uint32_t v = 0;
        std::memcpy(&v, p, 4);
        p += 4;
        return v;
    };
    auto read_u64 = [&]() -> std::uint64_t {
        std::uint64_t v = 0;
        std::memcpy(&v, p, 8);
        p += 8;
        return v;
    };

    const std::uint32_t magic = read_u32();
    const std::uint32_t version = read_u32();
    if (magic != 0x414E5332u || version != 1u) return false;

    sector_size_ = read_u32();
    reg_control_ = read_u32();
    reg_status_ = read_u32();
    reg_int_mask_ = read_u32();
    reg_asq_ = read_u64();
    reg_acq_ = read_u64();
    cmd_lba_ = read_u64();
    cmd_block_cnt_ = read_u32();
    cmd_dma_addr_ = read_u64();
    cmd_status_ = read_u32();

    const std::uint64_t disk_sz = read_u64();
    if (p + disk_sz > data + size) return false;

    backing_data_.assign(p, p + disk_sz);
    return true;
}

} // namespace winios::device

#include "device/virtual_dma_device.h"
#include "trace/trace_serializer.h"

#include <cstring>

namespace winios::device {

namespace {
inline constexpr std::uint64_t kDmaSnapshotMagic = 0x56444D41534E4150ull; // 'VDMASNAP'
inline constexpr std::size_t kDmaSnapshotPayloadSize = 64;
} // namespace

VirtualDmaDevice::VirtualDmaDevice(std::uint32_t device_id,
                                   std::uint64_t mmio_base,
                                   std::uint64_t mmio_size,
                                   std::shared_ptr<SmpSharedMemory> shared_memory,
                                   std::uint32_t irq_line,
                                   VirtualDmaLimits limits)
    : VirtualDevice(device_id, "VirtualDmaDevice", VirtualDeviceType::VirtualDma, mmio_base, mmio_size, irq_line),
      engine_(shared_memory, limits),
      shared_memory_(std::move(shared_memory)),
      reg_interrupt_id_(irq_line) {}

void VirtualDmaDevice::reset() noexcept {
    reg_control_ = kControlEnable;
    reg_status_ = 0;
    reg_descriptor_addr_ = 0;
    reg_transfer_count_ = 0;
    reg_bytes_transferred_ = 0;
    reg_error_code_ = 0;
    engine_.reset_telemetry();
    set_status(VirtualDeviceStatus::Idle);
}

std::uint64_t VirtualDmaDevice::read_register(std::uint64_t offset, unsigned width) {
    (void)width;
    switch (offset) {
    case dma_reg::kControl:          return reg_control_;
    case dma_reg::kStatus:           return reg_status_;
    case dma_reg::kDescriptorAddr:   return reg_descriptor_addr_;
    case dma_reg::kTransferCount:    return reg_transfer_count_;
    case dma_reg::kBytesTransferred: return reg_bytes_transferred_;
    case dma_reg::kErrorCode:        return reg_error_code_;
    case dma_reg::kInterruptId:      return reg_interrupt_id_;
    case dma_reg::kDeviceId:         return kDefaultDmaDeviceIdMagic;
    default:                         return 0;
    }
}

void VirtualDmaDevice::write_register(std::uint64_t offset, std::uint64_t value, unsigned width) {
    (void)width;
    switch (offset) {
    case dma_reg::kControl:
        if ((value & kControlReset) != 0) {
            reset();
            return;
        }
        reg_control_ = value & (kControlEnable | kControlIrqEnable);
        set_enabled((reg_control_ & kControlEnable) != 0);
        if ((value & kControlStart) != 0) {
            execute_submission(0);
        }
        break;

    case dma_reg::kStatus:
        // Guest can write 1 to complete/error bits to acknowledge/clear
        if ((value & kStatusComplete) != 0) {
            reg_status_ &= ~kStatusComplete;
        }
        if ((value & kStatusError) != 0) {
            reg_status_ &= ~kStatusError;
            reg_error_code_ = 0;
        }
        break;

    case dma_reg::kDescriptorAddr:
        reg_descriptor_addr_ = value;
        break;

    case dma_reg::kInterruptId:
        reg_interrupt_id_ = value;
        irq_line_ = static_cast<std::uint32_t>(value);
        break;

    default:
        break;
    }
}

VirtualDmaEngineResult VirtualDmaDevice::trigger_transfer(std::uint64_t desc_addr, std::size_t core_id) {
    reg_descriptor_addr_ = desc_addr;
    execute_submission(core_id);

    VirtualDmaEngineResult res;
    if ((reg_status_ & kStatusComplete) != 0) {
        res.status = VirtualDmaDescriptorStatus::Success;
    } else {
        res.status = static_cast<VirtualDmaDescriptorStatus>(reg_error_code_);
    }
    return res;
}

void VirtualDmaDevice::execute_submission(std::size_t core_id) {
    if (!is_enabled()) {
        reg_status_ |= kStatusError;
        reg_error_code_ = static_cast<std::uint64_t>(VirtualDmaDescriptorStatus::ErrorDeviceDisabled);
        if (sndl_recorder_ && sndl_recorder_->is_recording()) {
            sndl_recorder_->record_dma_submission_rejected(core_id, device_id_, reg_descriptor_addr_,
                static_cast<std::uint32_t>(VirtualDmaDescriptorStatus::ErrorDeviceDisabled));
        }
        return;
    }

    reg_status_ |= kStatusBusy;
    reg_status_ &= ~(kStatusComplete | kStatusError | 0xF0ull);

    if (!sndl_recorder_ || !sndl_recorder_->is_recording()) {
        // Direct execution without recording
        const auto res = engine_.execute_descriptor_chain(reg_descriptor_addr_, core_id);
        reg_status_ &= ~kStatusBusy;

        if (res.is_success()) {
            reg_status_ |= kStatusComplete;
            reg_transfer_count_ += res.descriptors_processed;
            reg_bytes_transferred_ += res.bytes_transferred;
            reg_error_code_ = 0;
            if ((reg_control_ & kControlIrqEnable) != 0) {
                trigger_interrupt(true);
            }
        } else {
            reg_status_ |= kStatusError;
            reg_status_ |= (static_cast<std::uint64_t>(res.status) & 0x0Fu) << 4;
            reg_error_code_ = static_cast<std::uint64_t>(res.status);
        }
        return;
    }

    // --- Deterministic 7-Step Recording Pipeline ---
    if (!shared_memory_) {
        reg_status_ &= ~kStatusBusy;
        reg_status_ |= kStatusError;
        reg_error_code_ = static_cast<std::uint64_t>(VirtualDmaDescriptorStatus::ErrorInvalidAddress);
        sndl_recorder_->record_dma_submission_rejected(core_id, device_id_, reg_descriptor_addr_,
            static_cast<std::uint32_t>(VirtualDmaDescriptorStatus::ErrorInvalidAddress));
        return;
    }

    const std::size_t mem_sz = shared_memory_->size();
    const auto& limits = engine_.limits();

    // Step 1: Validate complete descriptor chain structure
    std::vector<VirtualDmaDescriptor> descs;
    std::vector<std::uint64_t> desc_addrs;
    std::uint64_t current_desc_addr = reg_descriptor_addr_;

    VirtualDmaDescriptorStatus error_status = VirtualDmaDescriptorStatus::Success;
    std::uint64_t failed_desc_addr = 0;

    while (true) {
        if ((current_desc_addr & 7u) != 0) {
            error_status = VirtualDmaDescriptorStatus::ErrorMisaligned;
            failed_desc_addr = current_desc_addr;
            break;
        }
        if (current_desc_addr > mem_sz || sizeof(VirtualDmaDescriptor) > mem_sz - current_desc_addr) {
            error_status = VirtualDmaDescriptorStatus::ErrorOutOfBounds;
            failed_desc_addr = current_desc_addr;
            break;
        }
        if (std::find(desc_addrs.begin(), desc_addrs.end(), current_desc_addr) != desc_addrs.end()) {
            error_status = VirtualDmaDescriptorStatus::ErrorCyclicChain;
            failed_desc_addr = current_desc_addr;
            break;
        }
        if (desc_addrs.size() >= limits.max_descriptors_per_chain) {
            error_status = VirtualDmaDescriptorStatus::ErrorChainTooDeep;
            failed_desc_addr = current_desc_addr;
            break;
        }

        desc_addrs.push_back(current_desc_addr);
        VirtualDmaDescriptor desc{};
        if (!engine_.read_descriptor(current_desc_addr, desc, core_id)) {
            error_status = VirtualDmaDescriptorStatus::ErrorInvalidAddress;
            failed_desc_addr = current_desc_addr;
            break;
        }

        // Step 2: Validate guest memory ranges and descriptor fields
        const auto val_res = engine_.validate_descriptor(desc, mem_sz);
        if (val_res != VirtualDmaDescriptorStatus::Success) {
            error_status = val_res;
            failed_desc_addr = current_desc_addr;
            break;
        }

        descs.push_back(desc);

        if ((desc.flags & kDmaFlagChainNext) != 0 && desc.next_descriptor_paddr != 0) {
            current_desc_addr = desc.next_descriptor_paddr;
        } else {
            break;
        }
    }

    if (error_status != VirtualDmaDescriptorStatus::Success) {
        reg_status_ &= ~kStatusBusy;
        reg_status_ |= kStatusError;
        reg_status_ |= (static_cast<std::uint64_t>(error_status) & 0x0Fu) << 4;
        reg_error_code_ = static_cast<std::uint64_t>(error_status);
        if (failed_desc_addr != 0) {
            engine_.write_descriptor_status(failed_desc_addr, error_status, core_id);
        }
        if (sndl_recorder_) {
            sndl_recorder_->record_dma_submission_rejected(core_id, device_id_, failed_desc_addr ? failed_desc_addr : reg_descriptor_addr_,
                static_cast<std::uint32_t>(error_status));
        }
        return;
    }

    // Step 3: Capture replay-sufficient input data (pre-commit)
    std::vector<std::vector<std::uint8_t>> captured_payloads(descs.size());
    for (std::size_t i = 0; i < descs.size(); ++i) {
        const auto& desc = descs[i];
        if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
            captured_payloads[i].resize(static_cast<std::size_t>(desc.length_bytes));
            shared_memory_->read_bytes(core_id, desc.src_paddr, captured_payloads[i].data(), captured_payloads[i].size());
        }
    }

    // Step 4: Verify recording capacity
    if (sndl_recorder_ && sndl_recorder_->dropped_events() > 0) {
        reg_status_ &= ~kStatusBusy;
        reg_status_ |= kStatusError;
        reg_error_code_ = static_cast<std::uint64_t>(VirtualDmaDescriptorStatus::ErrorDeviceDisabled);
        sndl_recorder_->record_dma_submission_rejected(core_id, device_id_, reg_descriptor_addr_,
            static_cast<std::uint32_t>(VirtualDmaDescriptorStatus::ErrorDeviceDisabled));
        return;
    }

    // Step 5 & 6: Prepare and commit the memory transaction
    std::uint64_t total_bytes = 0;
    for (std::size_t i = 0; i < descs.size(); ++i) {
        const auto& desc = descs[i];
        if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
            shared_memory_->write_bytes(core_id, desc.dst_paddr, captured_payloads[i].data(), captured_payloads[i].size());
        } else if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Fill)) {
            std::vector<std::uint8_t> fill_buf(static_cast<std::size_t>(desc.length_bytes));
            const std::uint32_t pat = desc.fill_pattern;
            const std::uint8_t pat_bytes[4] = {
                static_cast<std::uint8_t>(pat & 0xFFu),
                static_cast<std::uint8_t>((pat >> 8u) & 0xFFu),
                static_cast<std::uint8_t>((pat >> 16u) & 0xFFu),
                static_cast<std::uint8_t>((pat >> 24u) & 0xFFu)
            };
            for (std::size_t b = 0; b < fill_buf.size(); ++b) {
                fill_buf[b] = pat_bytes[b % 4];
            }
            shared_memory_->write_bytes(core_id, desc.dst_paddr, fill_buf.data(), fill_buf.size());
        }

        // Invalidate exclusive reservations across all cores for DMA destination buffer
        if (shared_memory_->exclusive_monitor()) {
            shared_memory_->exclusive_monitor()->invalidate_reservations(
                shared_memory_->exclusive_monitor()->max_cores(),
                desc.dst_paddr,
                static_cast<std::size_t>(desc.length_bytes)
            );
        }

        // Commit success status to descriptor in guest RAM
        engine_.write_descriptor_status(desc_addrs[i], VirtualDmaDescriptorStatus::Success, core_id);
        total_bytes += desc.length_bytes;
    }

    reg_status_ &= ~kStatusBusy;
    reg_status_ |= kStatusComplete;
    reg_transfer_count_ += descs.size();
    reg_bytes_transferred_ += total_bytes;
    reg_error_code_ = 0;

    // Step 7: Record deterministic completion results
    if (sndl_recorder_) {
        for (std::size_t i = 0; i < descs.size(); ++i) {
            const auto& desc = descs[i];
            const std::uint32_t extra = (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Fill))
                ? desc.fill_pattern
                : trace::calculate_crc32c(captured_payloads[i].data(), captured_payloads[i].size());

            sndl_recorder_->record_dma_submission_accepted(
                core_id,
                device_id_,
                desc_addrs[i],
                desc.dst_paddr,
                desc.length_bytes,
                desc.operation_type,
                extra
            );

            if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
                sndl_recorder_->record_dma_copy_payload(
                    desc.descriptor_id,
                    desc.dst_paddr,
                    captured_payloads[i].data(),
                    captured_payloads[i].size()
                );

                // Emit inline chunks for transfers <= 256 bytes
                if (desc.length_bytes <= 256) {
                    const std::size_t chunk_size = 16;
                    std::uint16_t c_idx = 0;
                    for (std::size_t off = 0; off < captured_payloads[i].size(); off += chunk_size, ++c_idx) {
                        const std::size_t this_chunk = std::min<std::size_t>(chunk_size, captured_payloads[i].size() - off);
                        sndl_recorder_->record_dma_chunk(
                            core_id,
                            device_id_,
                            desc.dst_paddr + off,
                            captured_payloads[i].data() + off,
                            static_cast<std::uint16_t>(this_chunk),
                            c_idx
                        );
                    }
                }
            }
        }

        sndl_recorder_->record_dma_transfer_completed(
            core_id,
            device_id_,
            reg_descriptor_addr_,
            total_bytes,
            static_cast<std::uint32_t>(descs.size())
        );
    }

    if ((reg_control_ & kControlIrqEnable) != 0) {
        if (sndl_recorder_) {
            sndl_recorder_->record_device_interrupt_asserted(core_id, device_id_, irq_line_);
        }
        trigger_interrupt(true);
    }
}

std::vector<std::uint8_t> VirtualDmaDevice::serialize_state() const {
    std::vector<std::uint8_t> buf(kDmaSnapshotPayloadSize, 0);

    trace::encode_u64_le(buf.data() + 0, kDmaSnapshotMagic);
    trace::encode_u64_le(buf.data() + 8, reg_control_);
    trace::encode_u64_le(buf.data() + 16, reg_status_);
    trace::encode_u64_le(buf.data() + 24, reg_descriptor_addr_);
    trace::encode_u64_le(buf.data() + 32, reg_transfer_count_);
    trace::encode_u64_le(buf.data() + 40, reg_bytes_transferred_);
    trace::encode_u32_le(buf.data() + 48, static_cast<std::uint32_t>(reg_error_code_));
    trace::encode_u32_le(buf.data() + 52, static_cast<std::uint32_t>(reg_interrupt_id_));
    trace::encode_u32_le(buf.data() + 56, device_id_);

    const std::uint32_t crc = trace::calculate_crc32c(buf.data(), 60);
    trace::encode_u32_le(buf.data() + 60, crc);

    return buf;
}

bool VirtualDmaDevice::deserialize_state(const std::uint8_t* data, std::size_t size) {
    if (!data || size < kDmaSnapshotPayloadSize) return false;

    const std::uint64_t magic = trace::decode_u64_le(data + 0);
    if (magic != kDmaSnapshotMagic) return false;

    const std::uint32_t expected_crc = trace::calculate_crc32c(data, 60);
    const std::uint32_t actual_crc = trace::decode_u32_le(data + 60);
    if (expected_crc != actual_crc) return false;

    reg_control_ = trace::decode_u64_le(data + 8);
    reg_status_ = trace::decode_u64_le(data + 16);
    if ((reg_status_ & kStatusBusy) != 0) {
        // Refuse to restore snapshot captured in mid-DMA busy state
        return false;
    }
    reg_descriptor_addr_ = trace::decode_u64_le(data + 24);
    reg_transfer_count_ = trace::decode_u64_le(data + 32);
    reg_bytes_transferred_ = trace::decode_u64_le(data + 40);
    reg_error_code_ = trace::decode_u32_le(data + 48);
    reg_interrupt_id_ = trace::decode_u32_le(data + 52);
    irq_line_ = static_cast<std::uint32_t>(reg_interrupt_id_);
    device_id_ = trace::decode_u32_le(data + 56);

    set_enabled((reg_control_ & kControlEnable) != 0);
    return true;
}

} // namespace winios::device

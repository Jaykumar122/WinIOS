#include "device/virtual_dma_engine.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace winios::device {

VirtualDmaEngine::VirtualDmaEngine(std::shared_ptr<SmpSharedMemory> shared_memory,
                                   VirtualDmaLimits limits)
    : shared_memory_(std::move(shared_memory)),
      limits_(limits) {}

void VirtualDmaEngine::reset_telemetry() noexcept {
    total_submissions_ = 0;
    total_descriptors_processed_ = 0;
    total_bytes_transferred_ = 0;
    total_rejected_submissions_ = 0;
}

bool VirtualDmaEngine::read_descriptor(std::uint64_t descriptor_paddr,
                                       VirtualDmaDescriptor& out_desc,
                                       std::size_t core_id) const noexcept {
    if (!shared_memory_) return false;
    const std::size_t mem_sz = shared_memory_->size();

    // Alignment: must be 8-byte aligned
    if ((descriptor_paddr & 7u) != 0) {
        return false;
    }

    // Bounds: must fit 64 bytes in guest memory
    if (descriptor_paddr > mem_sz || sizeof(VirtualDmaDescriptor) > mem_sz - descriptor_paddr) {
        return false;
    }

    shared_memory_->read_bytes(core_id, descriptor_paddr, &out_desc, sizeof(VirtualDmaDescriptor));
    return true;
}

void VirtualDmaEngine::write_descriptor_status(std::uint64_t descriptor_paddr,
                                              VirtualDmaDescriptorStatus status,
                                              std::size_t core_id) noexcept {
    if (!shared_memory_) return;
    const std::size_t mem_sz = shared_memory_->size();

    if ((descriptor_paddr & 7u) != 0) return;
    if (descriptor_paddr > mem_sz || sizeof(VirtualDmaDescriptor) > mem_sz - descriptor_paddr) return;

    // Offset of status field within VirtualDmaDescriptor:
    // descriptor_id (8) + src_paddr (8) + dst_paddr (8) + length_bytes (8) + op (4) + flags (4) = 40
    const std::uint64_t status_addr = descriptor_paddr + offsetof(VirtualDmaDescriptor, status);
    shared_memory_->write_u32(core_id, status_addr, static_cast<std::uint32_t>(status));
}

VirtualDmaDescriptorStatus VirtualDmaEngine::validate_descriptor(const VirtualDmaDescriptor& desc,
                                                                 std::size_t memory_size) const noexcept {
    // 1. Operation type
    if (desc.operation_type != static_cast<std::uint32_t>(VirtualDmaOpType::Copy) &&
        desc.operation_type != static_cast<std::uint32_t>(VirtualDmaOpType::Fill)) {
        return VirtualDmaDescriptorStatus::ErrorUnsupportedOp;
    }

    // 2. Length check: non-zero and within limits
    if (desc.length_bytes == 0 || desc.length_bytes > limits_.max_transfer_bytes) {
        return VirtualDmaDescriptorStatus::ErrorLengthLimit;
    }

    // 3. Address arithmetic overflow
    if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
        if (desc.src_paddr + desc.length_bytes < desc.src_paddr) {
            return VirtualDmaDescriptorStatus::ErrorAddressOverflow;
        }
    }
    if (desc.dst_paddr + desc.length_bytes < desc.dst_paddr) {
        return VirtualDmaDescriptorStatus::ErrorAddressOverflow;
    }

    // 4. Memory bounds (for physical mode only; IOVA bounds are checked via IOMMU)
    const bool is_iommu = (addressing_mode_ == DmaAddressingMode::IommuTranslated) ||
                          ((desc.flags & kDmaFlagIommuTranslate) != 0);
    if (!is_iommu) {
        if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
            if (desc.src_paddr > memory_size || desc.length_bytes > memory_size - desc.src_paddr) {
                return VirtualDmaDescriptorStatus::ErrorOutOfBounds;
            }
        }
        if (desc.dst_paddr > memory_size || desc.length_bytes > memory_size - desc.dst_paddr) {
            return VirtualDmaDescriptorStatus::ErrorOutOfBounds;
        }
    }

    return VirtualDmaDescriptorStatus::Success;
}

VirtualDmaEngineResult VirtualDmaEngine::execute_descriptor_chain(std::uint64_t descriptor_paddr,
                                                                std::size_t core_id) {
    total_submissions_++;

    if (!enabled_) {
        total_rejected_submissions_++;
        return {VirtualDmaDescriptorStatus::ErrorDeviceDisabled, 0, 0, "Virtual DMA engine is disabled"};
    }

    if (!shared_memory_) {
        total_rejected_submissions_++;
        return {VirtualDmaDescriptorStatus::ErrorInvalidAddress, 0, 0, "No shared memory attached to DMA engine"};
    }

    const std::size_t mem_sz = shared_memory_->size();
    std::vector<std::uint64_t> visited_addrs;
    std::uint64_t current_desc_addr = descriptor_paddr;

    std::uint64_t total_processed = 0;
    std::uint64_t total_bytes = 0;

    std::vector<std::uint8_t> bounce_buffer;

    while (true) {
        // Alignment check
        if ((current_desc_addr & 7u) != 0) {
            total_rejected_submissions_++;
            return {VirtualDmaDescriptorStatus::ErrorMisaligned, total_processed, total_bytes, "Descriptor address misaligned"};
        }

        // Memory bounds check for descriptor itself
        if (current_desc_addr > mem_sz || sizeof(VirtualDmaDescriptor) > mem_sz - current_desc_addr) {
            total_rejected_submissions_++;
            return {VirtualDmaDescriptorStatus::ErrorOutOfBounds, total_processed, total_bytes, "Descriptor address outside guest RAM"};
        }

        // Cycle check
        if (std::find(visited_addrs.begin(), visited_addrs.end(), current_desc_addr) != visited_addrs.end()) {
            total_rejected_submissions_++;
            write_descriptor_status(current_desc_addr, VirtualDmaDescriptorStatus::ErrorCyclicChain, core_id);
            return {VirtualDmaDescriptorStatus::ErrorCyclicChain, total_processed, total_bytes, "Cyclic descriptor chain detected"};
        }

        // Chain depth check
        if (visited_addrs.size() >= limits_.max_descriptors_per_chain) {
            total_rejected_submissions_++;
            write_descriptor_status(current_desc_addr, VirtualDmaDescriptorStatus::ErrorChainTooDeep, core_id);
            return {VirtualDmaDescriptorStatus::ErrorChainTooDeep, total_processed, total_bytes, "Descriptor chain depth limit exceeded"};
        }

        visited_addrs.push_back(current_desc_addr);

        VirtualDmaDescriptor desc{};
        if (!read_descriptor(current_desc_addr, desc, core_id)) {
            total_rejected_submissions_++;
            return {VirtualDmaDescriptorStatus::ErrorInvalidAddress, total_processed, total_bytes, "Failed to read descriptor"};
        }

        // Validate descriptor
        const auto val_status = validate_descriptor(desc, mem_sz);
        if (val_status != VirtualDmaDescriptorStatus::Success) {
            total_rejected_submissions_++;
            write_descriptor_status(current_desc_addr, val_status, core_id);
            return {val_status, total_processed, total_bytes, "Descriptor validation failed"};
        }

        std::uint64_t effective_src_pa = desc.src_paddr;
        std::uint64_t effective_dst_pa = desc.dst_paddr;

        const bool is_iommu = (addressing_mode_ == DmaAddressingMode::IommuTranslated) ||
                              ((desc.flags & kDmaFlagIommuTranslate) != 0);

        if (is_iommu) {
            if (!iommu_) {
                total_rejected_submissions_++;
                write_descriptor_status(current_desc_addr, VirtualDmaDescriptorStatus::ErrorIommuNotAttached, core_id);
                return {VirtualDmaDescriptorStatus::ErrorIommuNotAttached, total_processed, total_bytes, "IOMMU not attached"};
            }

            const DeviceId dev_id = (desc.reserved != 0) ? static_cast<DeviceId>(desc.reserved) : default_device_id_;

            auto map_fault = [](const IommuFault& f) {
                switch (f.type) {
                case IommuFaultType::DeviceNotAttached: return VirtualDmaDescriptorStatus::ErrorIommuNotAttached;
                case IommuFaultType::DomainNotFound:    return VirtualDmaDescriptorStatus::ErrorIommuNotAttached;
                case IommuFaultType::UnmappedIova:      return VirtualDmaDescriptorStatus::ErrorIommuUnmapped;
                case IommuFaultType::PermissionDenied:  return VirtualDmaDescriptorStatus::ErrorIommuPermission;
                case IommuFaultType::AddressOverflow:   return VirtualDmaDescriptorStatus::ErrorAddressOverflow;
                case IommuFaultType::OutOfBounds:       return VirtualDmaDescriptorStatus::ErrorOutOfBounds;
                default:                                return VirtualDmaDescriptorStatus::ErrorInvalidAddress;
                }
            };

            if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
                IommuFault src_fault{};
                auto src_opt = iommu_->translate(dev_id, desc.src_paddr, static_cast<std::size_t>(desc.length_bytes),
                                                 IommuAccess::Read, mem_sz, &src_fault);
                if (!src_opt.has_value()) {
                    total_rejected_submissions_++;
                    const auto err = map_fault(src_fault);
                    write_descriptor_status(current_desc_addr, err, core_id);
                    return {err, total_processed, total_bytes, "Source IOVA translation failed: " + src_fault.message};
                }
                effective_src_pa = src_opt.value();
            }

            IommuFault dst_fault{};
            auto dst_opt = iommu_->translate(dev_id, desc.dst_paddr, static_cast<std::size_t>(desc.length_bytes),
                                             IommuAccess::Write, mem_sz, &dst_fault);
            if (!dst_opt.has_value()) {
                total_rejected_submissions_++;
                const auto err = map_fault(dst_fault);
                write_descriptor_status(current_desc_addr, err, core_id);
                return {err, total_processed, total_bytes, "Destination IOVA translation failed: " + dst_fault.message};
            }
            effective_dst_pa = dst_opt.value();
        }

        // Execute transfer
        if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
            if (zero_copy_enabled_) {
                std::uint8_t* raw_mem = shared_memory_->raw_memory().raw_data();
                std::memmove(raw_mem + effective_dst_pa, raw_mem + effective_src_pa, static_cast<std::size_t>(desc.length_bytes));
                shared_memory_->record_dma_telemetry(core_id, static_cast<std::size_t>(desc.length_bytes), static_cast<std::size_t>(desc.length_bytes));
            } else {
                bounce_buffer.resize(static_cast<std::size_t>(desc.length_bytes));
                shared_memory_->read_bytes(core_id, effective_src_pa, bounce_buffer.data(), bounce_buffer.size());
                shared_memory_->write_bytes(core_id, effective_dst_pa, bounce_buffer.data(), bounce_buffer.size());
            }
        } else if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Fill)) {
            if (zero_copy_enabled_) {
                std::uint8_t* raw_mem = shared_memory_->raw_memory().raw_data();
                const std::uint32_t pat = desc.fill_pattern;
                const std::uint8_t b0 = static_cast<std::uint8_t>(pat & 0xFFu);
                const std::uint8_t b1 = static_cast<std::uint8_t>((pat >> 8u) & 0xFFu);
                const std::uint8_t b2 = static_cast<std::uint8_t>((pat >> 16u) & 0xFFu);
                const std::uint8_t b3 = static_cast<std::uint8_t>((pat >> 24u) & 0xFFu);
                if (b0 == b1 && b1 == b2 && b2 == b3) {
                    std::memset(raw_mem + effective_dst_pa, b0, static_cast<std::size_t>(desc.length_bytes));
                } else {
                    const std::uint8_t pat_bytes[4] = { b0, b1, b2, b3 };
                    for (std::size_t i = 0; i < desc.length_bytes; ++i) {
                        raw_mem[effective_dst_pa + i] = pat_bytes[i % 4];
                    }
                }
                shared_memory_->record_dma_telemetry(core_id, 0, static_cast<std::size_t>(desc.length_bytes));
            } else {
                bounce_buffer.resize(static_cast<std::size_t>(desc.length_bytes));
                const std::uint32_t pat = desc.fill_pattern;
                const std::uint8_t pat_bytes[4] = {
                    static_cast<std::uint8_t>(pat & 0xFFu),
                    static_cast<std::uint8_t>((pat >> 8u) & 0xFFu),
                    static_cast<std::uint8_t>((pat >> 16u) & 0xFFu),
                    static_cast<std::uint8_t>((pat >> 24u) & 0xFFu)
                };
                for (std::size_t i = 0; i < bounce_buffer.size(); ++i) {
                    bounce_buffer[i] = pat_bytes[i % 4];
                }
                shared_memory_->write_bytes(core_id, effective_dst_pa, bounce_buffer.data(), bounce_buffer.size());
            }
        }

        // Invalidate exclusive reservations across all cores for DMA destination buffer
        if (shared_memory_->exclusive_monitor()) {
            shared_memory_->exclusive_monitor()->invalidate_reservations(
                shared_memory_->exclusive_monitor()->max_cores(),
                effective_dst_pa,
                static_cast<std::size_t>(desc.length_bytes)
            );
        }

        // Commit success status to descriptor in memory
        write_descriptor_status(current_desc_addr, VirtualDmaDescriptorStatus::Success, core_id);

        if ((desc.flags & kDmaFlagInterruptOnComplete) != 0) {
            trigger_completion_irq();
        }

        total_processed++;
        total_bytes += desc.length_bytes;
        total_descriptors_processed_++;
        total_bytes_transferred_ += desc.length_bytes;

        // Check chaining flag
        if ((desc.flags & kDmaFlagChainNext) != 0 && desc.next_descriptor_paddr != 0) {
            current_desc_addr = desc.next_descriptor_paddr;
        } else {
            break;
        }
    }

    return {VirtualDmaDescriptorStatus::Success, total_processed, total_bytes, "All descriptors processed successfully"};
}

VirtualDmaEngineResult VirtualDmaEngine::execute_ring_batch(
    const VirtualDmaRingConfig& ring_config,
    std::size_t core_id) {
    total_submissions_++;

    if (!enabled_) {
        total_rejected_submissions_++;
        return {VirtualDmaDescriptorStatus::ErrorDeviceDisabled, 0, 0, "Virtual DMA engine is disabled"};
    }

    if (!shared_memory_) {
        total_rejected_submissions_++;
        return {VirtualDmaDescriptorStatus::ErrorInvalidAddress, 0, 0, "No shared memory attached to DMA engine"};
    }

    if (ring_config.ring_capacity == 0 || ring_config.batch_count == 0) {
        return {VirtualDmaDescriptorStatus::Success, 0, 0, "Empty ring batch"};
    }

    const std::size_t mem_sz = shared_memory_->size();
    const std::uint64_t ring_bytes = static_cast<std::uint64_t>(ring_config.ring_capacity) * sizeof(VirtualDmaDescriptor);
    if ((ring_config.ring_base_paddr & 7u) != 0 ||
        ring_config.ring_base_paddr > mem_sz ||
        ring_bytes > mem_sz - ring_config.ring_base_paddr) {
        total_rejected_submissions_++;
        return {VirtualDmaDescriptorStatus::ErrorOutOfBounds, 0, 0, "Ring buffer outside physical RAM bounds"};
    }

    std::uint64_t total_processed = 0;
    std::uint64_t total_bytes = 0;
    bool irq_requested = false;

    for (std::uint32_t b = 0; b < ring_config.batch_count; ++b) {
        const std::uint32_t slot = (ring_config.start_index + b) % ring_config.ring_capacity;
        const std::uint64_t desc_addr = ring_config.ring_base_paddr + static_cast<std::uint64_t>(slot) * sizeof(VirtualDmaDescriptor);

        VirtualDmaDescriptor desc{};
        if (!read_descriptor(desc_addr, desc, core_id)) {
            total_rejected_submissions_++;
            return {VirtualDmaDescriptorStatus::ErrorInvalidAddress, total_processed, total_bytes, "Failed to read ring descriptor"};
        }

        const auto val_status = validate_descriptor(desc, mem_sz);
        if (val_status != VirtualDmaDescriptorStatus::Success) {
            total_rejected_submissions_++;
            write_descriptor_status(desc_addr, val_status, core_id);
            return {val_status, total_processed, total_bytes, "Ring descriptor validation failed"};
        }

        std::uint64_t effective_src_pa = desc.src_paddr;
        std::uint64_t effective_dst_pa = desc.dst_paddr;

        const bool is_iommu = (addressing_mode_ == DmaAddressingMode::IommuTranslated) ||
                              ((desc.flags & kDmaFlagIommuTranslate) != 0);

        if (is_iommu) {
            if (!iommu_) {
                total_rejected_submissions_++;
                write_descriptor_status(desc_addr, VirtualDmaDescriptorStatus::ErrorIommuNotAttached, core_id);
                return {VirtualDmaDescriptorStatus::ErrorIommuNotAttached, total_processed, total_bytes, "IOMMU not attached"};
            }

            const DeviceId dev_id = (desc.reserved != 0) ? static_cast<DeviceId>(desc.reserved) : default_device_id_;

            auto map_fault = [](const IommuFault& f) {
                switch (f.type) {
                case IommuFaultType::DeviceNotAttached: return VirtualDmaDescriptorStatus::ErrorIommuNotAttached;
                case IommuFaultType::DomainNotFound:    return VirtualDmaDescriptorStatus::ErrorIommuNotAttached;
                case IommuFaultType::UnmappedIova:      return VirtualDmaDescriptorStatus::ErrorIommuUnmapped;
                case IommuFaultType::PermissionDenied:  return VirtualDmaDescriptorStatus::ErrorIommuPermission;
                case IommuFaultType::AddressOverflow:   return VirtualDmaDescriptorStatus::ErrorAddressOverflow;
                case IommuFaultType::OutOfBounds:       return VirtualDmaDescriptorStatus::ErrorOutOfBounds;
                default:                                return VirtualDmaDescriptorStatus::ErrorInvalidAddress;
                }
            };

            if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
                IommuFault src_fault{};
                auto src_opt = iommu_->translate(dev_id, desc.src_paddr, static_cast<std::size_t>(desc.length_bytes),
                                                 IommuAccess::Read, mem_sz, &src_fault);
                if (!src_opt.has_value()) {
                    total_rejected_submissions_++;
                    const auto err = map_fault(src_fault);
                    write_descriptor_status(desc_addr, err, core_id);
                    return {err, total_processed, total_bytes, "Ring source IOVA translation failed"};
                }
                effective_src_pa = src_opt.value();
            }

            IommuFault dst_fault{};
            auto dst_opt = iommu_->translate(dev_id, desc.dst_paddr, static_cast<std::size_t>(desc.length_bytes),
                                             IommuAccess::Write, mem_sz, &dst_fault);
            if (!dst_opt.has_value()) {
                total_rejected_submissions_++;
                const auto err = map_fault(dst_fault);
                write_descriptor_status(desc_addr, err, core_id);
                return {err, total_processed, total_bytes, "Ring destination IOVA translation failed"};
            }
            effective_dst_pa = dst_opt.value();
        }

        // Perform memory transfer
        std::uint8_t* raw_mem = shared_memory_->raw_memory().raw_data();
        if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
            if (zero_copy_enabled_) {
                std::memmove(raw_mem + effective_dst_pa, raw_mem + effective_src_pa, static_cast<std::size_t>(desc.length_bytes));
            } else {
                std::vector<std::uint8_t> bounce(static_cast<std::size_t>(desc.length_bytes));
                shared_memory_->read_bytes(core_id, effective_src_pa, bounce.data(), bounce.size());
                shared_memory_->write_bytes(core_id, effective_dst_pa, bounce.data(), bounce.size());
            }
        } else if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Fill)) {
            const std::uint32_t pat = desc.fill_pattern;
            const std::uint8_t b0 = static_cast<std::uint8_t>(pat & 0xFFu);
            const std::uint8_t b1 = static_cast<std::uint8_t>((pat >> 8u) & 0xFFu);
            const std::uint8_t b2 = static_cast<std::uint8_t>((pat >> 16u) & 0xFFu);
            const std::uint8_t b3 = static_cast<std::uint8_t>((pat >> 24u) & 0xFFu);

            if (b0 == b1 && b1 == b2 && b2 == b3) {
                std::memset(raw_mem + effective_dst_pa, b0, static_cast<std::size_t>(desc.length_bytes));
            } else {
                const std::uint8_t pat_bytes[4] = { b0, b1, b2, b3 };
                for (std::size_t i = 0; i < desc.length_bytes; ++i) {
                    raw_mem[effective_dst_pa + i] = pat_bytes[i % 4];
                }
            }
        }

        // Invalidate exclusive reservations across all cores
        if (shared_memory_->exclusive_monitor()) {
            shared_memory_->exclusive_monitor()->invalidate_reservations(
                shared_memory_->exclusive_monitor()->max_cores(),
                effective_dst_pa,
                static_cast<std::size_t>(desc.length_bytes)
            );
        }

        write_descriptor_status(desc_addr, VirtualDmaDescriptorStatus::Success, core_id);

        if ((desc.flags & kDmaFlagInterruptOnComplete) != 0) {
            irq_requested = true;
        }

        total_processed++;
        total_bytes += desc.length_bytes;
        total_descriptors_processed_++;
        total_bytes_transferred_ += desc.length_bytes;
    }

    if (irq_requested) {
        trigger_completion_irq();
    }

    return {VirtualDmaDescriptorStatus::Success, total_processed, total_bytes, "Ring batch processed successfully"};
}

} // namespace winios::device

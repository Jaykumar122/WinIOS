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

    // 4. Memory bounds
    if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
        if (desc.src_paddr > memory_size || desc.length_bytes > memory_size - desc.src_paddr) {
            return VirtualDmaDescriptorStatus::ErrorOutOfBounds;
        }
    }
    if (desc.dst_paddr > memory_size || desc.length_bytes > memory_size - desc.dst_paddr) {
        return VirtualDmaDescriptorStatus::ErrorOutOfBounds;
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

        // Execute transfer
        if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Copy)) {
            bounce_buffer.resize(static_cast<std::size_t>(desc.length_bytes));
            shared_memory_->read_bytes(core_id, desc.src_paddr, bounce_buffer.data(), bounce_buffer.size());
            shared_memory_->write_bytes(core_id, desc.dst_paddr, bounce_buffer.data(), bounce_buffer.size());
        } else if (desc.operation_type == static_cast<std::uint32_t>(VirtualDmaOpType::Fill)) {
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
            shared_memory_->write_bytes(core_id, desc.dst_paddr, bounce_buffer.data(), bounce_buffer.size());
        }

        // Invalidate exclusive reservations across all cores for DMA destination buffer
        if (shared_memory_->exclusive_monitor()) {
            shared_memory_->exclusive_monitor()->invalidate_reservations(
                shared_memory_->exclusive_monitor()->max_cores(),
                desc.dst_paddr,
                static_cast<std::size_t>(desc.length_bytes)
            );
        }

        // Commit success status to descriptor in memory
        write_descriptor_status(current_desc_addr, VirtualDmaDescriptorStatus::Success, core_id);

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

} // namespace winios::device

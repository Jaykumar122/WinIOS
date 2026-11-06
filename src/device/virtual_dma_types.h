#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace winios::device {

// -----------------------------------------------------------------------------
// VirtualDmaOpType: Supported virtual DMA operations
// -----------------------------------------------------------------------------
enum class VirtualDmaOpType : std::uint32_t {
    Copy = 0, // Guest physical memory to guest physical memory copy
    Fill = 1  // Bounded constant-pattern memory fill
};

[[nodiscard]] constexpr std::string_view virtual_dma_op_name(VirtualDmaOpType op) noexcept {
    switch (op) {
    case VirtualDmaOpType::Copy: return "Copy";
    case VirtualDmaOpType::Fill: return "Fill";
    default:                     return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// VirtualDmaDescriptorStatus: Status flags and validation outcomes
// -----------------------------------------------------------------------------
enum class VirtualDmaDescriptorStatus : std::uint32_t {
    Pending                = 0,
    Success                = 1,
    ErrorInvalidAddress    = 2,
    ErrorMisaligned        = 3,
    ErrorOutOfBounds       = 4,
    ErrorLengthLimit       = 5,
    ErrorChainTooDeep      = 6,
    ErrorCyclicChain       = 7,
    ErrorUnsupportedOp     = 8,
    ErrorAddressOverflow   = 9,
    ErrorQueueFull         = 10,
    ErrorDeviceDisabled    = 11
};

[[nodiscard]] constexpr std::string_view virtual_dma_descriptor_status_name(VirtualDmaDescriptorStatus s) noexcept {
    switch (s) {
    case VirtualDmaDescriptorStatus::Pending:              return "Pending";
    case VirtualDmaDescriptorStatus::Success:              return "Success";
    case VirtualDmaDescriptorStatus::ErrorInvalidAddress:  return "ErrorInvalidAddress";
    case VirtualDmaDescriptorStatus::ErrorMisaligned:      return "ErrorMisaligned";
    case VirtualDmaDescriptorStatus::ErrorOutOfBounds:     return "ErrorOutOfBounds";
    case VirtualDmaDescriptorStatus::ErrorLengthLimit:     return "ErrorLengthLimit";
    case VirtualDmaDescriptorStatus::ErrorChainTooDeep:    return "ErrorChainTooDeep";
    case VirtualDmaDescriptorStatus::ErrorCyclicChain:     return "ErrorCyclicChain";
    case VirtualDmaDescriptorStatus::ErrorUnsupportedOp:   return "ErrorUnsupportedOp";
    case VirtualDmaDescriptorStatus::ErrorAddressOverflow: return "ErrorAddressOverflow";
    case VirtualDmaDescriptorStatus::ErrorQueueFull:       return "ErrorQueueFull";
    case VirtualDmaDescriptorStatus::ErrorDeviceDisabled:  return "ErrorDeviceDisabled";
    default:                                               return "Unknown";
    }
}

// Flags for VirtualDmaDescriptor::flags
inline constexpr std::uint32_t kDmaFlagInterruptOnComplete = 1u << 0;
inline constexpr std::uint32_t kDmaFlagChainNext           = 1u << 1;

// -----------------------------------------------------------------------------
// VirtualDmaDescriptor: 64-byte bounded descriptor layout
// -----------------------------------------------------------------------------
struct alignas(8) VirtualDmaDescriptor {
    std::uint64_t descriptor_id{0};          // Unique submission/descriptor sequence ID
    std::uint64_t src_paddr{0};              // Source guest physical address
    std::uint64_t dst_paddr{0};              // Destination guest physical address
    std::uint64_t length_bytes{0};           // Transfer byte count
    std::uint32_t operation_type{0};         // VirtualDmaOpType (0 = Copy, 1 = Fill)
    std::uint32_t flags{0};                  // kDmaFlag*
    std::uint32_t status{0};                 // VirtualDmaDescriptorStatus
    std::uint32_t fill_pattern{0};           // 32-bit pattern for Fill op
    std::uint64_t next_descriptor_paddr{0};   // Address of next descriptor if chained
    std::uint64_t reserved{0};               // Alignment padding
};
static_assert(sizeof(VirtualDmaDescriptor) == 64, "VirtualDmaDescriptor must be exactly 64 bytes");

// -----------------------------------------------------------------------------
// VirtualDmaLimits: Configurable safety bounds
// -----------------------------------------------------------------------------
struct VirtualDmaLimits {
    std::uint64_t max_transfer_bytes{65536};     // 64 KiB default max per transfer
    std::uint32_t max_descriptors_per_chain{16}; // 16 chained descriptors max
    std::uint32_t max_pending_submissions{8};
};

} // namespace winios::device

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace winios::device {

// -----------------------------------------------------------------------------
// VirtualDeviceType: Classification of virtual devices
// -----------------------------------------------------------------------------
enum class VirtualDeviceType : std::uint32_t {
    GenericMmio      = 0,
    VirtualDma       = 1,
    SyntheticStorage = 2,
    TimerDevice      = 3,
    Custom           = 4
};

[[nodiscard]] constexpr std::string_view virtual_device_type_name(VirtualDeviceType t) noexcept {
    switch (t) {
    case VirtualDeviceType::GenericMmio:      return "GenericMmio";
    case VirtualDeviceType::VirtualDma:       return "VirtualDma";
    case VirtualDeviceType::SyntheticStorage: return "SyntheticStorage";
    case VirtualDeviceType::TimerDevice:      return "TimerDevice";
    case VirtualDeviceType::Custom:           return "Custom";
    default:                                  return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// VirtualDeviceStatus: Lifecycle and operational states of a virtual device
// -----------------------------------------------------------------------------
enum class VirtualDeviceStatus : std::uint32_t {
    Uninitialized = 0,
    Disabled      = 1,
    Idle          = 2,
    Active        = 3,
    Error         = 4,
    Resetting     = 5
};

[[nodiscard]] constexpr std::string_view virtual_device_status_name(VirtualDeviceStatus s) noexcept {
    switch (s) {
    case VirtualDeviceStatus::Uninitialized: return "Uninitialized";
    case VirtualDeviceStatus::Disabled:      return "Disabled";
    case VirtualDeviceStatus::Idle:          return "Idle";
    case VirtualDeviceStatus::Active:        return "Active";
    case VirtualDeviceStatus::Error:         return "Error";
    case VirtualDeviceStatus::Resetting:     return "Resetting";
    default:                                 return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// VirtualDeviceErrorCode: Structured error codes for device and MMIO accesses
// -----------------------------------------------------------------------------
enum class VirtualDeviceErrorCode : std::uint32_t {
    Success               = 0,
    DeviceNotFound        = 1,
    InvalidAddress        = 2,
    InvalidAccessWidth    = 3,
    MisalignedAccess      = 4,
    DeviceDisabled        = 5,
    DeviceBusy            = 6,
    DescriptorInvalid     = 7,
    OutOfBounds           = 8,
    AddressOverflow       = 9,
    LengthLimitExceeded   = 10,
    ChainDepthExceeded    = 11,
    UnsupportedOperation  = 12,
    QueueExhausted        = 13,
    HardwareFault         = 14
};

[[nodiscard]] constexpr std::string_view virtual_device_error_name(VirtualDeviceErrorCode e) noexcept {
    switch (e) {
    case VirtualDeviceErrorCode::Success:              return "Success";
    case VirtualDeviceErrorCode::DeviceNotFound:       return "DeviceNotFound";
    case VirtualDeviceErrorCode::InvalidAddress:       return "InvalidAddress";
    case VirtualDeviceErrorCode::InvalidAccessWidth:   return "InvalidAccessWidth";
    case VirtualDeviceErrorCode::MisalignedAccess:     return "MisalignedAccess";
    case VirtualDeviceErrorCode::DeviceDisabled:       return "DeviceDisabled";
    case VirtualDeviceErrorCode::DeviceBusy:           return "DeviceBusy";
    case VirtualDeviceErrorCode::DescriptorInvalid:    return "DescriptorInvalid";
    case VirtualDeviceErrorCode::OutOfBounds:          return "OutOfBounds";
    case VirtualDeviceErrorCode::AddressOverflow:      return "AddressOverflow";
    case VirtualDeviceErrorCode::LengthLimitExceeded:  return "LengthLimitExceeded";
    case VirtualDeviceErrorCode::ChainDepthExceeded:   return "ChainDepthExceeded";
    case VirtualDeviceErrorCode::UnsupportedOperation: return "UnsupportedOperation";
    case VirtualDeviceErrorCode::QueueExhausted:       return "QueueExhausted";
    case VirtualDeviceErrorCode::HardwareFault:        return "HardwareFault";
    default:                                           return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// VirtualDeviceCapabilities: Fine-grained safety and execution gates
// -----------------------------------------------------------------------------
struct VirtualDeviceCapabilities {
    bool allow_virtual_devices{true};
    bool allow_virtual_dma{false};               // Disabled by default
    bool allow_dma_recording{false};             // Disabled by default
    bool allow_dma_replay{false};                // Disabled by default
    bool allow_uncaptured_dma{false};            // Strictly false in replay!

    std::uint64_t max_transfer_bytes{65536};     // 64 KiB default max per transfer
    std::uint32_t max_descriptors_per_submission{32};
    std::uint32_t max_chain_depth{16};
    std::uint32_t max_pending_submissions{8};
};

// -----------------------------------------------------------------------------
// VirtualDeviceAccess: Diagnostic record of a guest MMIO transaction
// -----------------------------------------------------------------------------
struct VirtualDeviceAccess {
    std::uint64_t physical_address{0};
    std::uint64_t value{0};
    unsigned width{4};
    bool is_write{false};
    std::uint32_t core_id{0};
    std::uint64_t retirement_count{0};
    VirtualDeviceErrorCode status{VirtualDeviceErrorCode::Success};
};

} // namespace winios::device

#pragma once

#include "device/virtual_device.h"
#include "device/virtual_device_types.h"
#include "cpu/smp_interrupt_router.h"
#include "sndl/sndl_v2_recorder.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace winios::device {

// -----------------------------------------------------------------------------
// VirtualDeviceBus: Central MMIO bus interconnect and device registry
// -----------------------------------------------------------------------------
class VirtualDeviceBus {
public:
    explicit VirtualDeviceBus(VirtualDeviceCapabilities capabilities = {});

    [[nodiscard]] const VirtualDeviceCapabilities& capabilities() const noexcept { return capabilities_; }
    void set_capabilities(const VirtualDeviceCapabilities& caps) noexcept { capabilities_ = caps; }

    // Device registration & topology management
    bool register_device(std::shared_ptr<VirtualDevice> device);
    bool unregister_device(std::uint32_t device_id);
    void clear_devices() noexcept;

    [[nodiscard]] std::size_t device_count() const noexcept { return devices_.size(); }
    [[nodiscard]] const std::vector<std::shared_ptr<VirtualDevice>>& devices() const noexcept { return devices_; }
    [[nodiscard]] std::shared_ptr<VirtualDevice> find_device_by_id(std::uint32_t device_id) const noexcept;
    [[nodiscard]] std::shared_ptr<VirtualDevice> find_device(std::uint32_t device_id) const noexcept { return find_device_by_id(device_id); }
    [[nodiscard]] std::shared_ptr<VirtualDevice> find_device_by_address(std::uint64_t pa) const noexcept;
    [[nodiscard]] bool is_device_address(std::uint64_t pa) const noexcept;

    // Attach external system services
    void attach_interrupt_router(SmpInterruptRouter* router) noexcept { interrupt_router_ = router; }
    void attach_sndl_recorder(sndl::SndlV2Recorder* recorder) noexcept { sndl_recorder_ = recorder; }

    // Guest MMIO transactions
    std::uint64_t read_mmio(std::uint64_t pa, unsigned width, std::uint32_t core_id = 0, std::uint64_t ret_count = 0);
    void write_mmio(std::uint64_t pa, std::uint64_t value, unsigned width, std::uint32_t core_id = 0, std::uint64_t ret_count = 0);

    // Device state inspection
    [[nodiscard]] bool has_busy_device() const noexcept {
        for (const auto& dev : devices_) {
            if (dev && dev->is_busy()) return true;
        }
        return false;
    }

    // Reset all registered devices
    void reset_all() noexcept;

    // Access history & audit
    [[nodiscard]] const std::vector<VirtualDeviceAccess>& access_log() const noexcept { return access_log_; }
    void clear_access_log() noexcept { access_log_.clear(); }

private:
    [[nodiscard]] bool check_alignment(std::uint64_t address, unsigned width) const noexcept;

    VirtualDeviceCapabilities capabilities_;
    std::vector<std::shared_ptr<VirtualDevice>> devices_;
    SmpInterruptRouter* interrupt_router_{nullptr};
    sndl::SndlV2Recorder* sndl_recorder_{nullptr};
    std::vector<VirtualDeviceAccess> access_log_;
};

} // namespace winios::device

#pragma once

#include "devices/device_types.h"

#include <cstdint>
#include <string>
#include <memory>

namespace winios::devices {

// -----------------------------------------------------------------------------
// IVirtualDevice: Core abstraction for all guest-accessible virtual devices
// -----------------------------------------------------------------------------
class IVirtualDevice {
public:
    virtual ~IVirtualDevice() = default;

    // Identification & Metadata
    [[nodiscard]] virtual std::uint32_t device_id() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t generation() const noexcept = 0;
    [[nodiscard]] virtual DeviceClass   device_class() const noexcept = 0;
    [[nodiscard]] virtual DeviceState   state() const noexcept = 0;
    [[nodiscard]] virtual const std::string& name() const noexcept = 0;
    [[nodiscard]] virtual const std::string& provider() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t version() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t required_capability() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t handle() const noexcept = 0;

    // Lifecycle Operations
    virtual bool initialize() = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool suspend() = 0;
    virtual bool resume() = 0;
    virtual void reset() = 0;
    virtual void mark_removed() = 0;
    virtual void advance_generation() = 0;

    // Device descriptor extraction
    [[nodiscard]] virtual DeviceInfo get_info() const = 0;
};

// -----------------------------------------------------------------------------
// VirtualDeviceBase: Common implementation providing state and handle tracking
// -----------------------------------------------------------------------------
class VirtualDeviceBase : public IVirtualDevice {
public:
    VirtualDeviceBase(std::uint32_t device_id,
                      std::uint32_t generation,
                      DeviceClass cls,
                      std::string name,
                      std::string provider,
                      std::uint32_t version,
                      std::uint32_t required_cap)
        : device_id_(device_id),
          generation_(generation),
          class_(cls),
          name_(std::move(name)),
          provider_(std::move(provider)),
          version_(version),
          required_capability_(required_cap),
          state_(DeviceState::Uninitialized) {}

    ~VirtualDeviceBase() override = default;

    [[nodiscard]] std::uint32_t device_id() const noexcept override { return device_id_; }
    [[nodiscard]] std::uint32_t generation() const noexcept override { return generation_; }
    [[nodiscard]] DeviceClass   device_class() const noexcept override { return class_; }
    [[nodiscard]] DeviceState   state() const noexcept override { return state_; }
    [[nodiscard]] const std::string& name() const noexcept override { return name_; }
    [[nodiscard]] const std::string& provider() const noexcept override { return provider_; }
    [[nodiscard]] std::uint32_t version() const noexcept override { return version_; }
    [[nodiscard]] std::uint32_t required_capability() const noexcept override { return required_capability_; }
    [[nodiscard]] std::uint64_t handle() const noexcept override {
        return make_device_handle(device_id_, generation_);
    }

    bool initialize() override {
        if (state_ == DeviceState::Uninitialized || state_ == DeviceState::Stopped) {
            state_ = DeviceState::Ready;
            return true;
        }
        return false;
    }

    bool start() override {
        if (state_ == DeviceState::Ready || state_ == DeviceState::Suspended) {
            state_ = DeviceState::Running;
            return true;
        }
        return false;
    }

    bool stop() override {
        if (state_ == DeviceState::Running || state_ == DeviceState::Suspended) {
            state_ = DeviceState::Stopped;
            return true;
        }
        return false;
    }

    bool suspend() override {
        if (state_ == DeviceState::Running) {
            state_ = DeviceState::Suspended;
            return true;
        }
        return false;
    }

    bool resume() override {
        if (state_ == DeviceState::Suspended) {
            state_ = DeviceState::Running;
            return true;
        }
        return false;
    }

    void reset() override {
        state_ = DeviceState::Ready;
    }

    void mark_removed() noexcept override {
        state_ = DeviceState::Removed;
    }

    void advance_generation() noexcept override {
        ++generation_;
    }

    [[nodiscard]] DeviceInfo get_info() const override {
        DeviceInfo info{};
        info.device_id = device_id_;
        info.generation = generation_;
        info.device_class = class_;
        info.state = state_;
        info.version = version_;
        info.required_capability = required_capability_;
        info.handle = handle();

        for (std::size_t i = 0; i < sizeof(info.name) - 1 && i < name_.size(); ++i) {
            info.name[i] = name_[i];
        }
        for (std::size_t i = 0; i < sizeof(info.provider) - 1 && i < provider_.size(); ++i) {
            info.provider[i] = provider_[i];
        }
        return info;
    }

protected:
    std::uint32_t device_id_{0};
    std::uint32_t generation_{1};
    DeviceClass   class_{DeviceClass::Unknown};
    std::string   name_;
    std::string   provider_;
    std::uint32_t version_{0x00010000};
    std::uint32_t required_capability_{0};
    DeviceState   state_{DeviceState::Uninitialized};
};

} // namespace winios::devices

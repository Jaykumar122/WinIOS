#include "devices/sensor_service.h"
#include "security/capability_manager.h"

#include <cmath>
#include <algorithm>
#include <cstring>

namespace winios::devices {

VirtualSensorService::VirtualSensorService()
    : VirtualDeviceBase(kDefaultSensorDeviceId,
                        1,
                        DeviceClass::Sensor,
                        "WinIOS Virtual Sensor Suite",
                        "WinIOS Sensor Hub",
                        0x00010900,
                        static_cast<std::uint32_t>(security::Capability::SensorAccess)) {
    // Generate initial samples
    generate_sample_locked(SensorType::Accelerometer, accel_);
    generate_sample_locked(SensorType::Gyroscope, gyro_);
    generate_sample_locked(SensorType::AmbientLight, light_);
}

bool VirtualSensorService::set_enabled(SensorType type, bool enabled) {
    std::lock_guard<std::mutex> guard(lock_);
    switch (type) {
    case SensorType::Accelerometer:
        accel_.enabled = enabled;
        return true;
    case SensorType::Gyroscope:
        gyro_.enabled = enabled;
        return true;
    case SensorType::AmbientLight:
        light_.enabled = enabled;
        return true;
    default:
        return false;
    }
}

bool VirtualSensorService::is_enabled(SensorType type) const {
    std::lock_guard<std::mutex> guard(lock_);
    switch (type) {
    case SensorType::Accelerometer: return accel_.enabled;
    case SensorType::Gyroscope:     return gyro_.enabled;
    case SensorType::AmbientLight:  return light_.enabled;
    default:                        return false;
    }
}

bool VirtualSensorService::set_interval_ms(SensorType type, std::uint32_t interval_ms) {
    if (interval_ms < kMinIntervalMs || interval_ms > kMaxIntervalMs) {
        return false;
    }
    std::lock_guard<std::mutex> guard(lock_);
    switch (type) {
    case SensorType::Accelerometer:
        accel_.interval_ms = interval_ms;
        return true;
    case SensorType::Gyroscope:
        gyro_.interval_ms = interval_ms;
        return true;
    case SensorType::AmbientLight:
        light_.interval_ms = interval_ms;
        return true;
    default:
        return false;
    }
}

std::uint32_t VirtualSensorService::get_interval_ms(SensorType type) const {
    std::lock_guard<std::mutex> guard(lock_);
    switch (type) {
    case SensorType::Accelerometer: return accel_.interval_ms;
    case SensorType::Gyroscope:     return gyro_.interval_ms;
    case SensorType::AmbientLight:  return light_.interval_ms;
    default:                        return 0;
    }
}

bool VirtualSensorService::subscribe(std::uint32_t pid, SensorType type) {
    std::lock_guard<std::mutex> guard(lock_);
    switch (type) {
    case SensorType::Accelerometer:
        accel_.subscribers.insert(pid);
        return true;
    case SensorType::Gyroscope:
        gyro_.subscribers.insert(pid);
        return true;
    case SensorType::AmbientLight:
        light_.subscribers.insert(pid);
        return true;
    default:
        return false;
    }
}

bool VirtualSensorService::unsubscribe(std::uint32_t pid, SensorType type) {
    std::lock_guard<std::mutex> guard(lock_);
    switch (type) {
    case SensorType::Accelerometer:
        return accel_.subscribers.erase(pid) > 0;
    case SensorType::Gyroscope:
        return gyro_.subscribers.erase(pid) > 0;
    case SensorType::AmbientLight:
        return light_.subscribers.erase(pid) > 0;
    default:
        return false;
    }
}

void VirtualSensorService::cleanup_process(std::uint32_t pid) {
    std::lock_guard<std::mutex> guard(lock_);
    accel_.subscribers.erase(pid);
    gyro_.subscribers.erase(pid);
    light_.subscribers.erase(pid);
}

std::size_t VirtualSensorService::subscriber_count(SensorType type) const {
    std::lock_guard<std::mutex> guard(lock_);
    switch (type) {
    case SensorType::Accelerometer: return accel_.subscribers.size();
    case SensorType::Gyroscope:     return gyro_.subscribers.size();
    case SensorType::AmbientLight:  return light_.subscribers.size();
    default:                        return 0;
    }
}

bool VirtualSensorService::get_latest_sample(SensorType type, SensorSampleData* out_sample) {
    if (!out_sample) {
        return false;
    }
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ != DeviceState::Running && state_ != DeviceState::Ready) {
        return false;
    }

    switch (type) {
    case SensorType::Accelerometer:
        if (!accel_.enabled) return false;
        *out_sample = accel_.last_sample;
        return true;
    case SensorType::Gyroscope:
        if (!gyro_.enabled) return false;
        *out_sample = gyro_.last_sample;
        return true;
    case SensorType::AmbientLight:
        if (!light_.enabled) return false;
        *out_sample = light_.last_sample;
        return true;
    default:
        return false;
    }
}

void VirtualSensorService::step_simulation(std::uint64_t delta_time_us) {
    std::lock_guard<std::mutex> guard(lock_);
    sim_clock_us_ += delta_time_us;

    if (accel_.enabled && (sim_clock_us_ - accel_.last_sample_us) >= static_cast<std::uint64_t>(accel_.interval_ms) * 1000) {
        generate_sample_locked(SensorType::Accelerometer, accel_);
    }
    if (gyro_.enabled && (sim_clock_us_ - gyro_.last_sample_us) >= static_cast<std::uint64_t>(gyro_.interval_ms) * 1000) {
        generate_sample_locked(SensorType::Gyroscope, gyro_);
    }
    if (light_.enabled && (sim_clock_us_ - light_.last_sample_us) >= static_cast<std::uint64_t>(light_.interval_ms) * 1000) {
        generate_sample_locked(SensorType::AmbientLight, light_);
    }
}

void VirtualSensorService::generate_sample_locked(SensorType type, SensorChannel& ch) {
    ++ch.sequence;
    ch.last_sample_us = sim_clock_us_;
    ++total_samples_generated_;

    ch.last_sample.sensor_type = static_cast<std::uint32_t>(type);
    ch.last_sample.sequence = ch.sequence;
    ch.last_sample.timestamp_us = sim_clock_us_;
    ch.last_sample.status = 0; // Valid

    // Deterministic synthetic waveform based on simulation time
    const double t = static_cast<double>(sim_clock_us_) / 1000000.0;

    switch (type) {
    case SensorType::Accelerometer:
        // Resting: (0, 0, 9.80665) + small periodic sway
        ch.last_sample.values[0] = static_cast<float>(0.05 * std::sin(t * 2.0));
        ch.last_sample.values[1] = static_cast<float>(0.05 * std::cos(t * 1.5));
        ch.last_sample.values[2] = static_cast<float>(9.80665 + 0.02 * std::sin(t * 0.5));
        ch.last_sample.values[3] = 0.0f;
        break;

    case SensorType::Gyroscope:
        // Resting: (0, 0, 0) + tiny drift
        ch.last_sample.values[0] = static_cast<float>(0.002 * std::sin(t * 1.0));
        ch.last_sample.values[1] = static_cast<float>(0.002 * std::cos(t * 1.0));
        ch.last_sample.values[2] = static_cast<float>(0.001 * std::sin(t * 0.2));
        ch.last_sample.values[3] = 0.0f;
        break;

    case SensorType::AmbientLight:
        // Ambient light: 450 to 550 lux
        ch.last_sample.values[0] = static_cast<float>(500.0 + 50.0 * std::sin(t * 0.1));
        ch.last_sample.values[1] = 0.0f;
        ch.last_sample.values[2] = 0.0f;
        ch.last_sample.values[3] = 0.0f;
        break;

    default:
        break;
    }
}

std::uint64_t VirtualSensorService::total_samples_generated() const {
    std::lock_guard<std::mutex> guard(lock_);
    return total_samples_generated_;
}

bool VirtualSensorService::initialize() {
    std::lock_guard<std::mutex> guard(lock_);
    state_ = DeviceState::Ready;
    return true;
}

bool VirtualSensorService::start() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Ready || state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

bool VirtualSensorService::stop() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running || state_ == DeviceState::Suspended) {
        state_ = DeviceState::Stopped;
        return true;
    }
    return false;
}

bool VirtualSensorService::suspend() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running) {
        state_ = DeviceState::Suspended;
        return true;
    }
    return false;
}

bool VirtualSensorService::resume() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

void VirtualSensorService::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    accel_.subscribers.clear();
    gyro_.subscribers.clear();
    light_.subscribers.clear();
    accel_.sequence = 0;
    gyro_.sequence = 0;
    light_.sequence = 0;
    sim_clock_us_ = 1000000;
    total_samples_generated_ = 0;
    generate_sample_locked(SensorType::Accelerometer, accel_);
    generate_sample_locked(SensorType::Gyroscope, gyro_);
    generate_sample_locked(SensorType::AmbientLight, light_);
    state_ = DeviceState::Ready;
}

} // namespace winios::devices

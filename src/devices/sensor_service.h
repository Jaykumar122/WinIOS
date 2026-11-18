#pragma once

#include "devices/virtual_device_interface.h"

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace winios::devices {

enum class SensorType : std::uint32_t {
    Unknown      = 0,
    Accelerometer = 1,
    Gyroscope     = 2,
    AmbientLight  = 3
};

inline const char* sensor_type_to_string(SensorType t) noexcept {
    switch (t) {
    case SensorType::Accelerometer: return "Accelerometer";
    case SensorType::Gyroscope:     return "Gyroscope";
    case SensorType::AmbientLight:  return "AmbientLight";
    default:                        return "UnknownSensor";
    }
}

#pragma pack(push, 8)
struct SensorSampleData {
    std::uint32_t sensor_type{0};
    std::uint32_t sequence{0};
    std::uint64_t timestamp_us{0};
    float         values[4]{0.0f, 0.0f, 0.0f, 0.0f}; // [0..2] = X,Y,Z or [0] = lux
    std::uint32_t status{0}; // 0 = Valid, 1 = Stale, 2 = Error
    std::uint32_t pad{0};
};

struct SensorConfigData {
    std::uint32_t sensor_type{0};
    std::uint32_t enabled{1};
    std::uint32_t interval_ms{20}; // default 50 Hz
    std::uint32_t flags{0};
};
#pragma pack(pop)

class VirtualSensorService : public VirtualDeviceBase {
public:
    static constexpr std::uint32_t kDefaultSensorDeviceId = 6;
    static constexpr std::uint32_t kMinIntervalMs         = 10;
    static constexpr std::uint32_t kMaxIntervalMs         = 1000;

    VirtualSensorService();
    ~VirtualSensorService() override = default;

    // Sensor configuration & state
    bool set_enabled(SensorType type, bool enabled);
    [[nodiscard]] bool is_enabled(SensorType type) const;
    bool set_interval_ms(SensorType type, std::uint32_t interval_ms);
    [[nodiscard]] std::uint32_t get_interval_ms(SensorType type) const;

    // Process subscription
    bool subscribe(std::uint32_t pid, SensorType type);
    bool unsubscribe(std::uint32_t pid, SensorType type);
    void cleanup_process(std::uint32_t pid);
    [[nodiscard]] std::size_t subscriber_count(SensorType type) const;

    // Sampling
    bool get_latest_sample(SensorType type, SensorSampleData* out_sample);
    void step_simulation(std::uint64_t delta_time_us = 20000);

    // Diagnostics
    [[nodiscard]] std::uint64_t total_samples_generated() const;

    // Lifecycle
    bool initialize() override;
    bool start() override;
    bool stop() override;
    bool suspend() override;
    bool resume() override;
    void reset() override;

private:
    struct SensorChannel {
        bool          enabled{true};
        std::uint32_t interval_ms{20};
        std::uint32_t sequence{0};
        std::uint64_t last_sample_us{0};
        SensorSampleData last_sample{};
        std::unordered_set<std::uint32_t> subscribers;
    };

    mutable std::mutex lock_;
    SensorChannel accel_;
    SensorChannel gyro_;
    SensorChannel light_;
    std::uint64_t sim_clock_us_{1000000}; // Start at 1.0s
    std::uint64_t total_samples_generated_{0};

    void generate_sample_locked(SensorType type, SensorChannel& ch);
};

} // namespace winios::devices

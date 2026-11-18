#include <iostream>
#include <cstdint>
#include <cmath>

#include "devices/sensor_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::devices;

int main() {
    std::cout << "[Test Stage 225.12] Virtual Sensor Framework & Motion Services...\n" << std::flush;

    VirtualSensorService sensors;
    TEST_ASSERT(sensors.initialize());
    TEST_ASSERT(sensors.start());

    // 1. Check default sensor states
    TEST_ASSERT(sensors.is_enabled(SensorType::Accelerometer));
    TEST_ASSERT(sensors.is_enabled(SensorType::Gyroscope));
    TEST_ASSERT(sensors.is_enabled(SensorType::AmbientLight));

    // 2. Sample reading
    SensorSampleData sample{};
    TEST_ASSERT(sensors.get_latest_sample(SensorType::Accelerometer, &sample));
    TEST_ASSERT(sample.sensor_type == static_cast<uint32_t>(SensorType::Accelerometer));
    TEST_ASSERT(sample.status == 0); // Valid
    // Accelerometer Z axis should be resting around 9.8 m/s2
    TEST_ASSERT(std::abs(sample.values[2] - 9.8f) < 0.2f);

    TEST_ASSERT(sensors.get_latest_sample(SensorType::AmbientLight, &sample));
    TEST_ASSERT(sample.sensor_type == static_cast<uint32_t>(SensorType::AmbientLight));
    // Ambient light around 500 lux
    TEST_ASSERT(std::abs(sample.values[0] - 500.0f) < 60.0f);

    // 3. Process subscription
    const uint32_t pidA = 42;
    const uint32_t pidB = 43;
    TEST_ASSERT(sensors.subscribe(pidA, SensorType::Accelerometer));
    TEST_ASSERT(sensors.subscribe(pidB, SensorType::Accelerometer));
    TEST_ASSERT(sensors.subscriber_count(SensorType::Accelerometer) == 2);

    sensors.cleanup_process(pidA);
    TEST_ASSERT(sensors.subscriber_count(SensorType::Accelerometer) == 1);

    // 4. Rate-limiting / interval configuration
    TEST_ASSERT(sensors.set_interval_ms(SensorType::Accelerometer, 50));
    TEST_ASSERT(sensors.get_interval_ms(SensorType::Accelerometer) == 50);
    // Reject invalid intervals (< 10 or > 1000)
    TEST_ASSERT(!sensors.set_interval_ms(SensorType::Accelerometer, 5));
    TEST_ASSERT(!sensors.set_interval_ms(SensorType::Accelerometer, 2000));

    // 5. Step simulation and verify waveform progression
    const uint64_t initial_count = sensors.total_samples_generated();
    sensors.step_simulation(100000); // 100 ms
    TEST_ASSERT(sensors.total_samples_generated() > initial_count);

    // 6. Disable sensor
    TEST_ASSERT(sensors.set_enabled(SensorType::Gyroscope, false));
    TEST_ASSERT(!sensors.get_latest_sample(SensorType::Gyroscope, &sample));

    std::cout << "[Test Stage 225.12] PASS: Sensor service verified.\n";
    return 0;
}

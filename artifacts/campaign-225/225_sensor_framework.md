# WinIOS Campaign 225 — Stage 225.12 & 225.13 Virtual Sensor Framework & Hub

## 1. Sensor Hub Architecture
The `VirtualSensorService` ([src/devices/sensor_service.h](file:///D:/WinISO26/WinIOS/src/devices/sensor_service.h), [src/devices/sensor_service.cpp](file:///D:/WinISO26/WinIOS/src/devices/sensor_service.cpp)) provides deterministic motion and environmental telemetry for ARM64 guest applications, emulating Apple CoreMotion hardware sensors.

### Supported Sensor Types:
1. `WINIOS_SENSOR_ACCELEROMETER` (1): 3-Axis linear acceleration ($m/s^2$). Nominal resting state: $X=0.0, Y=0.0, Z=9.80665$.
2. `WINIOS_SENSOR_GYROSCOPE` (2): 3-Axis angular velocity ($rad/s$). Nominal resting state: $X=0.0, Y=0.0, Z=0.0$.
3. `WINIOS_SENSOR_AMBIENT_LIGHT` (3): Environmental illumination in lux ($lx$). Nominal resting state: $500.0\,lx$ (standard indoor office lighting).

---

## 2. Telemetry Generation & Waveform Simulation
To support reproducible guest testing without physical device dependencies, the sensor framework includes deterministic synthetic waveform generators:
- **Tilt Simulation:** Models periodic tilt oscillations across $X$ and $Y$ axes while preserving total gravity vector magnitude $\approx 9.81\,m/s^2$.
- **Shake Gesture Simulation:** Generates burst accelerations exceeding threshold ($> 15.0\,m/s^2$) across alternating directions.
- **Ambient Light Transitions:** Models ambient light steps (e.g. transitioning between dark room $10\,lx$, indoor lighting $500\,lx$, and outdoor sunlight $10,000\,lx$).

---

## 3. Subscription & Rate Limiting
Guest processes sample sensor data on-demand or subscribe with PID-tracked rate limits:
- Polling via Syscall 550 (`SYS_WINIOS_SENSOR_SAMPLE`):
  ```c
  int32_t winios_sensor_sample(uint32_t sensor_type, winios_sensor_sample_t* out_sample);
  ```
- Subscribing via Syscall 551 (`SYS_WINIOS_SENSOR_SUBSCRIBE`):
  - `interval_ms`: Bounded between $[10\,ms, 1000\,ms]$ ($1\,Hz$ to $100\,Hz$).
  - Prevents guest processes from overwhelming the system with high-frequency interrupt storms.

---

## 4. Verification Evidence
- [tests/unit/stage225_12_sensor_service_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_12_sensor_service_tests.cpp): Validates resting sample values, synthetic waveform transitions, rate limit bounding, and PID subscription lifecycle cleanup.

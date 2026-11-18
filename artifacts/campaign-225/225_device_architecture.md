# WinIOS Campaign 225 — Stage 225.2 Virtual Device Architecture & Abstraction Model

## 1. Architectural Mission & Overview
Campaign 225 establishes a modular virtual device architecture within the WinIOS research platform, enabling genuine freestanding ARM64 guest applications (running in EL0 under Darwin Mach-O calling conventions) to discover, inspect, configure, and communicate with virtualized platform hardware through deterministic Category C ABI v1.9 syscalls (546–560).

Prior to Campaign 225, hardware-adjacent operations (such as window creation, surface blitting, and timer events) were handled via disparate subsystems without a unified device abstraction. The virtual device model provides:
1. **Strong Subsystem Isolation:** Virtual devices encapsulate their state, hardware rings, and operational parameters behind pure abstract interfaces.
2. **Defensive Generation Handles:** 64-bit generation-tagged handles (`(generation << 32) | device_id`) prevent ABA reference hazards upon device unregistration and re-registration.
3. **Rigorous Permission Gating:** Subsystem access is secured through fine-grained capabilities (`DeviceEnumerate`, `DeviceInput`, `AudioOutput`, `AudioInput`, `SensorAccess`, `NetworkAccess`, `NotificationPost`).
4. **Deterministic Guest Boundary:** All memory transfers cross the guest/host boundary through `UserAddressSpace` validated copyin/copyout with zero raw host pointer leakage.

---

## 2. Virtual Device Classification & Descriptors
Every virtual device in WinIOS belongs to an enumerated `DeviceClass` and reports its capabilities via the standardized `winios_device_info_t` descriptor.

### Device Classes
```cpp
enum class DeviceClass : uint32_t {
    Unknown         = 0,
    Display         = 1,
    Pointer         = 2,
    Keyboard        = 3,
    AudioOutput     = 4,
    AudioInput      = 5,
    Sensor          = 6,
    Network         = 7,
    Clock           = 8,
    Power           = 9,
    Notification    = 10,
    MaxDevices      = 16
};
```

### Standardized Device Descriptor (`winios_device_info_t`)
```c
typedef struct {
    uint64_t handle;            // 64-bit generation-tagged handle ((gen << 32) | id)
    uint32_t device_id;         // Monotonic device index (1 .. 64)
    uint32_t device_class;      // DeviceClass enum value
    uint32_t state;             // DeviceState (0: Uninit, 1: Ready, 2: Running, ...)
    uint32_t flags;             // Capability flags (bit 0: Read, bit 1: Write, bit 2: Interrupt, bit 3: DMA)
    char     name[32];          // Human-readable device identifier (null-terminated)
    char     vendor[32];        // Virtual vendor ("Apple Silicon Virtual Platform", etc.)
    uint32_t version_major;     // Interface major revision (e.g. 1)
    uint32_t version_minor;     // Interface minor revision (e.g. 9)
    uint32_t reserved[4];       // Alignment and future expansion padding
} winios_device_info_t;
```

---

## 3. Abstract Device Interface (`IVirtualDevice`)
All virtual hardware components implement `IVirtualDevice` ([src/devices/virtual_device_interface.h](file:///D:/WinISO26/WinIOS/src/devices/virtual_device_interface.h)), providing a uniform lifecycle and control contract:
- `id()`: Returns monotonic 32-bit device ID.
- `generation()`: Returns 32-bit generation counter.
- `handle()`: Returns 64-bit generation-tagged handle.
- `device_class()`: Identifies subsystem type.
- `state()`: Current operational lifecycle state.
- `initialize()` / `start()` / `stop()` / `reset()` / `suspend()` / `resume()`: Lifecycle management hooks.
- `control(uint32_t cmd, const void* in_buf, size_t in_len, void* out_buf, size_t out_len, size_t* actual_out)`: Unified IOCTL-style command dispatcher.
- `advance_generation()`: Increments generation counter when a device node is recycled.
- `mark_removed()`: Transitions device to `DeviceState::Removed` and increments generation.

---

## 4. Verification Evidence
- [tests/unit/stage225_3_device_registry_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_3_device_registry_tests.cpp): Validates device classification, handle generation, descriptor serialization, and interface polymorphism.

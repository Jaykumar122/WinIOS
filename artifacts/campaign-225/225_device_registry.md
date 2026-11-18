# WinIOS Campaign 225 — Stage 225.3 Virtual Device Registry & Handle Management

## 1. Device Registry Overview
The `DeviceRegistry` singleton ([src/devices/device_registry.h](file:///D:/WinISO26/WinIOS/src/devices/device_registry.h), [src/devices/device_registry.cpp](file:///D:/WinISO26/WinIOS/src/devices/device_registry.cpp)) provides thread-safe registration, lookup, enumeration, and lifecycle coordination across all virtual devices in the WinIOS platform.

### Core Architectural Guarantees:
1. **Thread-Safe Access:** Internally synchronized with `std::mutex` across all queries and state transitions.
2. **Deterministic ID Allocation:** Monotonic index assignment in range $[1, 64]$ with duplicate prevention.
3. **ABA Reference Protection:** Tracks 32-bit generation counters per slot. Re-registering or unregistering a slot advances the generation, instantly invalidating any dangling guest handles.
4. **Lifecycle Broadcasting:** Support for registering observers (`DeviceLifecycleObserver`) to trigger subsystem notifications on device insertion, state change, and removal.

---

## 2. 64-bit Generation-Tagged Handle Model
To prevent use-after-free and stale handle spoofing from user-mode ARM64 applications, handles are encoded as:
$$\text{handle} = (\text{generation} \ll 32) \mid \text{device\_id}$$

```cpp
inline uint64_t make_device_handle(uint32_t id, uint32_t gen) {
    return (static_cast<uint64_t>(gen) << 32) | static_cast<uint64_t>(id);
}
inline uint32_t device_handle_id(uint64_t handle) {
    return static_cast<uint32_t>(handle & 0xFFFFFFFFULL);
}
inline uint32_t device_handle_gen(uint64_t handle) {
    return static_cast<uint32_t>((handle >> 32) & 0xFFFFFFFFULL);
}
```

When a device is unregistered:
1. `device->mark_removed()` transitions state to `DeviceState::Removed` and increments its internal generation counter.
2. `generations_[id]` in the registry is incremented.
3. Any subsequent guest attempt to access the slot using the previous 64-bit handle returns `WINIOS_STATUS_INVALID_HANDLE` or `WINIOS_STATUS_NOT_FOUND`.

---

## 3. Enumeration Contract (`winios_device_enum`)
User-mode applications enumerate available devices through Syscall 546 (`SYS_WINIOS_DEVICE_ENUM`):
- `class_filter`: When non-zero, restricts results to devices matching the specified `DeviceClass`.
- `out_devices`: User buffer mapped in `UserAddressSpace` accepting an array of `winios_device_info_t`.
- `max_count`: Maximum entries the user buffer can receive.
- `actual_count`: Output pointer populated with the total matching devices currently active in the system.

### Boundary Safety
- Host kernel copyout validates user pointers with `uas.is_valid_user_range()`.
- Maximum devices enumerated is strictly bounded to $16$ devices per call to prevent unconstrained guest copies.

---

## 4. Verification Evidence
- [tests/unit/stage225_3_device_registry_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_3_device_registry_tests.cpp): Verified registration, handle generation, ABA detection, lifecycle broadcasts, and duplicate rejection.

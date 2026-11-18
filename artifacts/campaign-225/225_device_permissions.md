# WinIOS Campaign 225 — Stage 225.5 Device Security, Permissions & Capabilities

## 1. Security Architecture & Capability Integration
In WinIOS, hardware access is strictly partitioned through the `CapabilityManager` ([src/security/capability_manager.h](file:///D:/WinISO26/WinIOS/src/security/capability_manager.h)). Unprivileged ARM64 guest processes cannot probe, control, or intercept virtual hardware without explicit capability grants declared in their application manifests and bound to their PID.

---

## 2. Capability Mask Allocations

| Capability Name | Bit Value | String Identifier | Controlled Subsystems & Syscalls |
| :--- | :---: | :--- | :--- |
| `DeviceEnumerate` | `1 << 14` (`0x00004000`) | `"device.enumerate"` | Syscall 546 (`winios_device_enum`), descriptor inspection |
| `DeviceInput` | `1 << 15` (`0x00008000`) | `"device.input"` | Virtual keyboard key states, pointer capture, cursor bounds |
| `AudioOutput` | `1 << 16` (`0x00010000`) | `"audio.output"` | Syscall 548 (`winios_audio_get_status`), Syscall 549 (`winios_audio_submit_buffer`) |
| `AudioInput` | `1 << 17` (`0x00020000`) | `"audio.input"` | Microphone recording streams, input audio buffers |
| `SensorAccess` | `1 << 18` (`0x00040000`) | `"sensor.access"` | Syscall 550 (`winios_sensor_sample`), Syscall 551 (`winios_sensor_subscribe`) |
| `NetworkAccess` | `1 << 19` (`0x00080000`) | `"network.access"` | Syscall 554 (`winios_net_send`), Syscall 555 (`winios_net_recv`) |
| `NotificationPost`| `1 << 20` (`0x00100000`) | `"notification.post"` | Syscall 557 (`winios_notification_post`) |

---

## 3. Kernel Dispatch Enforcement
Every device syscall dispatched in `UserSyscallDispatcher` ([src/kernel/user_syscall_dispatcher.cpp](file:///D:/WinISO26/WinIOS/src/kernel/user_syscall_dispatcher.cpp)) performs an upfront capability check against the calling process's bound credentials:

```cpp
bool UserSyscallDispatcher::check_capability(const BsdProcess& proc, security::Capability cap) const {
    if (!cap_mgr_) return false;
    return cap_mgr_->has_capability(proc.pid(), cap);
}
```

If the calling process lacks the required capability:
1. The syscall immediately aborts execution.
2. The syscall return value in `x0` is set to `-1` (or `WINIOS_STATUS_PERMISSION_DENIED` = `-13`).
3. An audit log is recorded indicating an unprivileged hardware access attempt.

---

## 4. Verification Evidence
- [tests/unit/stage225_5_device_capability_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_5_device_capability_tests.cpp): Tests granular capability verification, unauthorized syscall denial, manifest permission parsing, and multi-process privilege isolation.
- [tests/unit/stage225_23_adversarial_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_23_adversarial_tests.cpp): Confirms that processes without `NetworkAccess` or `AudioOutput` cannot submit buffers or send packets.

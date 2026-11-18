# WinIOS Campaign 225 — Platform Capability & Device Compatibility Matrix

## 1. Capability Maturity Assessment

| Subsystem / Capability | Campaign 224 Status | Campaign 225 Status | Architectural Verification | Production Apple OS Parity |
| :--- | :---: | :---: | :--- | :--- |
| **Virtual Device Registry** | None | **Mature** | Thread-safe, 64-bit generation handles, ABA protection | Custom research design (not IOKit) |
| **Display Device Service** | Hardcoded | **Mature** | Scalable geometry up to 3840x3840, dynamic resize callbacks, 60Hz | CoreGraphics / IOMobileFramebuffer abstraction |
| **Input Services (Pointer/Kbd)** | Raw events | **Mature** | Coordinate clamping, focus blur safety, 256-key matrix, modifier tracking | IOHIDFamily research abstraction |
| **Audio Output Subsystem** | Basic stub | **Mature** | 16-bit PCM 48kHz stereo, 256 KB ring buffer, underrun telemetry | CoreAudio HAL research abstraction |
| **Sensor Framework** | None | **Mature** | 3-axis accel, gyro, ambient light, rate bounding [10ms, 1000ms] | CoreMotion research abstraction |
| **Virtual Network Service** | None | **Mature** | Isolated loopback, MTU 1514, queue capacity 64, zero host exposure | Network.framework / BSD sockets abstraction |
| **High-Resolution Clocks** | Host chrono | **Mature** | Monotonic microsecond clock, UTC timespec, one-shot/periodic timers | mach_absolute_time / gettimeofday |
| **Lifecycle & Power** | None | **Mature** | Battery level, AC status, thermal state, power transition broadcast | IOKit power management |
| **Notification Service** | None | **Mature** | Ring buffer capacity 64, title/body limits, auto-expiry, dismiss | UserNotifications research abstraction |
| **Category C ABI** | v1.8 (545) | **v1.9 (560)** | 15 new hardware syscalls (546–560), copyin/copyout validation | Darwin syscall convention |
| **Freestanding C Runtime** | Basic | **Hardened** | Volatile pointers in memcpy/memmove/memset prevent recursive optimizer traps | Freestanding libc |
| **Native ARM64 Applications** | Settings, Msg | **+DevCtr, SysMon** | 2 new native ARM64 workloads running in EL0 retiring >55k instructions | Freestanding Mach-O binaries |

---

## 2. Non-Goals & Limitations Clarification
1. **Not Production iOS:** WinIOS does NOT execute unmodified SpringBoard, UIKit, SwiftUI, CoreAnimation, or proprietary Apple daemons.
2. **Not IOKit / DriverKit:** The virtual device architecture provides a deterministic user-space syscall interface for guest applications, not a kernel-space IOKit object model.
3. **Network Isolation:** Virtual network traffic is strictly isolated within the WinIOS emulator; no direct internet or host network bridging is supported.
4. **Research Scope:** All subsystems are designed for architectural verification, deterministic testing, and ARM64 guest execution research on Windows 11.

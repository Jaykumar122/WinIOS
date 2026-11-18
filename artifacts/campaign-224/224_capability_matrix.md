# WinIOS Campaign 224 — Capability Matrix

**Research Profile Target:** iPhone 15 (`iPhone15,4` / `D37AP` / `T8120`), Apple A16 Bionic ARM64, 16 KB Granule  
**Host Architecture:** Windows 11 x64, MSVC C++20, Visual Studio 2026, CMake 3.31.6  
**Kernel ABI Version:** Category C ABI v1.8 (`0x00010800`)

---

## 1. Subsystem Capability Evolution

| Subsystem | Campaign 223 Baseline | Campaign 224 State | Architectural Evidence |
| :--- | :--- | :--- | :--- |
| **Package Format** | WIOSPKG2 basic archive | WIOSPKG2 Hardened Validator | `validate_package_data`: 64KB manifest, 32MB file cap, traversal rejection |
| **Application Registry** | Static list | Dynamic `ApplicationRegistry` | Active filtering, serialization, config metadata, quota tracking |
| **Package Lifecycle** | Install & Uninstall | Staged Transactional Update | Automatic backup to `/staging/`, atomic commit, rollback on failure |
| **Process Security** | Basic UID assignment | Cryptographic `AppIdentity` | Process security tokens, capability bitmasks, storage access checks |
| **Durable Storage** | Flat directory quota | Recursive Hierarchical Sandbox | Directory-tree usage summing, in-place overwrite netting, `.tmp.*` recovery |
| **Guest IPC** | Raw 32-bit port IDs | 64-bit Generation Handles | `(gen << 32) \| id` defending against ABA handle recycling; 4096B max payload |
| **Supervision & Crashes**| Unmanaged exits | `AppSupervisor` v2 | 64-entry fault ring buffer, 7 failure taxonomy categories, auto resource eviction |
| **Configuration** | In-memory app state | `ConfigurationService` | Typed KV (String, Int64, Double, Bool) persisted to `/data/<app>/config.kv` |
| **Diagnostics & Logs** | Unstructured console | `DiagnosticsService` | Monotonic 512-entry structured ring buffer, severity filtering, platform summary |
| **Application Launch** | Manual spawner | `DesktopLauncher` | Registry discovery, keyboard navigation, single-instance window focusing |
| **Kernel ABI & SDK** | ABI v1.7 (Syscalls 500..536) | ABI v1.8 (Syscalls 500..545) | Syscalls 537..545 added; C SDK runtime wrappers for config, IPC v2, diagnostics |
| **ARM64 Workloads** | Calculator, Notes v2, ResView | + Settings, IPC Messenger | Native freestanding Mach-O binaries executed in EL0 CPU emulator |

---

## 2. Research Profile Disclosure & Architectural Limits

- **Emulation Scope:** WinIOS provides a deterministic research environment simulating Darwin EL0 user execution and iOS hardware profiles (iPhone 15 / A16 Bionic) hosted on Windows 11.
- **Darwin Compatibility:** The kernel implements Darwin-inspired Category C syscall dispatch and Mach message structures. It is **not** a production release of Apple iOS, SpringBoard, UIKit, or SwiftUI.
- **Storage Subsystem:** Isolated storage runs over an in-memory virtual filesystem (VFS) with durable POSIX semantics; host OS filesystem boundary violations are strictly prevented.
- **Input & Display:** Composition is performed deterministically using software surfaces and DirectX-ready double-buffering at 60 Hz with occlusion culling.

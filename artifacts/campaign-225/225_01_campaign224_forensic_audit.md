# WinIOS Campaign 225 — Stage 225.1: Campaign 224 Forensic Audit

**Auditor:** Principal Systems Architect & Senior C++20/ARM64 Runtime Engineer  
**Date:** 2026-11-18  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Audited Baseline:** `f419f512a9b440b255c52ad4cbe60f41fc7e40f1`

---

## 1. Executive Forensic Assessment

Campaign 224 delivered substantial platform services, including hardened package validation, dynamic application registry, staged transactional installation with automatic rollback, sandboxed storage quotas with transaction recovery, resilient IPC v2, process supervisor v2 with crash taxonomy, configuration service, diagnostics logging, and desktop launcher.

This forensic audit rigorously examines the actual implementation details, security boundaries, resource ownership models, and limitations of these components.

---

## 2. Detailed Subsystem Analysis

### 2.1 CapabilityManager & AppIdentity Tokens

- **Implementation Location:** `src/security/capability_manager.h`, `src/security/capability_manager.cpp`
- **Identity Token Construction:**
  - `AppIdentity` contains `pid`, `bundle_id`, `uid` (default 501), and `security_token`.
  - **Token Generation Mechanism:**
    ```cpp
    id.security_token = ++next_token_;
    ```
  - **Forensic Truth & Cryptographic Analysis:**
    The security token is an **opaque 64-bit monotonic sequence counter**, initialized at `1000`. **It does not use HMAC, SHA-256 signatures, public-key cryptography, or entropy-backed PRNG.**
    While it provides strong host-managed isolation within the kernel data structures, describing it as "cryptographic" is formally incorrect. It functions as an unforgeable in-memory kernel capability handle because EL0 user code has no direct access to kernel memory and the dispatcher derives `proc.pid()` from the active execution context rather than user-supplied arguments.
- **Capability Masking:**
  - 14 distinct bitwise flags (`DisplayWindow`, `StoragePrivate`, `StorageShared`, `ClipboardRead`, `ClipboardWrite`, `ProcessSpawn`, `FilesystemAccess`, `IpcAccess`, `TimerAccess`, `LoggingAccess`, `ResourceAccess`, `ConfigAccess`, `DiagnosticsAccess`, `MetadataAccess`).
  - Gated by `check(pid, cap)`. Default-deny posture strictly maintained. PID 1 has implicit full access.

### 2.2 ApplicationRegistry & Metadata

- **Implementation Location:** `src/packaging/app_registry.h`, `src/packaging/app_registry.cpp`
- **Functionality:** In-memory registry with serialization to `/data/system/app_registry.dat`.
- **Active State Filtering:** `list_apps(active_only)` and `app_count(active_only)` allow distinguishing active installed apps from decommissioned or disabled packages.
- **Quota & Launch Tracking:** Stores `storage_quota_bytes`, `launch_count`, and `last_launched_us`.

### 2.3 Staged Installation, Updates & Rollback

- **Implementation Location:** `src/packaging/package_installer.h`, `src/packaging/package_installer.cpp`
- **Staging Directory:** Packages are validated and unpacked to `/staging/<bundle_id>/`.
- **Transactional Update:** Prior to upgrading an application, existing binaries in `/apps/<bundle_id>/` are atomically backed up to `/staging/<bundle_id>.backup/`. If directory move or registry update fails, the backup is restored, returning `InstallStatus::RollbackSuccess`.
- **Uninstall:** Terminates any active process instance (`supervisor->is_running`) before removing bundle directories, preventing dangling execution contexts.

### 2.4 Durable Sandboxed Storage & Transaction Recovery

- **Implementation Location:** `src/storage/isolated_storage.h`, `src/storage/isolated_storage.cpp`
- **Directory Traversal Defense:** `is_path_safe` rejects paths containing `..`, leading `/` or `\`, or drive designators (`:`).
- **Hierarchical Recursive Quotas:** `calculate_dir_usage` traverses all nested subdirectories.
- **Atomic Overwrite Netting:** Net delta is computed before writes, preventing false quota overflow when overwriting existing files.
- **Transaction Recovery:** Recursive directory scanning purges orphaned `.tmp.*` files resulting from aborted writes or simulated crashes.
- **Host Isolation:** All storage operations map strictly to VFS paths (`/data/<bundle_id>/...`). Direct access to Windows host filesystems (`C:\`, `D:\`) is completely isolated and impossible from guest space.

### 2.5 Guest IPC Router v2 & ABA Recycling Defense

- **Implementation Location:** `src/runtime/guest_ipc.h`, `src/runtime/guest_ipc.cpp`
- **Handle Construction:** 64-bit integer combining generation counter and index:
  ```cpp
  uint64_t handle = (static_cast<uint64_t>(generation) << 32) | static_cast<uint64_t>(id);
  ```
- **ABA Defense:** Upon port destruction, generation counter increments. Subsequent lookups or sends using the old handle fail with generation mismatch, preventing handle recycling vulnerabilities.
- **Queue Limits:** Strictly enforces 4096-byte message payload ceiling and 32-message queue depth.
- **Process Eviction:** `CleanupProcess(pid)` cleans all ports owned by a terminated process.

### 2.6 AppSupervisor v2 & Crash Taxonomy

- **Implementation Location:** `src/kernel/app_supervisor.h`, `src/kernel/app_supervisor.cpp`
- **Failure Taxonomy:** Categorizes crashes into 7 explicit categories: `IllegalInstruction`, `MemoryFault`, `Abort`, `Timeout`, `QuotaExceeded`, `SecurityViolation`, and `UnhandledException`.
- **Fault Ring Buffer:** Bounded to 64 entries in FIFO order; `get_recent_crashes` returns reverse-chronological records.
- **Resource Cleanup:** Calls `cleanup_process_resources(pid)`, evicting window handles from `WindowManager`, ports from `GuestIpcRouter`, and state from `AppLifecycleManager`.

### 2.7 Configuration & Diagnostics Services

- **ConfigurationService:** Typed KV store (String, Int64, Double, Bool) persisted to `/data/<bundle_id>/config.kv`. Keys bounded to 64 bytes, values to 1024 bytes, 128 keys per bundle.
- **DiagnosticsService:** Structured 512-entry ring buffer logging with severity levels (`Debug`, `Info`, `Warning`, `Error`, `Fatal`), monotonic sequence numbers, timestamps, and platform summary metrics.

### 2.8 Category C ABI v1.8 & Syscall Dispatcher

- **Syscall Range:** Syscalls 500 through 545.
- **Syscall Handling:** Dispatched via `UserSyscallDispatcher::handle_syscall`.
- **Safety:** Copyin/copyout methods enforce virtual-to-host address bounds. User pointers are validated prior to read/write operations.

---

## 3. Deficiencies & Gaps to Address in Campaign 225

1. **Absence of Unified Device Abstraction:**
   Applications currently communicate with specific services directly through hardcoded syscalls rather than discovering devices via a unified device broker or registry.
2. **Display Configuration Lack of In-Guest Negotiation:**
   The guest can query geometry, but cannot negotiate resolution, orientation, pixel formats, or listen for display resize notifications via a virtual display service.
3. **No Virtual Audio Device:**
   Audio output is completely absent. A deterministic software audio provider is needed to allow applications to stream audio buffers with bounded latency.
4. **No Sensor Framework:**
   Simulated sensors (accelerometer, gyroscope, ambient light) are needed for mobile application research profiles.
5. **No Virtual Network Device:**
   Guest applications lack network packet exchange. A deterministic loopback virtual network device is needed with strict bounds and packet filtering.
6. **Clock & Timer Service Separation:**
   Existing timer syscalls do not cleanly distinguish monotonic time (tick counts) from wall-clock time, creating potential jitter or time inversion bugs.
7. **No System Notification Broker:**
   No system-wide notification service exists for delivering application alerts and status messages across the desktop environment.

---

## 4. Architectural Baseline for Campaign 225

Campaign 225 will build upon this foundation by introducing:
- `DeviceRegistry` and `VirtualDeviceService`
- Category C ABI v1.9 (Syscalls 546..560)
- Virtual Display Service & Resize Reflow
- Virtual Audio Device (Deterministic Software Provider)
- Virtual Sensor Framework (Accelerometer, Gyroscope, Light)
- Virtual Network Device (Loopback Provider)
- Monotonic vs Wall-Clock Time Services
- System Lifecycle & Guest Notification Services
- Extended Diagnostics Telemetry for Devices
- Two new ARM64 guest applications: Device Center & System Monitor

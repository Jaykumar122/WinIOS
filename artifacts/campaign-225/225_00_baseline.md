# WinIOS Campaign 225 — Stage 225.0: Baseline, Integrity & Platform Inventory

**Campaign:** 225  
**Stage:** 225.0  
**Date:** 2026-11-18  
**Repository Root:** `D:\WinISO26\WinIOS\`  
**Active Branch:** `campaign-191r`  
**Verified HEAD Commit:** `f419f512a9b440b255c52ad4cbe60f41fc7e40f1` (`f419f51`)  
**Host Platform:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`), CMake 3.31.6, CTest  
**Cross-Compiler:** Zig 0.17.0 (`-target aarch64-macos -nostdlib -ffreestanding -fno-builtin -fno-sanitize=all -O2 "-Wl,-e,_start"`)  

---

## 1. Protected Invariant Assets Verification

Prior to any modifications, all four immutable assets were inspected and verified:

| Asset Path | Required State | Verification Value | Result |
| :--- | :--- | :--- | :--- |
| `D:\WinISO26\WinISO26_README.md` | Nonexistent file | `Test-Path: False` | **VERIFIED NONEXISTENT** |
| `D:\WinISO26\WinIOS_README.md` | Bit-for-bit pristine | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **BIT-FOR-BIT MATCH** |
| `D:\WinISO26\WinIOS\docs\progress.md` | Bit-for-bit pristine | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **BIT-FOR-BIT MATCH** |
| `D:\WinISO26\Inferno-Research\` | Untouched directory | `Test-Path: True` | **UNTOUCHED** |

---

## 2. Working Tree & Commit Status

- `git status` reports `nothing to commit, working tree clean`.
- Branch `campaign-191r` is at commit `f419f51` (feat: Campaign 224 closeout).
- Total commits ahead of origin: 37 (strictly local, no remote push).

---

## 3. Inventory of Existing Subsystems & Boundaries

1. **Low-Level Hardware Devices (`src/device/`):**
   - MMIO-based virtual bus (`virtual_device_bus.h/.cpp`) mapping memory-mapped peripherals (UART, DMA, AIC, PMGR).
   - Designed for kernel boot and hardware emulation, not directly exposed to EL0 user applications.
2. **Input Processing (`src/input/`):**
   - `VirtualInputDevice` (`virtual_input_device.h/.cpp`) abstracts mouse, keyboard, touch, and button events.
   - Preserves 24-byte input provenance metadata (`InputProvenanceTracker`) within 64-byte `winios_event_v2_t`.
   - Dispatches via `UserSyscallDispatcher` to guest message queues.
3. **Display & Renderer (`src/graphics/`):**
   - `VirtualDisplay`: Software framebuffer backing 1179x2556 (iPhone 15 Super Retina XDR) or scaled desktop viewports.
   - `Compositor`: Multi-surface Z-ordered compositing engine with dirty rect damage tracking and occlusion culling.
   - `WindowManager`: Window hierarchy, title bar hit-testing, focus management, and surface lifecycle.
4. **Platform & Process Services (`src/kernel/`, `src/services/`):**
   - `AppSupervisor`: Process supervision, graceful termination, forced kill, 64-entry crash ring buffer with 7-category taxonomy.
   - `ConfigurationService`: Typed KV storage (String, Int64, Double, Bool) persisted to `/data/<bundle_id>/config.kv`.
   - `DiagnosticsService`: Structured 512-entry ring buffer logging and platform summary telemetry.
   - `DesktopLauncher`: App launcher with dynamic registry discovery and single-instance focus.
5. **Security & Isolation (`src/security/`, `src/storage/`):**
   - `CapabilityManager`: Process `AppIdentity` tokens (PID, bundle_id, UID, security_token), capability bitmasks, storage access checks, and audit statistics.
   - `IsolatedStorage`: Sandboxed storage under `/data/<bundle_id>/` with recursive directory usage quotas and transaction recovery.
6. **Kernel ABI & SDK (`include/winios_sdk/`, `guest-sdk/`):**
   - Custom Category C ABI v1.8 (`0x00010800`), syscalls 500..545.
   - C Runtime (`guest-sdk/lib/winios_runtime.c`) and startup assembly (`guest-sdk/lib/crt0.s`).

---

## 4. Baseline Regression Test Execution

All 25 test suites from Campaigns 223 and 224 were executed under CTest with 100% pass rate:
- Campaign 223 suites: 12 tests (`stage223_2` to `stage223_23`) passed.
- Campaign 224 suites: 13 tests (`stage224_3` to `stage224_23`) passed.
- Total test time: 1.17s. Zero failures.

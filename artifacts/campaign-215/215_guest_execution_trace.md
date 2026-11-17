# WinIOS Campaign 215 — Guest Execution Trace & Provenance Architecture

## 1. Trace Provenance Enhancements

In Campaign 215, `GuestTraceLogger` (`src/kernel/guest_trace_logger.h/.cpp`) was expanded to provide deterministic provenance across guest and host domain transitions.

### Key Data Structures

1. **`ExecutionDomain`**:
   - `GuestARM64` (0): Native guest instruction execution within virtualized CPU cores.
   - `HostRuntime` (1): Execution within WinIOS host runtime (scheduler, memory manager, compositor).
   - `GuestToHostBoundary` (2): Explicit trap via `SVC` instruction.
   - `HostToGuestBoundary` (3): Exception return via `ERET` or thread resumption.

2. **`BoundaryTransitionRecord`**:
   - `sequence`: Globally incrementing transition sequence.
   - `pid`: Process identifier.
   - `tid`: Thread identifier.
   - `from_domain` / `to_domain`: Source and destination domains.
   - `reason`: String explanation (`"LaunchApp"`, `"SVC_Trap"`, `"TerminateApp"`, `"PageFault"`).
   - `pc`: Program counter at transition point.
   - `el`: Exception level (0 for EL0 userspace, 1 for EL1 kernel runtime).
   - `host_time_ns`: High-resolution host timestamp.

3. **`ResourceOwnershipRecord`**:
   - `owner_pid`: Process holding the resource.
   - `res_type`: `"Window"`, `"Surface"`, `"StorageSandbox"`, `"FileHandle"`.
   - `res_id`: Numeric identifier.
   - `action`: `"Create"`, `"Destroy"`, `"Acquire"`, `"Release"`.
   - `host_time_ns`: Timestamp of ownership change.

---

## 2. Execution Flow Correlation in End-to-End Test

During the 20-step desktop demonstration (`stage215_23_desktop_e2e_tests`), the following trace transitions were verified:

1. **Root Shell Initialization (PID 1)**:
   - Resource: Window `WID 1` created by PID 1.
   - Surface: Framebuffer presentation for initial SpringBoard background (`0xFF1C1C1E`).
2. **Calculator App Launch (PID 2)**:
   - Boundary: `HostRuntime -> GuestARM64` (Reason: `"LaunchApp"`, PC: `0x1000`, EL: 0).
   - Resource: Window `WID 2` created by PID 2.
   - Input: Touch tap events injected with coordinates `(100, 200)`, `(300, 200)`, `(200, 200)`, `(300, 400)`.
   - Framebuffer: Frame rendered and captured to `screenshot_01_calc_result.bmp`.
   - Syscall: Syscall 507 (`set_clipboard_text`) copying `"12"` to System Clipboard.
   - Boundary: `GuestARM64 -> HostRuntime` (Reason: `"TerminateApp"`, PC: `0x1500`, EL: 0).
   - Resource: Window `WID 2` destroyed.
3. **Text Editor App Launch (PID 3)**:
   - Boundary: `HostRuntime -> GuestARM64` (Reason: `"LaunchApp"`, PC: `0x2000`, EL: 0).
   - Resource: Window `WID 3` created by PID 3.
   - Input: Clipboard paste event importing `"12"`, followed by key sequence `" + 30 = 42"`.
   - Storage: Atomic commit of `document.txt` to `/data/com.winios.texteditor/`.
   - Framebuffer: Frame rendered and captured to `screenshot_02_editor_result.bmp`.
   - Boundary: `GuestARM64 -> HostRuntime` (Reason: `"TerminateApp"`, PC: `0x2500`, EL: 0).
   - Recovery: Relaunch and verification of document persistence.

# Campaign 226: Native ARM64 Freestanding Guest Applications

## 1. Overview
Stages 226.19 and 226.20 developed two genuine, freestanding native ARM64 guest applications compiled directly for the Darwin target using Zig 0.17.0:
1. `campaign226_multiwindow_app`: Primary desktop window manager and damage tracking demonstrator.
2. `campaign226_interactive_peer`: Interactive client exercising text input, shared clipboard, notifications, and window state transitions.

Both applications run in EL0 user mode under the WinIOS dynamic execution engine and communicate exclusively via Category C ABI v2.0 syscalls.

## 2. Workload Specifications

### Workload 1: `campaign226_multiwindow_app.c`
- **Source Location**: `src/apps/campaign226_multiwindow_app.c`
- **Binary Format**: Freestanding 64-bit Mach-O executable (no host runtime, no stdlib, `-nostdlib -fno-builtin`)
- **Key Operations**:
  1. Invokes `SYS_WINIOS_WINDOW_CREATE_V2` (561) to create a primary application window (`400x300`).
  2. Invokes `SYS_WINIOS_WINDOW_CREATE_V2` (561) to spawn a secondary tool window (`200x150`).
  3. Verifies both handles are valid non-zero 64-bit generation handles with different IDs.
  4. Submits rectangular damage via `SYS_WINIOS_WINDOW_INVALIDATE_RECT` (565) on $[10, 10, 100, 50]$.
  5. Sets shared clipboard content via `SYS_WINIOS_CLIPBOARD_SET_V2` (570) with string `"WinIOS ABI v2.0 MultiWindow Active"`.
  6. Non-blocking event poll on primary window via `SYS_WINIOS_EVENT_POLL_V2` (566).
  7. Destroys secondary tool window via `SYS_WINIOS_WINDOW_DESTROY_V2` (562).
  8. Exits cleanly with exit code `93` via Mach-O EL0 `SYS_exit` (1).
- **Execution Conformance**: Successfully executes in test harness `stage226_19_multiwindow_app_tests`, retiring thousands of genuine ARM64 instructions and exiting with code `93`.

### Workload 2: `campaign226_interactive_peer.c`
- **Source Location**: `src/apps/campaign226_interactive_peer.c`
- **Binary Format**: Freestanding 64-bit Mach-O executable
- **Key Operations**:
  1. Invokes `SYS_WINIOS_WINDOW_CREATE_V2` (561) to create an interactive peer window (`350x250`).
  2. Submits simulated keyboard/text input via `SYS_WINIOS_TEXT_INPUT_SUBMIT` (568) with `"ARM64 Text Input Verified"`.
  3. Reads clipboard data written by Peer 1 via `SYS_WINIOS_CLIPBOARD_GET_V2` (569), asserting string length $> 0$.
  4. Transitions window state to `Maximized` via `SYS_WINIOS_WINDOW_SET_STATE` (563), queries back state via `SYS_WINIOS_WINDOW_GET_STATE` (564), then restores to `Normal`.
  5. Posts a user notification toast via `SYS_WINIOS_NOTIF_POST` (558) with title `"Interactive Peer"` and body `"Handshake OK"`.
  6. Destroys peer window and terminates with exit code `94`.
- **Execution Conformance**: Successfully executes in test harness `stage226_20_multi_app_tests`, confirming clean multi-process execution with exit code `94`.

## 3. Multi-Process Concurrency & Isolation
When both applications execute concurrently:
- Each process operates within an independent virtual address space.
- Window handles belonging to PID 93 cannot be destroyed or modified by PID 94 (`kErrnoEPERM` protection).
- System clipboard and notifications bridge cross-application interaction safely through host-controlled buffers.
- All instructions execute via genuine ARM64 instruction decode, retirement, and exception handling.

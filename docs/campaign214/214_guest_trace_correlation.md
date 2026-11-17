# Campaign 214 Stage 214.2 — Guest Instruction and Syscall Trace Correlation Report

## 1. Overview and Objectives

Stage 214.2 establishes a complete, deterministic, and reproducible trace correlation pipeline connecting EL0 ARM64 guest execution, SVC immediate traps, Darwin and WinIOS Category C syscall handling, input event delivery with coordinate translation, surface damage tracking, compositor blending, and host presentation / screenshot capture.

Previously, test suites verified host-side assertions (such as window count or console output strings). Stage 214.2 introduces the architectural `GuestTraceLogger` (`src/kernel/guest_trace_logger.h/.cpp`) to provide end-to-end evidence connecting guest assembly instructions to resulting framebuffer mutations.

---

## 2. Trace Architecture & Schema

Each event in the guest execution flow is captured with structured metadata:

1. **Instruction Trace (`InstructionTraceRecord`):**
   - Guest PID and process identity (e.g. `guest_shell`, `counter_app`, `notes_app`).
   - Program Counter (PC) virtual address.
   - 32-bit ARM64 opcode.
   - Architectural retired instruction index.

2. **Syscall Trace (`SyscallTraceRecord`):**
   - Guest PID and process identity.
   - Syscall number (e.g. 1 for `SYS_exit`, 500 for `SYS_winios_window_create`, 503 for `SYS_winios_surface_present`).
   - Immediate value decoded from ARM64 `SVC #imm` instruction (e.g. `0x80` for Darwin syscalls).
   - Parameter registers $X0$ through $X5$.
   - Validated guest virtual addresses and buffer lengths.
   - Syscall return value ($X0$) and error code (`kErrno*`).
   - Semantic details.

3. **Input Delivery Trace (`InputTraceRecord`):**
   - Target guest PID.
   - Event type (`TouchDown=1`, `TouchUp=2`, `TouchMove=3`).
   - Display/screen coordinates $(x, y)$.
   - Window-local translated coordinates $(x_{local}, y_{local})$.
   - Target Window ID.
   - Timestamp in microseconds.

4. **Framebuffer & Presentation Trace (`FramebufferTraceRecord`):**
   - Monotonic frame sequence counter.
   - Surface ID and owning Window ID.
   - Operation type (`fill`, `update`, `present`, `composite`, `host_present`).
   - Bounding damage rectangle $[x, y, w, h]$.
   - Correlated screenshot artifact filename and SHA-256 digest.

---

## 3. Demonstration Trace Correlation Validation

Unit test `stage214_2_trace_correlation_tests.exe` verifies:
- Multi-process instruction sample recording.
- Full 6-argument register preservation and decoding.
- Coordinate transformation logging from screen-space to window-space.
- Pairing of surface present events with compositor passes and output screenshot artifacts.
- Deterministic Markdown export.

Stage 214.2 is fully verified.

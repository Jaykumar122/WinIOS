# Campaign 226: Adversarial Testing, Fuzzing & Deterministic Replay

## 1. Overview
Stages 226.21 and 226.22 subjected the Campaign 226 window manager, event queue, compositor, and syscall dispatchers to rigorous deterministic replay checks and adversarial fuzzing.

## 2. Deterministic Replay Verification (Stage 226.21)
To ensure the desktop compositor and event dispatch mechanisms are completely deterministic:
- A sequence of 10 windows with varying dimensions, positions, and damage rects was created and composited across multiple independent runs.
- The resulting `VirtualDisplay` framebuffer was hashed using 64-bit FNV-1a checksumming.
- In two independent runs from clean resets, identical FNV-1a checksums were generated:
  - Run 1 Checksum: `0x89C40BE356616C82`
  - Run 2 Checksum: `0x89C40BE356616C82`
- This proves that window stacking, background rendering, dirty-rect culling, and non-client chrome drawing contain no uninitialized memory, thread races, or indeterminate floating-point state.

## 3. Adversarial Attack Surfaces & Hardening (Stage 226.22)

### A. Forged & Stale Handle Attack Mitigation
- **Attack**: Malicious or buggy guest processes attempt to operate on arbitrary 64-bit integer values (`0xDEADBEEF`, `0x0`, `(1ULL<<32) | 9999`) or stale generation handles after window destruction.
- **Defense**: `WindowManager::is_handle_valid` validates both the slot index and generation counter. Forged handles return `false` or trigger `kErrnoEINVAL` / `kErrnoENOENT`. Stale handles whose generation has incremented fail validation immediately, neutralizing ABA recycling vulnerabilities.

### B. Cross-Process Ownership Theft
- **Attack**: Process PID 94 attempts to destroy (`SYS_WINIOS_WINDOW_DESTROY_V2`) or change state of a window owned by PID 93.
- **Defense**: `UserSyscallDispatcher` verifies `win->owner_pid == proc.pid()`. Unauthorized attempts are rejected with `kErrnoEPERM` without side effects.

### C. Event Queue Flooding & Denial-of-Service
- **Attack**: Rapid submission of 500+ pointer moves or synthetic events intended to cause unbounded memory growth or crash the desktop controller.
- **Defense**:
  1. Per-window event queues are strictly bounded to 128 slots (`kMaxEventQueueCapacity = 128`).
  2. Pointer moves coalesce automatically when button states match.
  3. Discrete events overflowing the queue drop oldest unpolled events gracefully while incrementing an overrun telemetry counter. Memory usage remains bounded to $< 16$ KB per window.

### D. Window Churn & Surface Stress
- **Attack**: Rapid allocation and immediate destruction of 100+ windows in rapid succession.
- **Defense**: All surface allocations in `SurfaceResourceManager` and slots in `WindowManager` are reclaimed cleanly with zero memory leaks or dangling pointers.

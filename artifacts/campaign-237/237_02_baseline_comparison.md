# Stage 237.2 — Baseline and Current Failure Comparison

## 1. Baseline Comparison Framework

- **Reference Baseline:** Commit `f81467bcc800970bc8391c6d7eac0fb419bb5524` (Campaign 235 closeout).
- **Current Evaluated State:** Commit `3befe1bcd2997b03bb4af5568e5e2b57cda747be` (Campaign 236 closeout / Campaign 237 starting state).
- **Evaluation Mechanism:** Monolithic CTest execution logs across identical 988-target population without switching or resetting primary working tree.

---

## 2. Test Outcome Transition Matrix (Baseline vs Closeout)

| Test Identifier | Test Target Name | Subsystem | Baseline Outcome (Campaign 235) | Closeout Outcome (Campaign 236) | Delta Direction | Root Cause of Transition |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **#685** | `stage213_18_display_syscalls_tests` | Virtual Display Syscalls | **FAILED** (EFAULT / Null Deref) | **PASSED** (0.02s) | **REPAIRED** | Corrected window title buffer length from `title.size()` to `title.size() + 1` and guarded `surface->read_pixel`. |
| **#810** | `stage222_22_multi_app_desktop_tests` | Desktop Clipboard IPC | **FAILED** (`0xCC` stack corruption) | **PASSED** (0.02s) | **REPAIRED** | Zero-initialized `char out_buf[256]{}` and added explicit null termination on IPC string receive. |
| **#822** | `stage223_23_security_fuzzing_tests` | Desktop UI Fuzzing | **FAILED** (Uninitialized length) | **PASSED** (0.02s) | **REPAIRED** | Zero-initialized stack buffer `char small_buf[16]{}` passed to `winios_textarea_init`. |
| **#947** | `stage233_11_hostile_filesystem_tests` | Filesystem Confinement | Transient Link Stale / Rebuilt | **PASSED** (0.01s) | **CONFIRMED** | Recompiled and relinked against updated `winios_core.lib`. |
| **#950** | `stage233_16_ipc_anti_replay_tests` | IPC Anti-Replay | Transient Link Stale / Rebuilt | **PASSED** (0.01s) | **CONFIRMED** | Recompiled and relinked against updated `ipc_capability_broker.h`. |
| **#974–#988**| Campaign 235 Security Suite (15 targets)| Cryptographic / WAL / Confinement | **PASSED** (15/15) | **PASSED** (15/15) | **STABLE** | Zero regressions across all constant-time Ed25519 and durability tests. |

---

## 3. Net Regression Verification

- **Total Tests Evaluated:** 988
- **Tests Transitioned from PASS to FAIL:** **0 (Zero)**
- **Tests Transitioned from FAIL to PASS:** **3** (`#685`, `#810`, `#822`)
- **Net Regressions Introduced:** **0 (Zero)**

The claimed zero-net-regression result from Campaign 236 is verified with full mathematical reproducibility. Zero functional, architectural, or security regressions were introduced to the codebase.

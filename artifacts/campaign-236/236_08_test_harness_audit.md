# Campaign 236.8 — Test Harness Architecture and Infrastructure Audit

**Campaign:** 236  
**Stage:** 236.8  
**Target Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64, MSVC C++20, CMake 3.31.6  
**Research Target:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`), ARM64 Mach-O, 16 KB granule  

---

## 1. Executive Summary

A comprehensive architectural audit of all test harnesses in `tests/unit/` was conducted. Across the 922 test implementation files, test harnesses have evolved through four distinct eras:
1. **Era 1 (Stages 211–214):** Primitive macro-based assertions (`TEST_CHECK`, `TEST_ASSERT`) tracking local failure counts.
2. **Era 2 (Stages 215–218):** Standard C `<cassert>` assertions (`assert(...)`), introducing modal dialog risks on MSVC Debug.
3. **Era 3 (Stages 219–227):** Mock-heavy integration harnesses simulating CPU execution loops, WindowManager, and IsolatedStorage.
4. **Era 4 (Stages 228–235):** Production-grade security, cryptographic, and fuzzing harnesses with strict exception handling and resource teardown.

---

## 2. Assertion Paradigm Inventory & Risk Analysis

| Assertion Macro / Mechanism | Eras Used | Abort Mechanism | Stderr Diagnostics | MSVC Debug Dialog Risk | Recommendation |
|---|---|---|---|---|---|
| `TEST_ASSERT(cond)` | Eras 1, 3, 4 | `std::exit(1)` | High (Prints file, line, expression) | **None (Safe)** | Preferred standard across all tests |
| `assert(cond)` (`<cassert>`) | Era 2 (215–217) | `_assert()` / `abort()` | Moderate (CRT output) | **High (Modal GUI dialog)** | Replace with `TEST_ASSERT` or suppress CRT dialogs |
| `TEST_CHECK(cond, msg)` | Era 1 (211–213) | Non-fatal (Increments failure counter) | High (Prints descriptive message) | **Low (Cascade crash risk on null dereference)** | Pair with fatal checks before dereferencing pointers |

### Key Risk: The Non-Fatal Check Cascade
In tests utilizing non-fatal `TEST_CHECK` (e.g. `stage213_18_display_syscalls_tests.cpp`), a failed condition (e.g. `TEST_CHECK(win && win->surface, "Window has valid surface")`) records a failure but continues execution into:
```cpp
win->surface->read_pixel(proc->pid(), 20, 20, px_val);
```
When `win->surface` is null, this causes a fatal access violation (`0xC0000005`, `***Exception: SegFault`), terminating the test runner before subsequent checks can execute.

---

## 3. Working Directory & File System Isolation Audit

### Hardcoded File Paths in Test Working Directories
Multiple guest application tests write files relative to the host process current working directory (`D:\WinISO26\WinIOS\build\`):
- `stage222_20_interactive_notes_tests`: `"active_note.txt"`
- `stage223_20_notes_v2_tests`: `"notes_v2.txt"`
- `stage224_22_platform_settings_tests`: `"platform_settings.dat"`
- `stage225_21_device_center_tests`: `"device_telemetry.log"`

### Risks Identified
1. **Concurrency Hazard:** If tests are ever run in parallel (`ctest -j N`), concurrent tests writing to the same filename will collide and corrupt data.
2. **Sequential Stale File Hazard:** If a test exits abnormally or crashes before file deletion, subsequent tests read the stale file rather than freshly created test data.

---

## 4. Hardware Emulation & Memory Manager Harness Audit

Across unit tests, CPU and memory initialization follows a consistent pattern:
- `Memory mem(0x1000000ULL)` to `Memory mem(0x20000000ULL)` (16 MB to 512 MB).
- `PhysicalPageManager ppm(mem, base, size, alloc_base)`.
- `UserAddressSpace uas(mem, ppm, root_l2, name)`.
- Page granularity: **16 KB (`0x4000`)** in accordance with Apple A16 Bionic (`T8120`) hardware profile.

**Audit Finding:** The memory and CPU harness implementation is highly robust and compliant with ARM64 EL0/EL1 architectural specifications. Page translation, TLB invalidation, and TCR/MAIR register setups are strictly deterministic.

---

## 5. Test Harness Hardening Recommendations (Stage 236.13)

1. **Implement `tests/common/test_harness_common.h`:**
   - Define a unified `TEST_ASSERT` macro that logs to `std::cerr` and calls `std::exit(1)`.
   - Call `disable_msvc_assert_dialogs()` at the start of every test `main()`.
2. **Defensive Pointer Checks:**
   - In all `TEST_CHECK` macros where a pointer validity is checked, follow with an immediate early exit or fatal assert if the pointer is null.
3. **Directory Sandboxing:**
   - Use test-unique temporary directories or in-memory VFS backends for guest storage testing to eliminate filesystem collisions.

---

## 6. Stage 236.8 Sign-Off

The test harness audit is complete. Architecture strengths, historical divergence points, and concrete remediation standards have been established.

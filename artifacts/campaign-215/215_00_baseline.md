# WinIOS Campaign 215 — Stage 215.0 Baseline Integrity & Repository Audit

## 1. Baseline Integrity Summary

- **Baseline Commit:** `4b0421873594bab2fd24af9ccb66580370b5d6ee`
- **Baseline Git Branch:** `campaign-191r`
- **HEAD Commit Subject:** `feat(campaign-214): Guest UI Framework, Application Lifecycle, Rendering Reliability & Interactive Window Validation`
- **Working Tree State at Entry:** Clean (0 uncommitted changes, 0 untracked files).
- **Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), 16 KB guest translation granule.
- **Host System:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`), CMake 3.31.6, CTest.

---

## 2. Protected Paths Audit

| Path | Required Status | Actual Status / SHA-256 Hash | Integrity Check |
| :--- | :--- | :--- | :--- |
| `D:\WinISO26\WinISO26_README.md` | Must NOT exist | `Test-Path = False` | **VERIFIED ABSENT** |
| `D:\WinISO26\WinIOS_README.md` | SHA-256 Match | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **100% BIT-FOR-BIT MATCH** |
| `D:\WinISO26\WinIOS\docs\progress.md` | SHA-256 Match | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **100% BIT-FOR-BIT MATCH** |
| `D:\WinISO26\Inferno-Research/` | Must NOT be modified | Directory present, 0 modifications | **UNTOUCHED** |

---

## 3. Baseline Test Execution Results (Debug & Release)

CTest registers exactly **711 tests** (Tests #1 to #711).

| Configuration | Registered Tests | Passed | Failed | Skipped | Effective Pass Rate |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Debug** | 711 | 710 | 0 | 1 (`#383 inferno_integration_tests`) | 100% of runnable (710/710) |
| **Release** | 711 | 710 | 0 | 1 (`#383 inferno_integration_tests`) | 100% of runnable (710/710) |

*Note on Skipped Test #383:* Test #383 (`inferno_integration_tests`) requires external QEMU harness configuration (`WINIOS_INFERNO_EXE` environment variable). In absence of external ChefKiss Inferno binaries, the test safely outputs `SKIPPED` and exits with code 0. CTest reports `100% tests passed out of 711` while noting `383 - inferno_integration_tests (Skipped)`.

---

## 4. Subsystem Inventory (Starting State)

1. **Package Management (`src/loader/app_package.h/.cpp`):**
   - Implements `AppManifest` definition and `AppPackageValidator`.
   - Validates `app_id`, `version`, resource budgets (`max_memory_bytes`, `max_surfaces`), and binary SHA-256.
2. **Virtual Filesystem (`src/fs/vfs.h/.cpp`, `pipe.h/.cpp`):**
   - In-memory node hierarchy (`VfsNode`, `VfsDirectory`, `VfsFile`), POSIX file descriptors, read/write/seek/close, and unbuffered pipes.
3. **Application Preferences (`src/kernel/app_preferences.h/.cpp`):**
   - Thread-safe, per-application isolated typed store (`get_bool`, `get_int`, `get_float`, `get_string`).
   - JSON snapshot serialization and restore mechanism.
4. **Syscall Dispatchers (`src/kernel/user_syscall_dispatcher.h/.cpp`):**
   - Handles POSIX BSD syscalls (read, write, exit, getpid, fork, execve, wait4, mmap, etc.).
   - Category C custom Darwin syscalls (500: `SYS_winios_window_create`, 501: `SYS_winios_window_destroy`, 502: `SYS_winios_surface_update`, 503: `SYS_winios_surface_fill`, 504: `SYS_winios_surface_present`, 505: `SYS_winios_poll_event`, 506: `SYS_winios_set_window_title`).
5. **Input Subsystem (`src/input/`, `src/runtime/ui/`):**
   - `VirtualInputDevice`, `TouchEvent`, `TouchEventQueue`, `InputRouter`.
   - `GestureRecognizer`: `Tap`, `DoubleTap`, `LongPress`, `Pan`, `Swipe`, `Cancel`.
   - `EventDispatcher`: Pointer capture, keyboard focus management, disabled view handling.
6. **Graphics & Presentation (`src/graphics/`, `src/ui/`):**
   - `VirtualDisplay`, `DisplaySurface`, `WindowManager`, `Compositor`, `FrameAccounting`.
   - `DisplayWindow`: Win32 native window presentation (`PresentationMode::DesktopWindow`) with backbuffer `StretchDIBits` and headless presentation (`PresentationMode::Headless`).
7. **Application Lifecycle (`src/kernel/app_lifecycle_manager.h/.cpp`):**
   - 8-State lifecycle machine: `Created`, `Starting`, `Foreground`, `Background`, `Suspended`, `Resuming`, `Terminating`, `Terminated`.
   - Complete resource cleanup (windows, surfaces, theme allocations) on termination.
8. **Guest UI Framework (`src/runtime/ui/`):**
   - Hierarchic `View` and `ViewGroup` with cycle rejection.
   - `LayoutEngine` (`VBox`, `HBox`), `TextRenderer` (8x8 bitmap glyphs), `Controls` (`Label`, `Button`, `Checkbox`, `Toggle`, `TextInput`, `ProgressBar`, `ScrollView`), `RenderContext` with scissor clipping and dirty region tracking.

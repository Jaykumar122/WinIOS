# Campaign 227 — Stage 227.0: Baseline and Integrity Verification

## 1. Baseline Environment & Commit
- **Repository Root**: `D:\WinISO26\WinIOS\`
- **Branch**: `campaign-191r`
- **Required Baseline HEAD**: `3beabc50b226af7f2ab1c95a034ff1474d162cf0`
- **Actual HEAD**: `3beabc50b226af7f2ab1c95a034ff1474d162cf0` (Matches exact)
- **Worktree State**: Clean (`git status --short` returns empty)
- **Host**: Windows 11 x64
- **Host Compiler**: MSVC C++20 (`/W4 /WX /permissive-`)
- **Build System**: CMake 4.4.4 / CTest 4.4.4
- **ARM64 Cross-Compiler**: Zig 0.17.0 (`-target aarch64-macos -nostdlib -fno-builtin -fno-sanitize=all -O2`)
- **Research Profile**: iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), ARM64 EL0, 16 KB translation granule
- **Public XNU Reference**: Pinned `xnu-10002.1.13`

## 2. Invariant Protected Assets Verification
All 4 invariant checks passed with 100% bit-for-bit fidelity:

1. `D:\WinISO26\WinISO26_README.md`: Nonexistent (`False`)
2. `D:\WinISO26\WinIOS_README.md`: SHA-256 `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (Verified)
3. `D:\WinISO26\WinIOS\docs\progress.md`: SHA-256 `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (Verified)
4. `D:\WinISO26\Inferno-Research\`: Untouched (`True`)

## 3. Existing CTest Inventory & Baseline Pass
- **Discovered Test Executables**: 860 tests
- **Campaign 226 Baseline Test Suite**: 11/11 Passed (100%) in 0.42 sec:
  - `stage226_3_window_manager_tests`: Passed (0.02s)
  - `stage226_4_compositor_tests`: Passed (0.02s)
  - `stage226_6_pointer_routing_tests`: Passed (0.02s)
  - `stage226_7_keyboard_focus_tests`: Passed (0.02s)
  - `stage226_10_notification_overlay_tests`: Passed (0.02s)
  - `stage226_11_desktop_event_loop_tests`: Passed (0.01s)
  - `stage226_12_abi_v2_syscall_tests`: Passed (0.02s)
  - `stage226_19_multiwindow_app_tests`: Passed (0.09s)
  - `stage226_20_multi_app_tests`: Passed (0.09s)
  - `stage226_21_deterministic_replay_tests`: Passed (0.02s)
  - `stage226_22_adversarial_tests`: Passed (0.01s)

## 4. Current Syscall & Capability Allocation Inventory
- **Syscall Map (Category C)**:
  - 500..512: Windowing & Surface v1.1
  - 513..521: Private Storage & IPC v1.2
  - 530..536: Extended Windows & Clipboard v1.6
  - 537..545: Config, Logging & IPC v1.8
  - 546..560: Hardware Virtual Devices v1.9
  - 561..570: Desktop Window Manager & Interaction v2.0
- **Allocated Capability Mask Bits**:
  - `DisplayWindow` (`1 << 0`) through `NotificationPost` (`1 << 20`)
  - Target bits for Document Services in Campaign 227:
    - `DocumentRead` (`1 << 21`)
    - `DocumentWrite` (`1 << 22`)
    - `DocumentCreate` (`1 << 23`)
    - `DocumentDelete` (`1 << 24`)
    - `DocumentShare` (`1 << 25`)
    - `DocumentEnumerate` (`1 << 26`)

Stage 227.0 baseline verification complete and recorded.

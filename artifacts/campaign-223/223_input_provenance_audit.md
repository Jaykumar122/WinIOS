# WinIOS Campaign 223 — Input Provenance Audit & Host Integration (Stage 223.11)

## 1. Overview

Stage 223.11 validates input provenance classification, coordinate translation, pointer capture, and telemetry integrity between the Windows host and guest applications.

---

## 2. Input Provenance Taxonomy

Every input event delivered through the runtime carries an immutable `winios_input_provenance_t` descriptor:
- `PhysicalWindowsInput`: Genuine physical hardware input received via the Win32 message loop (`WM_MOUSEMOVE`, `WM_LBUTTONDOWN`, `WM_KEYDOWN`).
- `WindowsMessageInjected`: Synthetic messages posted to the Win32 window queue.
- `TestInjected`: Programmatic injection from C++ test harnesses.
- `GuestGenerated`: Cross-process synthetic events or UI automation.
- `SyntheticHostEvent`: Internal window manager notifications (auto-blur, close).

---

## 3. Pointer Capture & Dragging Continuity

During mouse dragging (e.g. dragging a slider or window title bar):
1. `MouseDown` captures the pointer to the window under the cursor.
2. Subsequent `MouseMove` events are dispatched exclusively to the captured window's owning process, even if the cursor leaves the window bounds.
3. `MouseUp` releases capture immediately.

---

## 4. Verification

`tests/unit/stage223_11_input_provenance_tests.cpp` (100% PASS):
- Telemetry descriptor layout (`sizeof(winios_input_provenance_t) == 24`, `sizeof(winios_event_v2_t) == 64`).
- Mouse drag capture retention across window boundaries.
- Clean release of capture upon pointer release and window destruction.

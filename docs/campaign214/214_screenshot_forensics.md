# Campaign 214 Stage 214.1 — Screenshot and Framebuffer Forensic Audit Report

## 1. Executive Summary

This forensic audit investigates the 7 screenshot artifacts produced during Campaign 213's 13-step demonstration:
- `screenshot_01_shell_homescreen.bmp`
- `screenshot_02_counter_app_launched.bmp`
- `screenshot_03_counter_app_incremented.bmp`
- `screenshot_04_return_to_shell.bmp`
- `screenshot_05_notes_app_launched.bmp`
- `screenshot_06_notes_app_checked.bmp`
- `screenshot_07_shell_closeout.bmp`

The audit independently verified BMP file headers, pixel payload integrity, SHA-256 hashes, pairwise diffs, changed pixel counts, bounding boxes, and correlated these observations with guest application assembly execution and compositor dispatch behavior.

---

## 2. Screenshot Inventory and Header Verification

All screenshots share the standard Windows BMP uncompressed 32-bit BGRA format:
- **Magic:** `BM` (`0x4D42`)
- **File size:** 3,840,054 bytes
- **Header size:** 14-byte `BITMAPFILEHEADER` + 40-byte `BITMAPINFOHEADER`
- **Pixel data offset:** 54 bytes (`0x36`)
- **Dimensions:** 800 x 1200 pixels
- **Bits per pixel:** 32 bpp (4 bytes per pixel: BGRA)
- **Row stride:** 3,200 bytes per row (800 * 4)
- **Payload size:** 800 * 1200 * 4 = 3,840,000 bytes

### Individual File Hashes:
| Index | File Name | SHA-256 | Distinct Group |
|---|---|---|---|
| 01 | `screenshot_01_shell_homescreen.bmp` | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` | Group A |
| 02 | `screenshot_02_counter_app_launched.bmp` | `89f9bd9e0fe0dffa6eadc934c68adb9ecb918c788845073d122623a202a56e7a` | Group B |
| 03 | `screenshot_03_counter_app_incremented.bmp` | `89f9bd9e0fe0dffa6eadc934c68adb9ecb918c788845073d122623a202a56e7a` | Group B (identical) |
| 04 | `screenshot_04_return_to_shell.bmp` | `89f9bd9e0fe0dffa6eadc934c68adb9ecb918c788845073d122623a202a56e7a` | Group B (identical) |
| 05 | `screenshot_05_notes_app_launched.bmp` | `bc3d697ae85869a0aa62d5e713500c300bb128df59b2ea9f8ddbe5e4edb892cf` | Group C |
| 06 | `screenshot_06_notes_app_checked.bmp` | `1610a8668d538f82575435b092f1b40e16701fc6b5a1b043677712d300643eaf` | Group D |
| 07 | `screenshot_07_shell_closeout.bmp` | `1610a8668d538f82575435b092f1b40e16701fc6b5a1b043677712d300643eaf` | Group D (identical) |

---

## 3. Pairwise Pixel Comparisons

1. **`screenshot_01` -> `screenshot_02`:**
   - Changed pixels: 630,000 pixels (700 x 900)
   - Bounding box: `[x: 50..749, y: 150..1049]`
   - Explanation: Counter application window (700x900 at display x=50, y=150) was mapped, rendered, and composited over the shell background.

2. **`screenshot_02` -> `screenshot_03`:**
   - Changed pixels: 0
   - Identical SHA-256: `89f9bd9e0fe0dffa6eadc934c68adb9ecb918c788845073d122623a202a56e7a`
   - Root Cause: In `counter_app.s`, the tap handler received `SYS_winios_input_poll`, parsed the touch down event, and incremented guest register `x20` from 0 to 1. It then invoked `SYS_winios_surface_present` (503). However, `counter_app.s` did NOT invoke any pixel redraw routine into its surface memory before calling present. Consequently, the surface memory buffer contained the exact initial pixels, and the compositor pass blended identical pixels.

3. **`screenshot_03` -> `screenshot_04`:**
   - Changed pixels: 0
   - Identical SHA-256: `89f9bd9e0fe0dffa6eadc934c68adb9ecb918c788845073d122623a202a56e7a`
   - Root Cause: On exit, the counter app destroyed its window via `SYS_winios_window_destroy` (501). However, the compositor's `composite()` function was not invoked upon window destruction. The `VirtualDisplay` buffer retained the frame from Step 3 without re-blending the newly uncovered shell window. Thus, when `take_screenshot(screenshot_04)` was called, the framebuffer had not been recomposited.

4. **`screenshot_04` -> `screenshot_05`:**
   - Changed pixels: 630,000 pixels (700 x 900)
   - Bounding box: `[x: 50..749, y: 150..1049]`
   - Explanation: Notes app created its window at `[x=50, y=150, w=700, h=900]`, rendered its checklist UI, and called `SYS_winios_surface_present`, triggering a compositor pass.

5. **`screenshot_05` -> `screenshot_06`:**
   - Changed pixels: 2,500 pixels (50 x 50)
   - Bounding box (Display Coordinates): `[x: 100..149, y: 350..399]` (Local Coordinates: `[x: 50..99, y: 200..249]`)
   - Explanation: Notes app processed the touch event on Item 1's checkbox, redrew the 50x50 checkbox from light gray (`0xFFE5E5EA`) to system green (`0xFF34C759`), and presented. The compositor re-rendered the 2,500 updated pixels into the virtual display.

6. **`screenshot_06` -> `screenshot_07`:**
   - Changed pixels: 0
   - Identical SHA-256: `1610a8668d538f82575435b092f1b40e16701fc6b5a1b043677712d300643eaf`
   - Root Cause: Exactly identical to Step 4; Notes app exited and called `SYS_winios_window_destroy`, but `WindowManager` did not automatically trigger `Compositor::composite()`. The display buffer retained the Notes app frame.

---

## 4. Remediation Plan for Campaign 214

To ensure that every meaningful UI state transition produces distinct, verified, guest-driven pixels:

1. **Automatic Compositor Invalidation on Window Lifecycle Events:**
   In `src/graphics/window_manager.cpp`, modifying window state (destruction, hiding, z-order change) must mark dirty damage and trigger a compositor pass if a compositor is registered or requested.
2. **Dynamic UI Framework Re-rendering (Stage 214.6 - 214.12):**
   The new reusable Guest UI Framework must mandate that any state mutation (e.g. counter increment, button press, toggle) invokes `invalidate()` on the modified view, triggering a partial or full re-render into the guest surface prior to invoking `SYS_winios_surface_present`.
3. **Dirty-Rect and Frame Accounting Verification (Stage 214.4 & 214.11):**
   Add explicit checks ensuring dirty rectangles accurately bound changed pixels and frame presentation counters reflect whether pixels were newly rendered or repeated.

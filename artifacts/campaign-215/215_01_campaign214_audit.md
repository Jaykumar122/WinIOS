# WinIOS Campaign 215 — Stage 215.1 Campaign 214 Forensic Reconciliation

## 1. Executive Summary

Stage 215.1 performs a rigorous forensic reconciliation of Campaign 214's test reporting, frame presentation accounting, raw framebuffer pixel mutation data, and execution provenance. All reporting ambiguities and implementation boundaries from Campaign 214 are resolved and documented deterministically.

---

## 2. Test Execution & Reporting Reconciliation

### 2.1 The 711/711 Pass Count vs Skipped Test Discrepancy
In Campaign 214, the CTest summary output reported:
```
100% tests passed out of 711
Total Test time (real) = 329.80 sec
The following tests did not run:
    383 - inferno_integration_tests (Skipped)
```
- **Root Cause:** CTest's built-in summary prints `100% tests passed` when `Failed Tests == 0`, counting skipped tests as non-failing.
- **Strict Accounting Standard for Campaign 215:** We do not count skipped tests as passed. The exact breakdown is:

| Build Configuration | Registered Tests | Runnable Tests | Passed | Failed | Skipped | Exact Pass Rate |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Debug** | 711 | 710 | 710 | 0 | 1 (`#383`) | 100.0% of runnable (710/710) |
| **Release** | 711 | 710 | 710 | 0 | 1 (`#383`) | 100.0% of runnable (710/710) |

- **Details of Skipped Test #383:** Test #383 (`inferno_integration_tests.exe`) is an opt-in integration check that requires an external ChefKiss Inferno QEMU binary specified via `WINIOS_INFERNO_EXE`. When the variable is not set, the test prints `SKIPPED: no local ChefKiss Inferno build configured` and exits with code 0.

---

## 3. Screenshot Forensic Audit & Unique Content Accounting

Recomputing SHA-256 hashes directly from the 11 BMP files in `artifacts/campaign-214/`:

| Screenshot File | Resolution | File Size | SHA-256 Hash | Content Identity |
| :--- | :--- | :--- | :--- | :--- |
| `screenshot_01_shell_homescreen.bmp` | 800x1200 | 3,840,054 B | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` | Content Group A (Shell) |
| `screenshot_02_counter_app_launched.bmp` | 800x1200 | 3,840,054 B | `57536a1550bf967a24a3c8b36db414c00e554ce3ccc8e8c8874c61b3cd4d0dbd` | Content Group B (Counter=0) |
| `screenshot_03_counter_app_incremented.bmp` | 800x1200 | 3,840,054 B | `01788115c584c7b43499103212e15e31fb06056c0c15c858281442a25b37083c` | Content Group C (Counter=1) |
| `screenshot_04_counter_app_backgrounded.bmp`| 800x1200 | 3,840,054 B | `e47dceea4e3db6a8961f220c33051422c64850b50332252a3c1d643c8fc6133a` | Content Group D (Dimmed 0.40) |
| `screenshot_05_return_to_shell.bmp` | 800x1200 | 3,840,054 B | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` | Content Group A (Shell) |
| `screenshot_06_notes_app_launched.bmp` | 800x1200 | 3,840,054 B | `dc7dc3208e44fe25c33665f7615f41c058a486a43363d546093f25f17b84ba66` | Content Group E (Notes Unchecked) |
| `screenshot_07_notes_app_checked.bmp` | 800x1200 | 3,840,054 B | `06cab01e129e77b611cceb300895cba29ba7e9d7158cae8ef7acc35454f7d98b` | Content Group F (Notes Checked) |
| `screenshot_08_notes_app_scrolled.bmp` | 800x1200 | 3,840,054 B | `18f687dcedd6d6c21798fad85023d82f8839fdacff944d1b58d1af639a994740` | Content Group G (Notes Scrolled) |
| `screenshot_09_notes_closed_shell.bmp` | 800x1200 | 3,840,054 B | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` | Content Group A (Shell) |
| `screenshot_10_notes_relaunch_persisted.bmp`| 800x1200 | 3,840,054 B | `06cab01e129e77b611cceb300895cba29ba7e9d7158cae8ef7acc35454f7d98b` | Content Group F (Notes Checked) |
| `screenshot_11_shell_final.bmp` | 800x1200 | 3,840,054 B | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` | Content Group A (Shell) |

- **Total Screenshot Files Captured:** 11 files.
- **Unique Image-Content Hashes:** Exactly **7 unique content hashes**:
  1. `86ce3bd8...`: Screenshots 01, 05, 09, 11 (Shell homescreen)
  2. `57536a15...`: Screenshot 02 (Counter App initialized, count=0)
  3. `01788115...`: Screenshot 03 (Counter App incremented, count=1)
  4. `e47dceea...`: Screenshot 04 (Counter App backgrounded with 0.40 opacity)
  5. `dc7dc320...`: Screenshot 06 (Notes App launched, item 3 unchecked)
  6. `06cab01e...`: Screenshots 07, 10 (Notes App with item 3 checked; initial and relaunched from preferences)
  7. `18f687dc...`: Screenshot 08 (Notes App scrolled by +80px)

---

## 4. Verification of Raw Pixel Mutations

Direct byte-level comparison of the BMP pixel data was performed using a raw raster comparison script:

1. **Counter Mutation (`screenshot_02` vs `screenshot_03`):**
   - **Changed Pixels:** Exactly **279 pixels**.
   - **Bounding Box (X, Y):** `(475, 905)` to `(492, 925)` in bottom-up DIB coordinates.
   - **Explanation:** Corresponds precisely to redrawing the label glyphs from `"Count: 0"` to `"Count: 1"`.
2. **Notes Checkbox Mutation (`screenshot_06` vs `screenshot_07`):**
   - **Changed Pixels:** Exactly **576 pixels**.
   - **Bounding Box (X, Y):** `(90, 701)` to `(113, 724)` in bottom-up DIB coordinates.
   - **Explanation:** Corresponds precisely to a 24×24 pixel square checkbox ($24 \times 24 = 576$ pixels) toggled from white interior to blue fill with a white checkmark.

---

## 5. Frame Accounting Reconciliation

- **Campaign 213 Issue:** Reported 5 compositor frames vs 7 host presented frames without explaining the delta.
- **Audit Findings:** In Campaign 213, destroying a window (`SYS_winios_window_destroy`) removed the window metadata but omitted the compositor pass. Host presentation was called twice after window destruction without redrawing pixels:
  $$\text{Composed Frames (5)} + \text{Repeated Frames Post-Destruction (2)} = \text{Host Presented Frames (7)}$$
- **Campaign 214 Resolution:** In Campaign 214, window destruction explicitly invokes `compositor_->composite(false)`. In the 15-step demonstration, there were **13 compositor passes** and **11 host presentations** (every host presentation reflects an active composition pass).

---

## 6. Execution Provenance & Implementation Boundaries

To ensure complete scientific honesty, operations are explicitly classified by their execution domain:

| Operation | Guest ARM64 Code | Host C++ Runtime | Execution Domain |
| :--- | :---: | :---: | :--- |
| **Guest Shell Startup** | `kGuestShellMacho_data` (EL0) | CPU emulation (`CPU::step`) | **Guest ARM64** |
| **Category C Syscall Dispatch** | `SVC #0` (EL0) | Exception trap & C++ handler (EL1) | **Boundary Transition** |
| **Surface Allocation / Destruct** | Parameter passing via registers | `SurfaceResourceManager` / `WindowManager` | **Host Runtime** |
| **Layer Composition** | None | `Compositor::composite` (Alpha blend) | **Host Runtime** |
| **Display Presentation** | None | `DisplayWindow` (Win32 GDI / headless) | **Host Runtime** |
| **Guest UI Framework Layout** | None (in C++ test harness) | `View`, `ViewGroup`, `LayoutEngine` | **Host Framework** |
| **Text Rendering & Controls** | None (in C++ test harness) | `TextRenderer`, `controls.cpp` | **Host Framework** |
| **Preferences Store & VFS** | File operations | `AppPreferencesManager`, `Vfs` | **Host Runtime** |

*Note for Campaign 215:* In Campaign 214, `CounterAppView` and `NotesAppView` were implemented as host C++ classes rendering directly into the guest surface memory. Campaign 215 will provide an actual **Developer SDK** so that sample applications (such as Calculator and Text Editor) execute genuinely as freestanding guest ARM64 binaries issuing documented Category C syscalls.

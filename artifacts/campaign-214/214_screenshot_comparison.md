# WinIOS Campaign 214 — Framebuffer & Screenshot Forensic Analysis

## Overview
Campaign 214 conducted an exhaustive audit of Campaign 213's framebuffer export pipeline and resolved all root causes of identical screenshot hashes. In Campaign 214, genuine UI framework rendering, partial invalidation, dynamic text mutation, and immediate compositor recomposition on window destruction were implemented and verified across an end-to-end 15-step demonstration.

---

## 1. Screenshot SHA-256 Manifest and Verification

| Filename | Resolution | SHA-256 Hash | State / Description | Comparison vs Baseline |
| :--- | :--- | :--- | :--- | :--- |
| `screenshot_01_shell_homescreen.bmp` | 800x1200 | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` | Shell wallpaper, icons, status bar, dock | Initial baseline |
| `screenshot_02_counter_app_launched.bmp` | 800x1200 | `57536a1550bf967a24a3c8b36db414c00e554ce3ccc8e8c8874c61b3cd4d0dbd` | Counter App window at Z=1, initial count: 0 | Distinct from Shell (`hash_02 != hash_01`) |
| `screenshot_03_counter_app_incremented.bmp` | 800x1200 | `01788115c584c7b43499103212e15e31fb06056c0c15c858281442a25b37083c` | Counter App count incremented: 1 (279 mutated pixels) | **Distinct from 02** (`hash_03 != hash_02`) |
| `screenshot_04_counter_app_backgrounded.bmp` | 800x1200 | `e47dceea4e3db6a8961f220c33051422c64850b50332252a3c1d643c8fc6133a` | Counter App moved to background (dimmed opacity 0.40) | Distinct from 03 & 02 |
| `screenshot_05_return_to_shell.bmp` | 800x1200 | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` | Counter window destroyed; shell recomposited | **Identical to 01** (`hash_05 == hash_01`) |
| `screenshot_06_notes_app_launched.bmp` | 800x1200 | `dc7dc3208e44fe25c33665f7615f41c058a486a43363d546093f25f17b84ba66` | Notes App window at Z=1, checklist items 0..4 | Distinct from Shell & Counter |
| `screenshot_07_notes_app_checked.bmp` | 800x1200 | `06cab01e129e77b611cceb300895cba29ba7e9d7158cae8ef7acc35454f7d98b` | Notes Item 3 toggled checked (576 mutated pixels) | **Distinct from 06** (`hash_07 != hash_06`) |
| `screenshot_08_notes_app_scrolled.bmp` | 800x1200 | `18f687dcedd6d6c21798fad85023d82f8839fdacff944d1b58d1af639a994740` | Notes ScrollView scrolled by +80px | Distinct from 07 |
| `screenshot_09_notes_closed_shell.bmp` | 800x1200 | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` | Notes window destroyed; shell recomposited | **Identical to 01** (`hash_09 == hash_01`) |
| `screenshot_10_notes_relaunch_persisted.bmp` | 800x1200 | `06cab01e129e77b611cceb300895cba29ba7e9d7158cae8ef7acc35454f7d98b` | Notes relaunched (PID 4); restored Item 3 checked | **Identical to 07** (`hash_10 == hash_07`) |
| `screenshot_11_shell_final.bmp` | 800x1200 | `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` | Final shell closeout before clean shutdown | **Identical to 01** (`hash_11 == hash_01`) |

---

## 2. Forensic Analysis of Discrepancies and Root Causes

### Issue A: Counter State Changes in Campaign 213 Did Not Mutate Surface Pixels
- **Observation in Campaign 213:** Screenshots 02, 03, and 04 had identical SHA-256 hashes (`86ce...` or identical app hash).
- **Forensic Diagnosis:** In Campaign 213, `counter_app.s` logged button tap console output and invoked `SYS_winios_surface_present` (504), but did not blit updated font glyphs into its surface backing store.
- **Campaign 214 Resolution:** Implemented `CounterAppView`, `TextRenderer`, and `Label::set_text()`. When `inc_button` is tapped, `count_label_->set_text("Count: 1")` invalidates the bounding box and draws mutated glyph pixels (279 changed pixels), resulting in `hash_03 != hash_02`.

### Issue B: Window Destruction Did Not Trigger Compositor Recomposition
- **Observation in Campaign 213:** Screenshot 04 and 07 still displayed the destroyed window's stale pixels.
- **Forensic Diagnosis:** `SYS_winios_window_destroy` (501) removed the window entry from `WindowManager`, but never called `compositor_->composite()`. Consequently, the virtual display backbuffer was never refreshed with the underlying Shell surface.
- **Campaign 214 Resolution:** In `user_syscall_dispatcher.cpp` and `app_lifecycle_manager.cpp`, window destruction now immediately invokes `compositor_->composite(false)`. This is verified by `hash_05 == hash_01`, `hash_09 == hash_01`, and `hash_11 == hash_01`.

### Issue C: State Persistence and Deterministic Relaunch
- **Observation:** Interactive applications require state persistence across restarts.
- **Campaign 214 Implementation:** Developed `AppPreferencesManager` with process namespace isolation and snapshot serialization. When Notes is closed in Step 12 and relaunched in Step 12d as PID 4, `NotesAppView` reads `item_3` from preferences and renders the checked state.
- **Verification:** `screenshot_10` matches `screenshot_07` bit-for-bit (`hash_10 == hash_07 = 06cab...`), proving deterministic rendering and persistence.

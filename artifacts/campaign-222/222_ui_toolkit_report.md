# WinIOS Campaign 222 — Stage 222.13: Guest UI Toolkit v2 — Interactive Controls & Layout Engine Report

**Campaign:** 222  
**Stage:** 222.13  
**Date:** 2026-11-18  
**Component:** Guest UI Toolkit (`guest-sdk/include/winios/ui/toolkit.h`, `guest-sdk/lib/winios_runtime.c`)  
**Status:** COMPLETE (100% test pass rate)

---

## 1. Executive Summary

Stage 222.13 introduces the reusable, freestanding Guest UI Toolkit v2 for ARM64 guest applications.

Controls implemented and verified:
1. **Interactive Button (`winios_button_t`):**
   - Supports Normal, Hover, and Pressed states with configurable color themes.
   - Handles mouse move, hover detection, press on mouse down, and click dispatch on mouse up inside bounding box.
   - Text centering within button bounds using 8x8 bitmap glyphs.
2. **Text Label (`winios_label_t`):**
   - Supports foreground text coloring and optional background rectangle fills.
3. **Single-Line Text Input (`winios_textinput_t`):**
   - Click-to-focus and focus tracking with blue highlight border (`0xFF007AFF`).
   - Click-to-cursor positioning based on character width calculation ($8\text{px}/\text{char}$).
   - Character insertion at caret position with automatic right-shifting and null-termination.
   - Backspace deletion preceding caret with left-shifting.
   - Left/Right keyboard navigation.
   - Caret rendering at current editing position.
4. **Checkbox (`winios_checkbox_t`):**
   - Interactive toggle on click, state rendering with custom box and checkmark colors.
5. **Progress Bar (`winios_progressbar_t`):**
   - Value mapping between `min_val` and `max_val`, rendering proportional fill bars.

---

## 2. Technical Invariants & Memory Model

- All UI components are pure C structs without heap allocation requirements.
- Applications can allocate controls statically, on the stack, or in guest heaps.
- Rendering operates directly into 32-bit ARGB surface buffers or window framebuffers using software raster routines.

---

## 3. Verification & Evidence

Automated testing in `tests/unit/stage222_13_ui_toolkit_tests.cpp` verified:
1. `test_button_component`: Hover, press, and click event handling; pixel rendering.
2. `test_textinput_and_caret`: Click focus, character typing "HeXllo", backspace deletion "Hello", caret navigation, and defocusing.
3. `test_checkbox_and_progressbar`: State toggle and proportional progress calculation.

**Result:** 100% tests passed.

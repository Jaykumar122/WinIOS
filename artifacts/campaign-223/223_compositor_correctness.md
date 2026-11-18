# WinIOS Campaign 223 — Compositor Correctness & Occlusion Culling (Stage 223.15)

## 1. Overview

Stage 223.15 verifies multi-surface Z-ordered compositing, damage-driven partial redraws, and full surface occlusion culling.

---

## 2. Occlusion Culling Architecture

1. **Back-to-Front vs Front-to-Back**: Surfaces are sorted by Z-order ascending for painter's algorithm rendering. Prior to rendering, an occlusion culling pass evaluates if a higher Z-order surface fully covers lower surfaces.
2. **Culled Surface Elimination**: When a lower surface is fully occluded by an opaque top surface (`opacity == 1.0f`), the lower surface is culled entirely, incrementing `occluded_surfaces_culled` and `occluded_pixels_saved`.
3. **Dirty Clearing**: Culled surfaces have their dirty trackers cleared to ensure synchronization with future frame updates.

---

## 3. Verification

`tests/unit/stage223_16_compositor_damage_tests.cpp` (100% PASS):
- Exact color readback for overlapping windows (top blue window occludes underlying red window).
- Background pixel isolation outside active window boundaries.

# WinIOS Campaign 223 — Text Rendering & Font Architecture (Stage 223.6)

## 1. Overview

Stage 223.6 audits the font rendering pipeline used by the guest UI toolkit and application framework.

---

## 2. Font Engine Specification

- **Font Representation**: 8x8 monochrome bitmap glyph matrix (`font8x8.h`) covering standard ASCII character set `[0x20, 0x7E]`.
- **Row/Column Stride**: Each glyph occupies an 8x8 pixel boundary.
- **Blending**: Text pixels are rendered directly with 32-bit ARGB values onto the surface framebuffer, respecting font color while transparent background pixels preserve the underlying widget surface.
- **Line Metrics**:
  - Glyph Width: 8 px
  - Glyph Height: 8 px
  - Line Stride / Baseline Offset: 12 px (providing 4 px inter-line leading)
  - Word Wrapping: Occurs dynamically when `cur_x + 8 > bounds.x + bounds.width - padding`.

---

## 3. Performance & Memory Impact

- **Zero Runtime Allocations**: Glyphs are stored in const `.rodata` flash/memory; rendering reads bitmasks without memory allocations or decompression.
- **Cache Locality**: 8x8 glyph definitions fit within a single 4 KB memory page, resulting in near 100% L1 instruction/data cache hit rates during string rendering passes.

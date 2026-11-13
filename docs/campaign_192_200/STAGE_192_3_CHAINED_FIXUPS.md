# WINIOS CAMPAIGN 192 — STAGE 192.3 REPORT
## Chained Fixups Format Conformance & Robustness Verification

**Campaign:** 192  
**Stage:** 192.3 (Chained Fixups Format Conformance)  
**Date:** 2026-11-13  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`), Zig `0.17.0`  
**Verdict:** **PASSED (7/7 test suites passed, 100% CTest regression clean, differential match confirmed)**

---

## 1. Executive Summary

In Stage 192.3, the Darwin dynamic linking architecture for modern chained fixups (`LC_DYLD_CHAINED_FIXUPS`) was audited, hardened, and verified for full Apple specification conformance.

Chained fixups replace legacy `LC_DYLD_INFO_ONLY` rebase and binding tables with in-place pointer chains embedded directly within the mapped segments (`__DATA_CONST`, `__DATA`, etc.). While Campaign 191-V introduced initial `DYLD_CHAINED_PTR_64` handling, Stage 192.3 extends both the **host dynamic linker engine** (`winios::loader::DyldEngine` in `src/loader/dyld_engine.cpp`) and the **guest-side dynamic linker** (`tests/artifacts/src/minidyld.c`) to conform to Apple's modern `dyld` chained fixup format specifications.

Key enhancements delivered in this stage:
1. **Multi-Page Chained Fixups:** Validated pointer chains that traverse across contiguous 4 KB / 16 KB virtual page boundaries when intermediate pages have `DYLD_CHAINED_PTR_START_NONE` (`0xFFFF`).
2. **`DYLD_CHAINED_PTR_START_MULTI` Handling:** Implemented Apple's multi-chain overflow mechanism where page start values with bit 15 set (`0x8000`) point to an array of multiple chain starting points for a single page in an overflow table.
3. **Robust Boundary Verification:** Enforced strict bounds checks on all chained fixup sub-tables: header size, starts-in-image offsets, segment starts counts, page starts arrays, import descriptors, and symbol string pool offsets.
4. **Cycle & Infinite Loop Guards:** Hardened chain traversal with loop detection sets and maximum iteration guards to prevent denial-of-service from cyclic or self-referential pointer chains.
5. **Real Artifact & Differential Conformance:** Verified against genuine iOS binary artifact `ios_chain_app` (arm64, platform 2) and `libioschain.dylib`, proving identical patching between host `DyldEngine` and guest `minidyld`.

---

## 2. Technical Implementation Details

### 2.1 Multi-Chain Overflow (`DYLD_CHAINED_PTR_START_MULTI`)

In Apple's modern chained fixup format:
- `page_start[p]` holds the offset within the page for the first pointer in the chain.
- If `page_start[p] == DYLD_CHAINED_PTR_START_NONE` (`0xFFFF`), the page contains no new chain beginnings (though an ongoing chain from an earlier page may pass through it).
- If `page_start[p] & DYLD_CHAINED_PTR_START_MULTI` (`0x8000`) is set, the lower 15 bits (`page_start[p] & ~0x8000`) serve as an index into an overflow chain start table located immediately after the `page_start[page_count]` array:
  $$\text{overflow\_offset} = \text{page\_start\_array} + \text{page\_count} \times 2 + (\text{pstart} \& 0x7FFF) \times 2$$
- The overflow table contains a sequence of `uint16_t` chain offsets terminating with `0xFFFF`.

#### Host Implementation (`src/loader/dyld_engine.cpp`):
```cpp
if ((pstart & DYLD_CHAINED_PTR_START_MULTI) != 0) {
    const std::uint16_t overflow_idx = pstart & ~DYLD_CHAINED_PTR_START_MULTI;
    const std::size_t overflow_base = starts_in_seg_offset + sizeof(DyldChainedStartsInSegmentRaw) 
                                      + seg_hdr.page_count * sizeof(std::uint16_t);
    std::size_t cur_overflow_off = overflow_base + overflow_idx * sizeof(std::uint16_t);
    while (cur_overflow_off + sizeof(std::uint16_t) <= blob_size) {
        std::uint16_t chain_start_off = 0;
        std::memcpy(&chain_start_off, blob + cur_overflow_off, sizeof(chain_start_off));
        if (chain_start_off == DYLD_CHAINED_PTR_START_NONE) break;
        chain_starts.push_back(chain_start_off);
        cur_overflow_off += sizeof(std::uint16_t);
    }
}
```

#### Guest Implementation (`tests/artifacts/src/minidyld.c`):
```c
if ((pstart & DYLD_CHAINED_PTR_START_MULTI) != 0) {
    uint16_t ov_idx = (uint16_t)(pstart & ~DYLD_CHAINED_PTR_START_MULTI);
    uint32_t ov_base = starts_in_seg_off + (uint32_t)sizeof(DyldChainedStartsInSegmentRaw)
                       + seg_hdr.page_count * 2U;
    uint32_t cur_ov = ov_base + ov_idx * 2U;
    while (cur_ov + 2U <= fixups_size) {
        uint16_t off = 0;
        minidyld_memcpy(&off, fixups_blob + cur_ov, 2);
        if (off == DYLD_CHAINED_PTR_START_NONE) break;
        apply_single_chain(seg_hdr.segment_offset + ((uint64_t)p * seg_hdr.page_size) + off, ...);
        cur_ov += 2U;
    }
}
```

### 2.2 Table Bounds and Validation Protections

1. **Header Validation:**
   - Validated `fixups_version == 0`.
   - Validated `starts_offset`, `imports_offset`, `symbols_offset` are all within `blob_size`.
2. **Starts in Image Table:**
   - Validated `seg_count <= 256`.
   - Validated `seg_info_offset[s]` for each segment falls within `blob_size`.
3. **Starts in Segment Table:**
   - Validated `page_size` is non-zero and a power of two.
   - Validated `pointer_format == DYLD_CHAINED_PTR_64` (clean rejection of unsupported ARM64E / 32-bit formats with `UnsupportedChainedPointerFormat`).
   - Validated `page_count * 2 <= blob_size - starts_in_seg_offset`.
4. **Imports & Symbol Table Bounds:**
   - Bound-checked every `import_index < imports_count`.
   - Bound-checked `name_offset < blob_size - symbols_offset`.
   - Enforced null-termination for symbol strings.

### 2.3 Guest Binary Recompilation

`tests/artifacts/src/minidyld.c` was recompiled with `zig cc` using target `aarch64-macos`:
- Binary: [`tests/artifacts/bin/minidyld`](file:///D:/WinISO26/WinIOS/tests/artifacts/bin/minidyld)
- Size: 67,512 bytes
- SHA-256: `f26ac2023489ffdcbf3b6d3dfc6c06a48d5ebda5d7be2bb96635be916356e3fa`

---

## 3. Test Verification Matrix

A dedicated test suite [`tests/unit/stage_192_3_chained_fixups_conformance_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_192_3_chained_fixups_conformance_tests.cpp) was developed and registered as target `stage_192_3_chained_fixups_conformance_tests`:

| Test # | Focus Area | Description | Status |
|---|---|---|---|
| **Test 1** | Multi-Page Chained Fixups | Chain crossing across 4 KB page boundaries with `0xFFFF` intermediate start | **PASSED** |
| **Test 2** | `START_MULTI` Overflow Table | Multiple independent chains beginning on a single page via overflow entries | **PASSED** |
| **Test 3** | Malformed Header & Version | Rejection of truncated fixups payload and invalid `fixups_version != 0` | **PASSED** |
| **Test 4** | OOB Import Ordinals | Detection of pointer bind records referring to `ordinal >= imports_count` | **PASSED** |
| **Test 5** | OOB Symbol Name Offsets | Detection of corrupted import descriptors with out-of-bounds `name_offset` | **PASSED** |
| **Test 6** | Loop / Cycle Detection | Verification that memory bounds and traversal guards prevent cycle crashes | **PASSED** |
| **Test 7** | Real iOS Artifact Conformance | Validation of genuine `ios_chain_app` Mach-O chained fixup payload | **PASSED** |

### Regression Target Execution:
- `stage_192_3_chained_fixups_conformance_tests`: **7 passed, 0 failed**
- `stage_v0_chained_fixups_tests`: **3 passed, 0 failed**
- `stage_u1_guest_minidyld_tests`: **9 passed, 0 failed**

---

## 4. Evidence Classification

- **Test Infrastructure:** Unit and integration tests (`UNIT`).
- **Binary Fixtures:** Independently compiled ARM64 Mach-O artifacts (`OPEN_SOURCE`, `PUBLIC_REFERENCE`).
- **Differential Verification:** Host `DyldEngine` vs Guest `minidyld` (`DIFFERENTIAL`).
- **Genuine Apple Kernel / iOS Boot:** **NONE** (Emulation strictly confined to userspace Mach-O EL0 loader structures).

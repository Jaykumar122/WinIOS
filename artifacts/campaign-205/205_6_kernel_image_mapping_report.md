# WinIOS Campaign 205 — Stage 205.6 Engineering Report
## Safe Kernel Image Mapping & Transactional Rollback

**Date:** November 16, 2026  
**Campaign:** 205  
**Stage:** 205.6  
**Deliverables:**
- `src/loader/kernel_image_mapper.h`
- `src/loader/kernel_image_mapper.cpp`
- `tests/unit/stage205_6_kernel_image_mapping_tests.cpp`

---

### 1. Implementation Overview
Stage 205.6 implements `KernelImageMapper`, providing transactional kernel image mapping into guest physical memory.

Key capabilities:
1. **Segment Allocation & Copy:** Iterates over parsed kernel segments, resolves relative physical placement within the configured RAM window, and copies initialized bytes from file offsets.
2. **Deterministic BSS Zero-Fill:** For segments where `vmsize > filesize` (such as `__DATA` and `__BSS`), the mapper explicitly initializes the residual region `[fileoff + filesize, fileoff + vmsize)` with zeros, preventing guest access to stale RAM data.
3. **Transactional Rollback:** Tracks every mapped physical range in `mapped_ranges`. If any segment fails bounds checking, memory allocation, or file offset validation, `rollback()` is automatically invoked to zero out all previously mapped ranges, guaranteeing no partial or corrupt image remains.
4. **Boundary & Overflow Protection:** Validates that `rel_offset + vmsize` never exceeds RAM capacity or target buffer size.

---

### 2. Verification & Test Results
Test suite `stage205_6_kernel_image_mapping_tests` executed:
- Test 1: Valid mapping of `__TEXT` and `__DATA` with verified BSS zero-fill over dirty memory.
- Test 2: Transactional rollback on mid-stream segment error, verifying clean restoration to zero.

**All tests PASSED.** Zero warnings under `/W4 /WX /permissive-`.

**Stage 205.6 Status: PASSED**

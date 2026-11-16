# WinIOS Campaign 203 Stage 203.11: Synthetic VM Object Model Report

## 1. Overview
Stage 203.11 establishes the architectural foundation for Darwin virtual memory management in `src/memory/darwin_vm_object.h` and `src/memory/darwin_vm_object.cpp`. It introduces a 16KB-aligned `DarwinVmObject` representation adhering strictly to Apple Silicon MMU geometry, backing memory access safety, and copy-on-write shadow duplication.

## 2. Key Architecture & Capabilities
1. **16KB Apple Silicon Page Granule:**
   - Enforces `kDarwinPageSize = 16384` (0x4000) and page mask `0x3FFF`.
   - `align_page_up` rounds all allocation requests to 16KB boundaries.
   - `is_page_aligned` validates base addresses, rejecting x86/ARM legacy 4KB alignments.
2. **VM Object Identification & Properties:**
   - Each VM object is assigned an atomic monotonic unique 64-bit identifier.
   - Categorized by `VmMappingType`: `Anonymous`, `FileBacked`, `Shared`, and `CopyOnWrite`.
   - Preserves shared mapping flags across process boundaries.
3. **Memory Access Integrity:**
   - `read_bytes` and `write_bytes` enforce strict offset and length bounds checks against `size_` with integer overflow protection.
   - Prevents memory corruptions or out-of-bounds buffer over-reads.
4. **Copy-on-Write (COW) Shadow Cloning:**
   - `clone_for_cow()` produces a new independent `DarwinVmObject` with type `CopyOnWrite` initialized with the parent's memory contents.
   - Subsequent writes to the shadow clone do not affect the original object, providing clean page-level memory isolation.

## 3. Verification Results
The test suite `tests/unit/stage203_11_vm_object_model_tests.cpp` was compiled and executed under MSVC C++20 (`/W4 /WX /permissive-`):
- Release: 4/4 tests PASSED.
- Debug: 4/4 tests PASSED.
- Cumulative Stage 203.11 tests: 4 registered, 4 passed, 0 failed.

# WINIOS CAMPAIGN 194: STAGE 194.6 REPORT
## Guest Kernel Heap Allocator (`kmalloc` / `kfree`)

### 1. Executive Summary
Stage 194.6 establishes the guest kernel dynamic heap allocator architecture for WinIOS.
Operating at ARM64 EL1, the kernel heap manager provides:
- First-fit dynamic boundary-tag block allocation (`kmalloc`) with 16-byte alignment guarantee.
- Block header structure with magic word verification (`0x4B48454150424C4B` - "KHEAPBLK").
- Forward coalescing on block release (`kfree`) to mitigate external fragmentation.
- Buffer resizing with data preservation (`krealloc`).
- Zero-initialized allocation (`kcalloc`).
- Structural heap integrity validation and corruption detection.
- Self-contained ARM64 machine-code bump allocator routine (`emit_guest_kmalloc_routine`) executing directly on the virtual CPU.

---

### 2. Architecture & Design

#### 2.1 Heap Block Structure
Each heap block is preceded by a 16-byte header:
```cpp
#pragma pack(push, 1)
struct HeapBlockHeader {
    std::uint64_t size_and_flags; // Bit 0: 1 = allocated, 0 = free; Bits [63:1]: total block size
    std::uint64_t magic;          // 0x4B48454150424C4B ("KHEAPBLK")
};
#pragma pack(pop)
```
- **Alignment:** All allocated memory payloads are aligned to 16 bytes (`kHeapAlignment = 16`).
- **Splitting:** If a free block has excess capacity (`>= needed + 32` bytes), it is split into an allocated block and a newly formed free block.
- **Coalescing:** When a block is freed, the allocator inspects the adjacent next block in memory; if free, they are coalesced into a single contiguous block.

#### 2.2 Guest ARM64 Bump Allocator Routine
For minimal-overhead low-level kernel bootstrap paths, `emit_guest_kmalloc_routine` produces an authentic 13-instruction ARM64 subroutine:
1. `add x1, x1, #15` & `bic x1, x1, #15` (align request to 16 bytes).
2. `ldr x3, [x0]` (load `current_heap_ptr`).
3. `ldr x4, [x0, #8]` (load `end_heap_ptr`).
4. `add x5, x3, x1` (compute `new_curr`).
5. `cmp x5, x4` followed by `b.hi .Loom` (bounds check).
6. `str x5, [x0]` (update pointer in guest memory).
7. `mov x0, x3` & `ret` (return allocated pointer).
8. `.Loom`: `mov x0, #0` & `ret`.

---

### 3. Verification & Results

Unit test suite `tests/unit/stage194_6_kernel_heap_tests.cpp` executes 6 comprehensive test cases:
1. `test_heap_initialization_and_alignment`: Verifies root block setup, 16-byte alignment of payloads, stats accounting.
2. `test_kmalloc_kfree_lifecycle`: Validates multiple allocations, read/write payload integrity, slot reuse.
3. `test_forward_coalescing`: Validates forward merging of adjacent free blocks to satisfy larger requests.
4. `test_krealloc_and_kcalloc`: Validates memory zeroing and reallocation with byte preservation.
5. `test_corruption_detection_and_boundaries`: Validates rejection of double-frees, out-of-bounds pointers, and corrupted magic.
6. `test_guest_arm64_bump_allocator`: Steps virtual ARM64 CPU through guest instructions, verifying pointer alignment, control block updates, and OOM handling.

Both Release and Debug configurations build cleanly under `/W4 /WX /permissive-` and pass all 6 tests with zero errors.

---

### 4. Classification
- Source implementations: `GUEST_MEMORY_MANAGER` / `SYNTHETIC_KERNEL`
- Simulation / host harness: `HOST_MODEL`
